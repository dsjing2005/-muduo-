#include "offlinemsgmodel.hpp"
#include "db.h"


//存储用户的离线消息
bool OfflineMsgModel::insert(int userid,std::string msg)
{
    MySQL mysql;
    if(!mysql.connect()) return false;
    return mysql.update("insert into OfflineMessage values("+std::to_string(userid)+",'"+mysql.escape(msg)+"')");
}

//删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    //1.组装sql语句
    char sql[1024]={0};
    sprintf(sql,"delete from OfflineMessage where userid =%d",userid);


    MySQL mysql;
    if(mysql.connect())
    {
        mysql.update(sql);
    }
}

//查询用户的离线消息
std::vector<std::string> OfflineMsgModel::query(int userid)
{
    //1.组装sql语句
    char sql[1024]={0};
    sprintf(sql,"select message from OfflineMessage where userid=%d",userid);


    MySQL mysql;
    std::vector<std::string> vec;
    if(mysql.connect())
    {
        MYSQL_RES* res=mysql.query(sql);
        if(res!=nullptr)
        {
            //把userid用户的所有离线消息放入vec中，返回
            MYSQL_ROW row;
            while((row=mysql_fetch_row(res))!=nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}