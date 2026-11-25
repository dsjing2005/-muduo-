#pragma once

#include <muduo/net/TcpConnection.h>
#include<unordered_map>
#include<mutex>
#include<functional>
#include "json.hpp"
#include "usermodel.hpp"
#include "offlinemsgmodel.hpp"
#include "chatmessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
#include "redis.hpp"
using json=nlohmann::json;

using namespace muduo::net;

//表示处理消息的事件回调方法类型
using MsgHandler=std::function<void(const TcpConnectionPtr& conn,json &js,muduo::Timestamp)>;

// 聊天服务器业务类（单例模式）
class ChatService
{
public:
    //获取单例对象的接口函数
    static ChatService* instance();

    //处理登录业务
    void login(const TcpConnectionPtr& conn,json &js,muduo::Timestamp);

    //处理注册业务 
    void reg(const TcpConnectionPtr& conn,json &js,muduo::Timestamp);

    //一对一聊天业务
    void oneChat(const TcpConnectionPtr& conn,json &js,muduo::Timestamp);

    //添加好友业务
    void addFriend(const TcpConnectionPtr& conn,json &js,muduo::Timestamp);

    //创建群组业务
    void createGroup(const TcpConnectionPtr& conn,json& js,muduo::Timestamp);
    
    //加入群组业务
    void addGroup(const TcpConnectionPtr& conn,json& js,muduo::Timestamp);

    //群组聊天业务
    void groupChat(const TcpConnectionPtr& conn,json& js,muduo::Timestamp);

    //处理注销业务
    void loginout(const TcpConnectionPtr& conn,json& js,muduo::Timestamp);

    //刷新好友和群组
    void contacts(const TcpConnectionPtr& conn,json& js,muduo::Timestamp);

    //查询会话历史
    void history(const TcpConnectionPtr& conn,json& js,muduo::Timestamp);

    //获取消息对应的处理器
    MsgHandler getHandler(int msgid);

    //处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr& conn);

    //从redis消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int userid,std::string msg);
    
    //服务器异常，业务重置方法
    void reset();

private:
    ChatService();

    bool requireLogin(const TcpConnectionPtr& conn,json& js);
    void fillContacts(int userid,json& response);
    bool deliver(int userid,const std::string& message,uint64_t messageid);

    //存储消息id和其对应的业务处理方法
    std::unordered_map<int,MsgHandler> _msgHandlerMap;

    //数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    ChatMessageModel _chatMessageModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    //存储在线用户的通信连接(访问注意线程安全)
    std::unordered_map<int,muduo::net::TcpConnectionPtr> _userConnMap;

    //定义互斥锁，保证_userConnMap的线程安全
    std::mutex _connMutex;

    //定义redis操作对象
    Redis _redis;

};