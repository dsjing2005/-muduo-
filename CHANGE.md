# ChatMuduo 升级与迁移指南

本文覆盖从原版 Muduo 命令行聊天室升级到带有 Vue 前端、WebSocket 网关和 MySQL 会话历史的版本。若已运行支持长度分帧的早期 Web 版，先完成备份和数据库迁移，再更新全部 ChatServer 和 Web 构建产物。该阶段沿用现有 TCP 帧格式。

## 迁移范围

| 组件 | 原版 | 当前版本 |
| --- | --- | --- |
| MySQL | 账号、好友、群组关系和待补收离线消息 | 新增消息正文和离线补收索引，覆盖私聊与群聊 |
| Muduo 服务端 | TCP 接收回调直接解析缓冲区中的 JSON | 按帧解析，使用连接绑定身份，写入历史并提供回执和分页接口 |
| C++ 客户端 | 发送带 NUL 结尾的 JSON，直接读取一次 `recv()` 的结果 | 完整读写长度帧，注销后等待回执并回到登录菜单 |
| Redis | 按账号 ID 订阅跨实例消息 | 使用 `PUBLISH` 和订阅数判断跨实例投递，新版消息离线时登记消息 ID |
| Nginx | `stream` 转发命令行客户端的 TCP 连接 | 另可通过 HTTP 入口转发 Web 页面和 `/ws`，并沿用 TCP 负载均衡 |
| Web | 命令行客户端提供交互 | Vue 页面和 Node.js 网关提供浏览器入口 |

## 1. 核对环境并备份

以下命令在虚拟机的项目根目录执行。当前虚拟机使用 GCC 8.5、CMake 3.26.5、MySQL 8.0.46、Redis 5.0.3、Node.js 22 和 Nginx 1.26.2。部署到其他环境时，确认 Muduo、nlohmann/json、MySQL 客户端开发库和 hiredis 可用。使用 Nginx TCP 代理时，确认 Nginx 支持 `stream`。

查看 `src/server/db/db.cpp`，确认 ChatServer 使用的 MySQL 主机、账号和数据库名。Redis 地址定义在 `src/server/redis/redis.cpp`，当前连接 `127.0.0.1:6379`。多实例应使用同一套 MySQL 数据和 Redis 发布订阅；如果把实例拆到不同主机，需要让实例连接同一个 Redis 服务。

从原版切换时，先关闭旧入口的新增连接，通知在线用户退出，并确认所有旧实例的现有连接已退出。然后停止旧实例并备份。旧版 ChatServer 的 `SIGINT` 处理会批量重置 `User` 表中的在线状态，应等全部实例停止接流后再用该信号退出。

```bash
read -r -p 'MySQL 主机: ' db_host
read -r -p 'MySQL 用户: ' db_user
read -r -p 'MySQL 数据库: ' db_name

umask 077
backup=$(mktemp -d ../chatmuduo-upgrade.XXXXXX)
cp -p bin/ChatServer bin/ChatClient "$backup/"
mysqldump --single-transaction --no-tablespaces \
  -h "$db_host" -u "$db_user" -p "$db_name" > "$backup/chatmuduo.sql"
test -s "$backup/chatmuduo.sql"
```

确认 `mysqldump` 成功退出并保留备份目录。另存当前源代码版本、实际使用的 Nginx 配置及其 include 文件。若正在升级已有 Web 版，同时保存网关运行参数和当前 Web 构建产物。项目的 CMake 输出路径为 `bin/`，即使使用新的构建目录，重新编译也会覆盖该目录里的可执行文件。

## 2. 迁移 MySQL

历史迁移依赖原聊天室数据库。迁移到空数据库时，先从原项目导入基础表及需要保留的数据，再执行 `chat_history.sql`。已有数据库可以直接执行增量脚本。

| 表 | 升级后的用途 |
| --- | --- |
| `User` | 保留账号 ID 和凭据。登录时原子更新在线状态，用于防止多实例重复登录。 |
| `Friend` | 保存好友关系。好友列表以登录用户的 `userid` 查询。 |
| `AllGroup`、`GroupUser` | 保存群信息与成员关系，并为群聊发送和群历史查询提供权限依据。 |
| `OfflineMessage` | 继续补收旧版已经写入的离线 JSON。 |
| `ChatMessage` | 保存私聊和群聊正文。主键 `id` 为无符号 64 位自增 ID，按发送方、接收方和群号建立查询索引。 |
| `ChatMessageOffline` | 保存 `userid` 和 `message_id` 的组合键。补收时关联 `ChatMessage` 获取正文，补收后清理对应索引。 |

