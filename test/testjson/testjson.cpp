#include <nlohmann/json.hpp>  // 推荐使用系统头文件路径
using json = nlohmann::json;

#include <iostream>
#include <vector>
#include <map>
#include <string>
using namespace std;

string func1()
{
    json js;
    js["msg_type"] = 2;
    js["from"] = "zhangsan";
    js["to"] = "li si";                          // 加分号
    js["msg"] = "hello, what are you dong now?";  // 加分号
    
    //转成字符串
    string sendBuf=js.dump();
    cout << js << endl;
    cout<<sendBuf.c_str()<<endl;
    return sendBuf;
}

string func2()
{
    json js;

    //添加数组
    js["id"]={1,2,3,4,5};

    //添加key-value
    js["name"]="zhang san";

    //添加对象
    js["msg"]["zhang san"]="hello world";
    js["msg"]["liu shuo"]="hello china";

    //上面等同于下面这句一次性添加数组对象
    js["msg"]={{"zhang san","hello world"},{"liu shuo","hello china"}};

    cout<<js<<endl;
    return js.dump();

}

//json 序列化容器
string func3()
{
    json js;

    //直接序列化一个vector容器
    vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    js["list"]=vec;

    //直接序列化一个 map容器

    map<int,string> mp;
    mp.insert({1,"ni"});
    mp.insert({2,"er"});
    mp.insert({3,"san"});
     
    js["path"]=mp;

    string sendBuf=js.dump();   //json数据对象 ->序列化 json字符串

    cout<<sendBuf<<endl;
    return sendBuf;
}


int main()
{
    string recvBuf=func1();
    //数据的反序列化  json字符串 -> 反序列化 json数据对象
    json buf=json::parse(recvBuf);
    cout<<buf["msg_type"]<<" "<<buf["to"]<<" "<<buf["from"]<<" "<<buf["msg"]<<endl;
    
    string recvBuf2=func2();
    json buf2=json::parse(recvBuf2);
    cout<<buf2["id"]<<" "<<buf2["name"]<<endl;
    json buf2_=buf2["msg"];
    cout<<buf2_["zhang san"]<<" "<<buf2_["liu shuo"]<<endl;


    string recvBuf3=func3();
    json buf3=json::parse(recvBuf3);
    vector<int> v=buf3["list"];
    map<int,string> mp=buf3["path"];
    for(auto &a:v) cout<<a<<" ";
    for(auto &a:mp) cout<<a.first<<" "<<a.second<<endl;
    return 0;
}