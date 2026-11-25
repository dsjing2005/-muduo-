#pragma once

#include<cstdint>
#include "json.hpp"

class ChatMessageModel
{
public:
    //写入消息正文，返回数据库分配的消息ID
    uint64_t insert(const nlohmann::json& message,int targetid);

    //按消息ID从新到旧读取，before为0时从最新消息开始
    bool queryDirect(int userid,int peerid,uint64_t before,int limit,nlohmann::json& messages);
    bool queryGroup(int groupid,uint64_t before,int limit,nlohmann::json& messages);

    //返回最近有消息的私聊，包含未互加好友的联系人
    bool queryRecentDirects(int userid,nlohmann::json& conversations);

    //使用消息ID登记和清理离线补收记录
    bool queueOffline(int userid,uint64_t messageid);
    bool queryOffline(int userid,nlohmann::json& messages);
    void removeOffline(int userid,const nlohmann::json& messages);
};
