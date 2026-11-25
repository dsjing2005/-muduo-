#pragma once

#include<muduo/net/TcpConnection.h>
#include<muduo/net/EventLoop.h>
#include<muduo/net/TcpServer.h>
using namespace muduo::net;
using namespace muduo;
class ChatServer
{
public:

    ChatServer(muduo::net::EventLoop* loop,             //事件循环
        const muduo::net::InetAddress& listenAddr,      //IP + port
        const std::string& nameArg                      //服务器名称
        );

     //启动服务
    void start();
private:
    //上报连接相关信息的回调函数
    void onConnection(const TcpConnectionPtr& conn);
    
    //上报读写事件相关信息的回调函数
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer * buffer,
                           muduo::Timestamp time);
    TcpServer _server;
    EventLoop* _loop;
};