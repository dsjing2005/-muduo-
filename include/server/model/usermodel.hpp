#pragma once

#include "user.hpp"

//User表的数据操作类
class UserModel
{
public:
    //user表的增加方法
    bool insert(User& user);
    
    //根据用户号码，查询用户信息
    User query(int id);

    //跟新用户信息
    bool claimOnline(int userid);
    bool updateState(User& user);

    //重置用户的状态信息
    void resetState();
};