#pragma once

#include<unordered_map>
#include<string>

class MprpcConfig
{
public:
    void LoadConfigFile(const char* config_file);
    std::string Load(const std::string &key);
private:
    //负责存储配置信息：rpcserverip，rpcserverport, zookeeperip, zookeeperport
    std::unordered_map<std::string, std::string> config_map;
    void Trim(std::string &read_buf);
};