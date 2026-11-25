#include "chatcodec.hpp"
#include <muduo/base/Logging.h>
#include "chatserver.hpp"
#include <nlohmann/json.hpp>
#include "chatservice.hpp"
#include <functional>
#include<string>
using namespace std::placeholders;
using json=nlohmann::json;

ChatServer::ChatServer(muduo::net::EventLoop* loop,
        const muduo::net::InetAddress& listenAddr,
        const std::string& nameArg
):_server(loop,listenAddr,nameArg),_loop(loop)
{
    //注册连接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection,this,_1));

    //注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage,this,_1,_2,_3));

    //设置线程数量
    _server.setThreadNum(4);
}


 //启动服务
void ChatServer::start()
{
    _server.start();
}

//上报连接相关信息的回调函数
void ChatServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
{
    //用户断开连接
    if(!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }

}

//上报读写事件相关信息的回调函数
void ChatServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer * buffer,
                           muduo::Timestamp time)
{
    while(buffer->readableBytes()>=sizeof(int32_t))
    {
        uint32_t length=static_cast<uint32_t>(buffer->peekInt32());
        if(length==0||length>MAX_CHAT_FRAME)
        {
            conn->forceClose();
            return;
        }
        if(buffer->readableBytes()<sizeof(int32_t)+length) return;

        buffer->retrieve(sizeof(int32_t));
        try
        {
            json js=json::parse(buffer->retrieveAsString(length));
            auto msgHandler=ChatService::instance()->getHandler(js.at("msgid").get<int>());
            msgHandler(conn,js,time);
        }
        catch(const json::exception& error)
        {
            LOG_WARN<<"invalid chat request: "<<error.id;
            conn->forceClose();
            return;
        }
    }
}
