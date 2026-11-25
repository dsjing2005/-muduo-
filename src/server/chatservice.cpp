#include "chatcodec.hpp"
#include <algorithm>
#include <limits>
#include "chatservice.hpp"
#include "public.hpp"
#include<string>
#include<vector>
#include <muduo/base/Logging.h>
namespace
{
void sendMessage(const TcpConnectionPtr& conn,const std::string& message)
{
    if(message.size()>MAX_CHAT_FRAME)
    {
        conn->forceClose();
        return;
    }
    conn->send(encodeChatFrame(message));
}

void reply(const TcpConnectionPtr& conn,const json& request,bool success,const std::string& error="",
    const json& message=nullptr,bool deliveryPending=false)
{
    int operation=request.at("msgid").get<int>();
    if(!request.contains("requestId")&&operation!=LOGINOUT_MSG) return;

    json response={{"msgid",OPERATION_ACK},{"operation",operation},{"errno",success?0:1}};
    if(request.contains("requestId")) response["requestId"]=request["requestId"];
    if(!success) response["errmsg"]=error;
    if(!message.is_null()) response["message"]=message;
    if(deliveryPending) response["deliveryPending"]=true;
    sendMessage(conn,response.dump());
}

bool readPositiveInt(const json& request,const char* key,int& result)
{
    auto value=request.find(key);
    if(value==request.end()||!value->is_number_integer()) return false;
    auto number=value->get<int64_t>();
    if(number<=0||number>std::numeric_limits<int>::max()) return false;
    result=static_cast<int>(number);
    return true;
}

bool readMessageId(const json& value,uint64_t& result)
{
    if(!value.is_string()) return false;
    const auto& text=value.get_ref<const std::string&>();
    if(text.empty()||text.size()>20||text.front()=='0') return false;

    uint64_t id=0;
    for(char digit:text)
    {
        if(digit<'0'||digit>'9'||id>(std::numeric_limits<uint64_t>::max()-(digit-'0'))/10)
        {
            return false;
        }
        id=id*10+(digit-'0');
    }
    result=id;
    return true;
}

bool validText(const std::string& value,size_t maxLength)
{
    return !value.empty()&&value.size()<=maxLength&&value.find('\0')==std::string::npos;
}
}

//获取单例对象的接口函数
ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

//注册消息以及对应的Hanler回调操作
ChatService::ChatService()
{
    //用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG,std::bind(&ChatService::login,this,_1,_2,_3)});
    _msgHandlerMap.insert({REG_MSG,std::bind(&ChatService::reg,this,_1,_2,_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG,std::bind(&ChatService::oneChat,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG,std::bind(&ChatService::addFriend,this,_1,_2,_3)});
    
    //群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG,std::bind(&ChatService::createGroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG,std::bind(&ChatService::addGroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG,std::bind(&ChatService::groupChat,this,_1,_2,_3)});
    _msgHandlerMap.insert({LOGINOUT_MSG,std::bind(&ChatService::loginout,this,_1,_2,_3)});

    _msgHandlerMap.insert({CONTACTS_MSG,std::bind(&ChatService::contacts,this,_1,_2,_3)});

    _msgHandlerMap.insert({HISTORY_MSG,std::bind(&ChatService::history,this,_1,_2,_3)});

    //连接redis服务器
    _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage,this,_1,_2));
    _redis.connect();
}


