#include "mprpcconfig.h"
#include <string>

void MprpcConfig::LoadConfigFile(const char* config_file){
    FILE *pf = fopen(config_file, "r");
    if (nullptr == pf){
        exit(EXIT_FAILURE);
    }
    
    while(!feof(pf)){
        char buf[512] = {0};
        fgets(buf, 512, pf);

        std::string src_buf(buf);
        Trim(src_buf);
        
        if(src_buf[0] == '#' || src_buf.empty()) continue;
        int index = src_buf.find('=');
        if(index == -1) continue;

        std::string key = src_buf.substr(0, index);
        Trim(key);
        int endindex = src_buf.find('\n', index);
        std::string value = src_buf.substr(index+1, endindex-index-1);
        Trim(value);

        config_map.insert({key,value});
    }
    
    fclose(pf);
}

std::string MprpcConfig::Load(const std::string &key)
{
    auto it = config_map.find(key);
    if(it == config_map.end()){
        return "";
    }
    return it->second;
}

void MprpcConfig::Trim(std::string &read_buf){
    int index = read_buf.find_first_not_of(' ');
    if(index != -1){
        read_buf = read_buf.substr(index, read_buf.size()-index);
    }
    index = read_buf.find_last_not_of(' ');
    if(index != -1){
       read_buf = read_buf.substr(0, index + 1);
    }
}