#include "chatmessagemodel.hpp"
#include "db.h"
#include "public.hpp"
#include<string>

using json=nlohmann::json;

namespace
{
bool queryMessages(const std::string& sql,json& messages)
{
    MySQL mysql;
    if(!mysql.connect()) return false;
    MYSQL_RES* res=mysql.query(sql);
    if(!res) return false;

    messages=json::array();
    while(MYSQL_ROW row=mysql_fetch_row(res))
    {
        json message=json::parse(row[1],nullptr,false);
        if(!message.is_object())
        {
            mysql_free_result(res);
            return false;
        }
        message["messageId"]=row[0];
        messages.push_back(std::move(message));
    }
    mysql_free_result(res);
    return true;
}

std::string historyPage(uint64_t before,int limit)
{
    std::string sql;
    if(before>0) sql=" and id < "+std::to_string(before);
    return sql+" order by id desc limit "+std::to_string(limit);
}
}

uint64_t ChatMessageModel::insert(const json& message,int targetid)
{
    MySQL mysql;
    if(!mysql.connect()) return 0;

    std::string sql="insert into ChatMessage(msgtype,sender_id,target_id,payload) values("+
        std::to_string(message.at("msgid").get<int>())+","+
        std::to_string(message.at("id").get<int>())+","+
        std::to_string(targetid)+",'"+mysql.escape(message.dump())+"')";
    if(!mysql.update(sql)) return 0;
    return mysql_insert_id(mysql.getConnection());
}

bool ChatMessageModel::queryDirect(int userid,int peerid,uint64_t before,int limit,json& messages)
{
    std::string user=std::to_string(userid);
    std::string peer=std::to_string(peerid);
    std::string sql="select id,payload from ChatMessage where msgtype="+std::to_string(ONE_CHAT_MSG)+
        " and ((sender_id="+user+" and target_id="+peer+") or (sender_id="+peer+" and target_id="+user+"))";
    return queryMessages(sql+historyPage(before,limit),messages);
}

bool ChatMessageModel::queryGroup(int groupid,uint64_t before,int limit,json& messages)
{
    std::string sql="select id,payload from ChatMessage where msgtype="+std::to_string(GROUP_CHAT_MSG)+
        " and target_id="+std::to_string(groupid);
    return queryMessages(sql+historyPage(before,limit),messages);
}

bool ChatMessageModel::queryRecentDirects(int userid,json& conversations)
{
    MySQL mysql;
    if(!mysql.connect()) return false;

    std::string direct=std::to_string(ONE_CHAT_MSG);
    std::string user=std::to_string(userid);
    std::string sql="select u.id,u.name from ("
        "select peer_id,max(last_id) as last_id from ("
        "select target_id as peer_id,max(id) as last_id from ChatMessage where msgtype="+direct+
        " and sender_id="+user+" group by target_id union all "
        "select sender_id as peer_id,max(id) as last_id from ChatMessage where msgtype="+direct+
        " and target_id="+user+" group by sender_id"
        ") as messages group by peer_id order by last_id desc limit 100"
        ") as recent join User u on u.id=recent.peer_id order by recent.last_id desc";
    MYSQL_RES* res=mysql.query(sql);
    if(!res) return false;

    conversations=json::array();
    while(MYSQL_ROW row=mysql_fetch_row(res))
    {
        conversations.push_back({{"id",atoi(row[0])},{"name",row[1]}});
    }
    mysql_free_result(res);
    return true;
}

bool ChatMessageModel::queueOffline(int userid,uint64_t messageid)
{
    MySQL mysql;
    return mysql.connect()&&mysql.update("insert into ChatMessageOffline(userid,message_id) values("+
        std::to_string(userid)+","+std::to_string(messageid)+") on duplicate key update message_id=ChatMessageOffline.message_id");
}

bool ChatMessageModel::queryOffline(int userid,json& messages)
{
    std::string sql="select m.id,m.payload from ChatMessageOffline o join ChatMessage m on m.id=o.message_id"
        " where o.userid="+std::to_string(userid)+" order by o.message_id limit 100";
    return queryMessages(sql,messages);
}

void ChatMessageModel::removeOffline(int userid,const json& messages)
{
    if(messages.empty()) return;
    std::string ids;
    for(const auto& message:messages)
    {
        if(!ids.empty()) ids+=",";
        ids+=message.at("messageId").get<std::string>();
    }

    MySQL mysql;
    if(mysql.connect())
    {
        mysql.update("delete from ChatMessageOffline where userid="+std::to_string(userid)+
            " and message_id in ("+ids+")");
    }
}