MsgHandler ChatService::getHandler(int msgid)
{
    //记录错误日志，msgid没有对应的事件处理回调
    auto it=_msgHandlerMap.find(msgid);
    if(it==_msgHandlerMap.end())
    {
        //返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr& conn,json &js,muduo::Timestamp){
            LOG_ERROR<<"msg:id: "<<msgid<<" can not find handler! ";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}


//登录后的业务从连接读取身份
bool ChatService::requireLogin(const TcpConnectionPtr& conn,json& js)
{
    auto* user=boost::any_cast<User>(conn->getMutableContext());
    if(!user)
    {
        reply(conn,js,false,"请先登录");
        return false;
    }
    js["id"]=user->getId();
    js["name"]=user->getName();
    return true;
}

//处理登录业务  ORM 业务层操作的都是对象 DAO
void ChatService::login(const TcpConnectionPtr& conn,json &js,muduo::Timestamp)
{
    //id  pwd   接收连个参数
    int id=js["id"].get<int>();
    std::string pwd=js["password"];

    User user=_userModel.query(id);
    if(user.getId()==id&&user.getPwd()==pwd)
    {
        std::unique_lock<std::mutex> lock(_connMutex);
        if(boost::any_cast<User>(conn->getMutableContext())||!_userModel.claimOnline(id))
        {
            //该用户已经登录，不允许重复登陆
            json response;
            response["msgid"]=LOGIN_MSG_ACK;
            response["errno"]=2;
            response["errmsg"]="该账号已经登录，请重新输入新账号";
            sendMessage(conn,response.dump());
        }
        else 
        {
            _userConnMap.emplace(id,conn);
            user.setPwd("");
            conn->setContext(user);
            _redis.subscribe(id);

            json response;
            response["msgid"]=LOGIN_MSG_ACK;
            response["errno"]=0;
            response["id"]=user.getId();
            response["name"]=user.getName();
            //查询该用户似乎否有离线消息
            auto vec=_offlineMsgModel.query(id);
            bool hasLegacyMessages=!vec.empty();
            json queuedMessages;
            if(_chatMessageModel.queryOffline(id,queuedMessages))
            {
                for(const auto& message:queuedMessages) vec.push_back(message.dump());
            }
            if(!vec.empty()) response["offlinemsg"]=std::move(vec);

            fillContacts(id,response);
            std::string payload=response.dump();
            sendMessage(conn,payload);
            if(payload.size()<=MAX_CHAT_FRAME)
            {
                if(hasLegacyMessages) _offlineMsgModel.remove(id);
                _chatMessageModel.removeOffline(id,queuedMessages);
            }
        }
    }
    else
    {
        //登录失败
        json response;
        response["msgid"]=LOGIN_MSG_ACK;
        response["errno"]=1;
        response["errmsg"]="用户名或密码错误";
        sendMessage(conn,response.dump());
    }
}

void ChatService::fillContacts(int userid,json& response)
{
    json recentDirects;
    if(_chatMessageModel.queryRecentDirects(userid,recentDirects))
    {
        response["recentDirects"]=std::move(recentDirects);
    }

    //查询该用户的好友信息并返回
    std::vector<User> userVec=_friendModel.query(userid);
    if(!userVec.empty())
    {
        std::vector<std::string> vec2;
        for(auto& a:userVec)
        {
            json js;
            js["id"]=a.getId();
            js["name"]=a.getName();
            js["state"]=a.getState();
            vec2.push_back(js.dump());
        }
        response["friends"]=vec2;
    }

    //查询用户群组消息
    std::vector<AllGroup> groupuserVec=_groupModel.queryGroup(userid);
    if(!groupuserVec.empty())
    {
        //group:[{groupid:[xxx,xxx,xxx]}]
        std::vector<std::string> groupV;
        for(auto& group :groupuserVec)
        {
            json grpjson;
            grpjson["id"]=group.getId();
            grpjson["groupname"]=group.getName();
            grpjson["groupdesc"]=group.getDesc();
            std::vector<std::string> userV;
            for(auto& user:group.getUsers())
            {
                json js;
                js["id"]=user.getId();
                js["name"]=user.getName();
                js["state"]=user.getState();
                js["role"]=user.getRole();
                userV.push_back(js.dump());
            }
            grpjson["users"]=userV;
            groupV.push_back(grpjson.dump());
        }

        response["groups"]=groupV;
    }
}

void ChatService::contacts(const TcpConnectionPtr& conn,json& js,muduo::Timestamp)
{
    if(!requireLogin(conn,js)) return;
    json response={{"msgid",CONTACTS_MSG}};
    if(js.contains("requestId")) response["requestId"]=js["requestId"];
    fillContacts(js.at("id").get<int>(),response);
    sendMessage(conn,response.dump());
}

//查询当前用户所在会话的历史记录
void ChatService::history(const TcpConnectionPtr& conn,json& js,muduo::Timestamp)
{
    if(!requireLogin(conn,js)) return;

    int targetid=0;
    int limit=30;
    uint64_t before=0;
    auto kind=js.find("kind");
    if(kind==js.end()||(*kind!="direct"&&*kind!="group")||
        !readPositiveInt(js,"targetId",targetid)||
        (js.contains("limit")&&(!readPositiveInt(js,"limit",limit)||limit>50))||
        (js.contains("before")&&!readMessageId(js["before"],before)))
    {
        reply(conn,js,false,"历史查询参数无效");
        return;
    }

    int userid=js.at("id").get<int>();
    json messages;
    bool success=false;
    if(*kind=="group")
    {
        if(!_groupModel.hasMember(userid,targetid))
        {
            reply(conn,js,false,"只有群成员可以查看群历史");
            return;
        }
        success=_chatMessageModel.queryGroup(targetid,before,limit+1,messages);
    }
    else
    {
        success=_chatMessageModel.queryDirect(userid,targetid,before,limit+1,messages);
    }
    if(!success)
    {
        reply(conn,js,false,"读取会话历史失败");
        return;
    }

    json nextBefore=nullptr;
    if(messages.size()>static_cast<size_t>(limit))
    {
        messages.erase(messages.size()-1);
        nextBefore=messages.back().at("messageId");
    }
    std::reverse(messages.begin(),messages.end());
    json response={{"msgid",HISTORY_MSG},{"errno",0},{"messages",std::move(messages)},
        {"nextBefore",nextBefore}};
    if(js.contains("requestId")) response["requestId"]=js["requestId"];
    sendMessage(conn,response.dump());
}

//处理注册业务  name passwd
void ChatService::reg(const TcpConnectionPtr& conn,json &js,muduo::Timestamp)
{
    std::string name=js["name"];
    std::string pwd=js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state=validText(name,50)&&validText(pwd,50)&&_userModel.insert(user);
    if(state)
    {
        //注册成功
        json response;
        response["msgid"]=REG_MSG_ACK;
        response["errno"]=0;
        response["id"]=user.getId();
        sendMessage(conn,response.dump());
    }
    else
    {
        //注册失败
        json response;
        response["msgid"]=REG_MSG_ACK;
        response["errno"]=1;
        response["errmsg"]="注册失败，请检查名称和密码";
        sendMessage(conn,response.dump());
    }
}

//处理客户端退出
void ChatService::clientCloseException(const TcpConnectionPtr& conn)
{
    auto* session=boost::any_cast<User>(conn->getMutableContext());
    if(!session) return;

    User user=*session;
    std::unique_lock<std::mutex> lock(_connMutex);
    _userConnMap.erase(user.getId());
    _redis.unsubscribe(user.getId());
    user.setState("offline");
    _userModel.updateState(user);
    conn->setContext(boost::any());
}

bool ChatService::deliver(int userid,const std::string& message,uint64_t messageid)
{
    std::unique_lock<std::mutex> lock(_connMutex);
    auto it=_userConnMap.find(userid);
    if(it!=_userConnMap.end())
    {
        sendMessage(it->second,message);
        return true;
    }

    User user=_userModel.query(userid);
    if(user.getId()!=userid) return false;
    if(user.getState()=="online"&&_redis.publish(userid,message)) return true;
    return _chatMessageModel.queueOffline(userid,messageid);
}

void ChatService::oneChat(const TcpConnectionPtr& conn,json& js,muduo::Timestamp time)
{
    if(!requireLogin(conn,js)) return;
    int toid=js.at("toid").get<int>();
    std::string message=js.at("msg").get<std::string>();
    if(toid<=0||!validText(message,200))
    {
        reply(conn,js,false,"请输入有效账号和 200 字节以内的消息");
        return;
    }
    if(_userModel.query(toid).getId()!=toid)
    {
        reply(conn,js,false,"收件人不存在");
        return;
    }
    json event={{"msgid",ONE_CHAT_MSG},{"id",js["id"]},{"name",js["name"]},
        {"toid",toid},{"msg",message},{"time",time.toFormattedString(false)}};
    uint64_t messageid=_chatMessageModel.insert(event,toid);
    if(messageid==0)
    {
        reply(conn,js,false,"保存私聊消息失败");
        return;
    }
    event["messageId"]=std::to_string(messageid);
    bool delivered=deliver(toid,event.dump(),messageid);
    reply(conn,js,true,"",event,!delivered);
}

//服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

//添加好友业务
void ChatService::addFriend(const TcpConnectionPtr& conn,json& js,muduo::Timestamp)
{
    if(!requireLogin(conn,js)) return;
    int userid=js.at("id").get<int>();
    int friendid=js.at("friendid").get<int>();
    bool success=friendid>0&&userid!=friendid&&_userModel.query(friendid).getId()==friendid
        &&_friendModel.insert(userid,friendid);
    reply(conn,js,success,"添加好友失败，请检查账号是否存在或已添加");
}

void ChatService::createGroup(const TcpConnectionPtr& conn,json& js,muduo::Timestamp)
{
    if(!requireLogin(conn,js)) return;
    int userid=js.at("id").get<int>();
    std::string name=js.at("groupname").get<std::string>();
    std::string desc=js.at("groupdesc").get<std::string>();
    if(!validText(name,50)||desc.size()>200||desc.find('\0')!=std::string::npos)
    {
        reply(conn,js,false,"群名称或简介长度无效");
        return;
    }

    AllGroup group(-1,name,desc);
    reply(conn,js,_groupModel.createGroup(group,userid),"创建群组失败");
}

void ChatService::addGroup(const TcpConnectionPtr& conn,json& js,muduo::Timestamp)
{
    if(!requireLogin(conn,js)) return;
    int groupid=js.at("groupid").get<int>();
    int userid=js.at("id").get<int>();
    reply(conn,js,groupid>0&&_groupModel.addGroup(userid,groupid,"normal"),
        "加群失败，请检查群号是否存在或已加入");
}

void ChatService::groupChat(const TcpConnectionPtr& conn,json& js,muduo::Timestamp time)
{
    if(!requireLogin(conn,js)) return;
    int userid=js.at("id").get<int>();
    int groupid=js.at("groupid").get<int>();
    std::string message=js.at("msg").get<std::string>();
    if(groupid<=0||!validText(message,200))
    {
        reply(conn,js,false,"请输入有效群号和 200 字节以内的消息");
        return;
    }

    auto members=_groupModel.queryGroupUsers(groupid);
    if(std::find(members.begin(),members.end(),userid)==members.end())
    {
        reply(conn,js,false,"只有群成员可以发送消息");
        return;
    }

    json event={{"msgid",GROUP_CHAT_MSG},{"id",userid},{"name",js["name"]},
        {"groupid",groupid},{"msg",message},{"time",time.toFormattedString(false)}};
    uint64_t messageid=_chatMessageModel.insert(event,groupid);
    if(messageid==0)
    {
        reply(conn,js,false,"保存群消息失败");
        return;
    }
    event["messageId"]=std::to_string(messageid);
    std::string payload=event.dump();
    bool success=true;
    for(int id:members)
    {
        if(id!=userid&&!deliver(id,payload,messageid)) success=false;
    }
    reply(conn,js,true,"",event,!success);
}

void ChatService::loginout(const TcpConnectionPtr& conn,json& js,muduo::Timestamp)
{
    if(!requireLogin(conn,js)) return;
    clientCloseException(conn);
    reply(conn,js,true);
}

//从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid,std::string msg)
{
    std::lock_guard<std::mutex> lock(_connMutex);
    auto it=_userConnMap.find(userid);
    if(it!=_userConnMap.end())
    {
        sendMessage(it->second,msg);
        return;
    }

    //新版Redis消息只登记离线索引，旧版消息保留原有存储方式
    json event=json::parse(msg,nullptr,false);
    uint64_t messageid=0;
    if(event.is_object()&&event.contains("messageId")&&readMessageId(event["messageId"],messageid))
    {
        _chatMessageModel.queueOffline(userid,messageid);
    }
    else if(event.is_object())
    {
        _offlineMsgModel.insert(userid,msg);
    }
}