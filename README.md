# ChatMuduo

ChatMuduo 是一个基于 Muduo 的 C/S 聊天室。可以使用 C++ 命令行客户端，也可以通过浏览器注册、登录、私聊、群聊、管理好友和群组、补收离线消息以及查看会话历史。注册成功后使用服务分配的数字账号 ID 登录。

## 架构

```text
浏览器 → HTTP / WebSocket 网关 → Muduo ChatServer
C++ ChatClient ─────────── TCP ──┘     ├─ MySQL
                                       └─ Redis Pub/Sub
```

浏览器和网关之间使用同源 `/ws`。网关为每条 WebSocket 建立一条 Muduo TCP 连接。使用多实例部署时，可在网关和 ChatServer 之间接入 Nginx `stream`，并通过 Redis 转发不同实例上的在线消息。MySQL 保存账号、好友、群组、消息正文和离线补收索引。

| 路径 | 用途 |
| --- | --- |
| `src/server/` | Muduo 网络层、聊天业务、MySQL 和 Redis |
| `src/server/db/chat_history.sql` | 会话历史和离线补收索引的数据库迁移 |
| `src/client/` | C++ 命令行客户端 |
| `include/public.hpp`、`include/chatcodec.hpp` | 消息类型和 TCP 帧格式 |
| `src/gateway/` | Node.js WebSocket 网关、静态文件服务和 Nginx 示例 |
| `src/web/` | Vue 3、TypeScript、Pinia 和 Vite 前端 |

## 在虚拟机上运行

从项目根目录执行以下命令。构建环境需要 C++ 编译器、CMake、Muduo、nlohmann/json、MySQL 客户端开发库和 hiredis。Web 部分使用 Node.js 22 和 npm。运行前启动 MySQL 和 Redis，并确保 ChatServer 能连接到配置的数据库及本机 Redis `127.0.0.1:6379`。

首次启用历史功能时，先准备已有聊天室基础表，然后对 ChatServer 实际使用的 MySQL 数据库执行历史迁移。数据库准备和升级命令见 [迁移指南](CHANGE.md#2-迁移-mysql)。

```bash
cmake -S . -B build-web
cmake --build build-web -j2
npm --prefix src/web ci
npm --prefix src/web run build
npm --prefix src/gateway ci
```

CMake 将两个 C++ 可执行文件输出到 `bin/`。分别在两个终端启动服务端和网关。

```bash
./bin/ChatServer 127.0.0.1 6000
```

```bash
CHAT_PORT=6000 npm --prefix src/gateway start
```

在虚拟机中打开 [ChatMuduo](http://127.0.0.1:8080/)。如果从宿主机访问，在宿主机新开终端建立 SSH 转发，然后用宿主机浏览器打开同一个地址。

```bash
ssh -N -L 8080:127.0.0.1:8080 dsj@192.168.174.128
```

使用命令行客户端时，在虚拟机另开终端运行下面的命令。登录后输入 `help` 查看支持的命令。

```bash
./bin/ChatClient 127.0.0.1 6000
```

## 开发和部署

- 前端开发时保持网关运行，再执行 `npm --prefix src/web run dev`。Vite 会将 `/ws` 转发到网关默认端口 `8080`。
- 多实例部署时，将网关的 `CHAT_PORT` 指向 Nginx TCP 代理端口。浏览器入口可参考 `src/gateway/nginx.conf.example`。
- 新消息写入 MySQL 后可分页查询。打开会话时加载最近 30 条，点击「加载更早的消息」继续翻页。账号重新登录后，可从会话列表打开历史。

从旧版升级时，按 [CHANGE.md](CHANGE.md) 完成备份、数据库迁移、客户端和服务端切换以及验收。网关配置和历史请求格式见 [网关说明](src/gateway/README.md)。
