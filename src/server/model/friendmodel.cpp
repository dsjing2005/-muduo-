#include"friendmodel.hpp"
#include "db.h"
//添加好友关系
bool FriendModel::insert(int userid,int friendid)
{
    //1.组装sql语句
    char sql[1024]={0};
    sprintf(sql,"insert into Friend values(%d,%d)",userid,friendid);

    MySQL mysql;
    if(mysql.connect())
    {
        return mysql.update(sql);
    }
    return false;
}

//返回用户好友列表
std::vector<User> FriendModel::query(int userid)
{
    //1.组装sql语句
    char sql[1024]={0};
    //select us.id,us.name,us.state from Friend fri left join User us 
    //on fri.friendid=us.id where us.id=1;
    sprintf(sql,"select us.id,us.name,us.state from Friend fri join User us on fri.friendid=us.id where fri.userid=%d;",userid);

    std::vector<User> vec;
    MySQL mysql;
    if(mysql.connect())
    {
        MYSQL_RES* res=mysql.query(sql);
        if(res!=nullptr)
        {
            MYSQL_ROW row;
            while((row=mysql_fetch_row(res))!=nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                vec.push_back(user);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}