使用有建表权限的账号执行迁移，再核对表结构。沿用上一步设置的连接参数。

```bash
mysql -h "$db_host" -u "$db_user" -p "$db_name" \
  < src/server/db/chat_history.sql

mysql -h "$db_host" -u "$db_user" -p "$db_name" \
  -e 'SHOW CREATE TABLE ChatMessage; SHOW CREATE TABLE ChatMessageOffline;'
```

两张新表均使用 InnoDB 和 `utf8mb4`。脚本以 `CREATE TABLE IF NOT EXISTS` 创建新表，可以重复执行。ChatServer 的数据库账号需要能查询和写入消息正文，并能查询、写入和删除离线索引。

历史从新版接收并写入的消息开始积累。原版在线消息没有入库记录，因此无法回填。原版积压的离线消息仍可在登录时补收。新版登录一次最多回送 100 条待补收消息，历史接口可以继续查询同一会话的完整新版记录。

## 3. 构建并切换 ChatServer 和 ChatClient

```bash
cmake -S . -B build-web
cmake --build build-web -j2
ldd bin/ChatServer
redis-cli -h 127.0.0.1 -p 6379 ping
```

`ldd` 用于确认 MySQL 和 hiredis 等动态库都解析到了可用的路径，Redis 正常时会返回 `PONG`。当前构建会为找到的 hiredis 目录设置构建产物的运行时库搜索路径。

### TCP 帧格式

新版本客户端和服务端都通过 `include/chatcodec.hpp` 使用同一套帧格式。

```text
前 4 字节             后续 N 字节
大端无符号整数 N       UTF-8 JSON 正文
```

长度仅计算正文，范围为 1 字节至 1 MiB。ChatServer 等待完整正文后才解析，并可在一次读取中处理多个帧。ChatClient 对帧头和正文循环读取，发送也循环写入直至完成。旧版二进制使用原始 JSON 协议，因此一个入口下的所有 ChatServer 实例、命令行客户端和 Web 网关需要一起切换到长度分帧协议。

### 服务端和客户端行为

- 服务端在登录后将用户身份绑定到连接。私聊、群聊等操作从连接取得发送者身份，消息时间由服务端生成。
- 私聊和群聊先写入 `ChatMessage`。在线用户通过本实例或 Redis 收到推送，离线用户通过 `ChatMessageOffline` 补收。在线推送、离线补收和分页查询共用同一消息 ID。
- 数据库连接使用 `utf8mb4`，注册、建群和旧式离线 JSON 写入时统一转义字符。创建群与写入创建者成员关系在同一个事务内完成。
- 新版 C++ 客户端保留数字账号登录和原命令入口。输入 `help` 可查看私聊、群聊、好友、群组和注销命令。发送的消息同样进入 MySQL 历史，浏览器可以查询。
- 私聊只能查询登录用户与指定对方的记录。群历史要求当前群成员身份，当前成员可以查看该群加入前的历史。

单实例部署时先在虚拟机启动 ChatServer，在另一个终端用新版 ChatClient 连接它。

```bash
./bin/ChatServer 127.0.0.1 6000
```

```bash
./bin/ChatClient 127.0.0.1 6000
```

## 4. 构建 Web 并接入 Nginx

```bash
npm --prefix src/web ci
npm --prefix src/web run build
npm --prefix src/gateway ci
```

网关在同一个 HTTP 端口提供 `src/web/dist/` 页面和 `/ws`。单实例直连时运行下面的命令，浏览器访问 `http://127.0.0.1:8080/`。

```bash
CHAT_PORT=6000 npm --prefix src/gateway start
```

| 环境变量 | 默认值 | 用途 |
| --- | --- | --- |
| `HOST` | `127.0.0.1` | 网关监听地址 |
| `PORT` | `8080` | 网关的 HTTP 和 WebSocket 端口 |
| `CHAT_HOST` | `127.0.0.1` | ChatServer 或 Nginx TCP 代理地址 |
| `CHAT_PORT` | `8000` | ChatServer 或 Nginx TCP 代理端口 |

多实例部署时，虚拟机的 `/usr/local/nginx/conf/nginx.conf` 已配置 TCP 入口 `8000`，后端为 `127.0.0.1:6000` 和 `127.0.0.1:6002`。在两个终端分别启动下列两个服务端进程。

```bash
./bin/ChatServer 127.0.0.1 6000
./bin/ChatServer 127.0.0.1 6002
```

Nginx 的 TCP 入口启用后，让网关和命令行客户端接入 `8000`。这两条命令也分别在各自的终端执行。

