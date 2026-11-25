#include "groupmodel.hpp"
#include "db.h"

//创建群组
bool GroupModel::createGroup(AllGroup& allgroup,int userid)
{
    MySQL mysql;
    if(mysql.connect()&&mysql.update("start transaction"))
    {
        std::string sql="insert into AllGroup(groupname,groupdesc) values('"+mysql.escape(allgroup.getName())+
            "','"+mysql.escape(allgroup.getDesc())+"')";
        if(mysql.update(sql))
        {
            allgroup.setId(mysql_insert_id(mysql.getConnection()));
            std::string member="insert into GroupUser values("+std::to_string(allgroup.getId())+
                ","+std::to_string(userid)+",'creator')";
            if(mysql.update(member)&&mysql.update("commit")) return true;
        }
        mysql.update("rollback");
    }
    return false;
}

//加入群组
bool GroupModel::addGroup(int userid,int groupid ,std::string role)
{
    //1.组装sql语句
    char sql[1024]={0};
    sprintf(sql,"insert into GroupUser select id,%d,'%s' from AllGroup where id=%d",
    userid,role.c_str(),groupid);

    MySQL mysql;
    if(mysql.connect())
    {
        return mysql.update(sql)&&mysql_affected_rows(mysql.getConnection())==1;
    }
    return false;
}

bool GroupModel::hasMember(int userid,int groupid)
{
    MySQL mysql;
    if(!mysql.connect()) return false;

    std::string sql="select 1 from GroupUser where groupid="+std::to_string(groupid)+
        " and userid="+std::to_string(userid)+" limit 1";
    MYSQL_RES* res=mysql.query(sql);
    if(!res) return false;

    bool found=mysql_fetch_row(res)!=nullptr;
    mysql_free_result(res);
    return found;
}

//查询用户所在群组信息
std::vector<AllGroup> GroupModel::queryGroup(int userid)
{
    /*
    1.先根据userid在groupuser表中查询出该用户所属的群组信息
    2.再根据群组信息，查询属于该群组的所有用户的userid，并且和user表进行多表查询，查出用户的详细信息
    */

   char sql [1024]={0};
   sprintf(sql,"select ag.id,ag.groupname,ag.groupdesc from AllGroup ag left join \
    GroupUser g on ag.id=g.groupid where g.userid=%d",userid);

    std::vector<AllGroup> groupVec;
    MySQL mysql;
    if(mysql.connect())
    {
        MYSQL_RES* res=mysql.query(sql);
        if(res!=nullptr)
        {
            MYSQL_ROW row;
            while((row=mysql_fetch_row(res))!=nullptr)
            {
                AllGroup group;
                //查出userid所有的群组信息
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                groupVec.push_back(group);
            }
            mysql_free_result(res);
        }
    }
    
    //查询群组的用户信息
    for(AllGroup& a:groupVec)
    {
        sprintf(sql,"select u.id,u.name,u.state,gu.grouprole from User u left join \
        GroupUser gu on u.id=gu.userid where gu.groupid=%d",
        a.getId());

        MYSQL_RES* res=mysql.query(sql);
        if(res!=nullptr)
        {
            MYSQL_ROW row;
            while((row=mysql_fetch_row(res))!=nullptr)
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setRole(row[3]);
                a.getUsers().push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return groupVec;
}

//根据指定的groupid查询群组用户id列表，主要用户群聊业务给群组其他成员群发消息
std::vector<int> GroupModel::queryGroupUsers(int groupid)
{
    char sql[1024]={0};
    sprintf(sql,"select userid from GroupUser where groupid=%d",groupid);

    std::vector<int> idVec;
    MySQL mysql;
    if(mysql.connect())
    {
        MYSQL_RES* res=mysql.query(sql);
        if(res!=nullptr)
        {
            MYSQL_ROW row;
            while((row=mysql_fetch_row(res))!=nullptr)
            {   
                idVec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return idVec;
}