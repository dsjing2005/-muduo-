# ChatMuduo Web

Web 界面位于 `src/web`，WebSocket 网关位于 `src/gateway`。网关将每条浏览器连接映射到一条 Muduo TCP 连接。Vue 开发模式和构建产物共用同一个 `/ws` 路径。

## 虚拟机内运行

需要 MySQL、Redis、Muduo 的现有运行环境，以及 Node.js 22。首次部署先执行下文的会话历史迁移。下列命令从项目根目录执行。CMake 默认将可执行文件输出到项目的 `bin` 目录。

```bash
cmake -S . -B build
cmake --build build -j2

npm --prefix src/web ci
npm --prefix src/web run build
npm --prefix src/gateway ci
```

在两个终端分别启动服务端和网关。

```bash
./bin/ChatServer 127.0.0.1 6000
```

```bash
CHAT_PORT=6000 npm --prefix src/gateway start
```

网关默认监听 `127.0.0.1:8080`，同时提供页面和 `/ws`。从宿主机访问时，可以用 SSH 转发，然后打开 `http://127.0.0.1:8080`。

```bash
ssh -N -L 8080:127.0.0.1:8080 dsj@192.168.174.128
```

前端开发时保持 ChatServer 和网关运行，再执行 `npm --prefix src/web run dev`。Vite 会代理 `/ws` 到本机的网关。

## Nginx

多实例使用现有 Nginx `stream` 时，将网关的 `CHAT_PORT` 设成 TCP 代理端口（现有配置为 `8000`）。`src/gateway/nginx.conf.example` 提供 HTTP 和 WebSocket 转发示例，放在 Nginx 的 `http` 块内。通过可信局域网以外的网络提供服务前，应配置 HTTPS/WSS 并迁移现有数据库里的明文密码。

## 协议和功能范围

- Muduo TCP 双向使用 4 字节大端长度和 UTF-8 JSON，单帧最大 1 MiB。升级时一起替换所有 ChatServer 实例和命令行 ChatClient。
- Web 页面支持注册、登录、私聊、群聊、离线补收、加好友、创建群组和加入群组。注册后的数字 ID 用于登录。
- 单条聊天消息限制为 200 个 UTF-8 字节。发送回执表示正文已写入 MySQL，在线推送没有已读回执。
- 登录返回最近有消息的 100 个私聊会话。打开一个会话时，页面加载最近 30 条并提供向前翻页。

## 会话历史

先在 ChatServer 使用的 MySQL 数据库中执行迁移，再启动新版本 ChatServer。迁移可重复执行。

```bash
mysql -h <数据库地址> -u <数据库用户> -p <聊天室数据库> < src/server/db/chat_history.sql
```

`ChatMessage` 保存私聊和群聊正文。`ChatMessageOffline` 仅保存待补收的用户和消息 ID。登录返回最多 100 条新版待补收消息，完整记录可在会话内分页读取。新版部署前的在线消息无法从原有数据恢复，已有旧离线消息仍按原来方式补收。

WebSocket 登录后可发送以下历史请求。私聊的 `targetId` 是对方账号 ID，群聊的 `targetId` 是群 ID。私聊范围由登录身份确定，群聊范围由当前群成员身份确定。当前群成员可以查看该群已有历史。

```json
{"msgid":13,"requestId":101,"kind":"direct","targetId":42,"limit":30}
```

响应中的 `messages` 按消息 ID 从早到晚排列。`nextBefore` 是下一页要传回的 `before`，值为 `null` 时已到最早一页。默认取 30 条，最大 50 条。服务端用十进制字符串传递消息 ID，避免浏览器处理无符号 64 位整数时丢失精度。

```json
{"msgid":13,"requestId":102,"kind":"direct","targetId":42,"before":"451","limit":30}
```

发送成功的操作回执携带已保存的 `message` 对象。实时推送、离线补收和历史查询共用 `messageId`。如果保存成功但投递失败，回执会带上 `deliveryPending: true`，接收方仍可从历史中查阅。
