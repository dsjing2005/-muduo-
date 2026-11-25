#include "db.h"
#include<muduo/base/Logging.h>
// 数据库配置信息
static std::string server="127.0.0.1";
static std::string user="dsj";
static std::string password="DSJdsj2005!";
static std::string dbname="chat";

//初始化数据库连接
MySQL::MySQL()
{
    _conn=mysql_init(nullptr);
}


//释放数据库连接资源
MySQL::~MySQL()
{
    if(_conn!=nullptr)
    {
        mysql_close(_conn);
    }
}

//连接数据库
bool MySQL::connect()
{
    MYSQL* p=mysql_real_connect(_conn,server.c_str(),user.c_str(),
    password.c_str(),dbname.c_str(),3306,nullptr,0);
    
    if(p!=nullptr)
    {
        //c和C++默认的编码字符是ASCII，如果不设置，从MySQL上拉下来的是乱码
        if(mysql_set_character_set(_conn,"utf8mb4")!=0)
        {
            LOG_ERROR<<"set mysql charset failed: "<<mysql_error(_conn);
            return false;
        }
        LOG_INFO<<"connect mysql success!";
    }
    else
    {
        // 打印错误信息
        LOG_ERROR << "connect mysql failed: " << mysql_error(_conn);
    }
    return p;
}

//更新操作
bool MySQL::update(std::string sql)
{
    if(mysql_query(_conn,sql.c_str()))
    {
        LOG_INFO<<__FILE__<<":"<<__LINE__<< ":"
                <<sql<<"更新失败！";
        return false;
    }
    return true;
}

//查询操作
MYSQL_RES* MySQL::query(std::string sql)
{
    if(mysql_query(_conn,sql.c_str()))
    {
        LOG_INFO<<__FILE__<<" : "<<__LINE__<<" :"
                <<sql<<"查询失败";
        return nullptr;
    }
    return mysql_use_result(_conn);
}

MYSQL* MySQL::getConnection()
{
    return _conn;
}
std::string MySQL::escape(const std::string& value)
{
    std::string escaped(value.size()*2+1,'\0');
    escaped.resize(mysql_real_escape_string(_conn,&escaped[0],value.data(),value.size()));
    return escaped;
}
