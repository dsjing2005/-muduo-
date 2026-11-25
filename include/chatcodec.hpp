#pragma once

#include<arpa/inet.h>
#include<cstdint>
#include<string>

//TCP消息格式：4字节网络字节序长度，后接JSON正文
constexpr uint32_t MAX_CHAT_FRAME=1024*1024;

inline std::string encodeChatFrame(const std::string& message)
{
    uint32_t length=htonl(static_cast<uint32_t>(message.size()));
    return std::string(reinterpret_cast<const char*>(&length),sizeof(length))+message;
}