```bash
CHAT_PORT=8000 npm --prefix src/gateway start
./bin/ChatClient 127.0.0.1 8000
```

需要经 Nginx 提供 Web 页面时，在实际使用的 `nginx.conf` 的 `http` 块内引入 `src/gateway/nginx.conf.example`。示例在 `8081` 监听，将页面和 `/ws` 转发给本机 `8080`。WebSocket 路由使用 HTTP/1.1，并转发原始 `Host`、`Upgrade` 和 `Connection`，以便网关检查同源连接。

```nginx
include /home/dsj/project/src/gateway/nginx.conf.example;
```

当前虚拟机 Nginx 安装在 `/usr/local/nginx/`。用实际部署的二进制和配置检查语法，再重载正在运行的 Nginx。若部署使用了自定义 `-c` 或 `-p` 参数，检查和重载时也带上同样的参数。

```bash
/usr/local/nginx/sbin/nginx -t
/usr/local/nginx/sbin/nginx -s reload
```

首次启动时，语法检查通过后执行 `/usr/local/nginx/sbin/nginx` 启动进程。

原版 TCP 升级时，同一 `stream` 上游应全部指向新版实例，再恢复入口。对外运行 Web 时，按部署环境配置 HTTPS/WSS，并单独安排原版明文凭据的迁移。

## 5. 接口变动

`include/public.hpp` 继续使用原有登录、注册、私聊和群聊等消息号 `1–10`，新增以下消息。

| 消息号 | 含义 |
| --- | --- |
| `11` | 刷新好友、群组和最近私聊列表 |
| `12` | 操作回执，通过 `requestId` 匹配业务请求，包含 `operation` 和 `errno` |
| `13` | 读取私聊或群聊的历史页，成功响应同样使用 `13` |

WebSocket 客户端登录后即可查询历史。私聊的 `targetId` 是对方账号 ID，群聊的 `targetId` 是群 ID，`kind` 对应 `direct` 或 `group`。

```json
{"msgid":13,"requestId":101,"kind":"direct","targetId":42,"limit":30}
```

响应的 `messages` 按消息 ID 从早到晚排列。下一页将响应的 `nextBefore` 原样作为请求的 `before`。当 `nextBefore` 为 `null` 时已到最早一页。页大小默认 30，最大 50。消息 ID 和分页游标都是十进制字符串，用于保留 64 位整数精度。

发送消息时可附带 `requestId`。服务端保存后返回包含 `message` 对象的成功回执。若正文已保存但投递出现失败，回执包含 `deliveryPending: true`。前端按 `messageId` 合并历史、实时推送和离线补收。网关还会把原协议中的好友、群成员和离线消息 JSON 字符串数组转换成对象数组。

## 6. 验收

| 检查 | 预期结果 |
| --- | --- |
| 旧账号登录 | 使用原账号 ID 和密码登录，可看到已有好友与群组 |
| C++ 和 Web 互发 | 用两个不同账号，互相收到消息，命令行客户端注销后可再次登录 |
| 双实例转发 | 让两个账号接入不同 ChatServer，私聊和群聊可跨实例到达 |
| 离线补收 | 让接收者退出再发送消息。重新登录后能补收，打开同一会话时同一条消息只显示一次 |
| 会话历史 | 同一会话发送超过 30 条消息，重新登录后加载最近 30 条并可向前翻页 |
| 历史权限 | 非群成员查询群历史时收到错误。第三个账号只能查自己与目标账号之间的私聊 |
| Nginx | 页面和静态资源正常加载，浏览器的 `/ws` 成功升级为 WebSocket |

页面能打开但登录或消息请求失败时，依次核对网关的 `CHAT_PORT`、Nginx 上游端口和各 ChatServer 的实际监听地址。若保存或历史查询报错，确认迁移执行在 ChatServer 实际连接的数据库中，以及运行账号拥有所需权限。

## 7. 回退

1. 先让新版入口停止接流，等待在线连接退出。保存升级期间的新消息和数据库状态，便于恢复后继续使用。
2. 恢复已备份且相互匹配的 ChatServer、ChatClient 及 Nginx 配置。回退到已有 Web 版时，一并恢复对应的网关与 Web 构建产物。
3. 启动回退版本，先验证其原有客户端登录和消息发送，再恢复入口。
4. 将 `ChatMessage` 和 `ChatMessageOffline` 保留在数据库中。原版客户端与服务端只能按旧流程处理 `OfflineMessage`。再次部署新版后，可以继续查询已保存的记录并补收仍在索引中的新版消息。
