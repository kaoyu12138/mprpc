#include "mprpcchannel.h"
#include "mprpcheader.pb.h"
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <error.h>
#include "mprpcapplication.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include "mprpccontroller.h"
#include "zookeeperutil.h"
#include "memory"
#include "functional"

//对caller调用的rpc方法，做统一的数据序列化和反序列化
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response, 
                    google::protobuf::Closure* done){
    //得到rpc请求字符串所需的所有部分： headerSize + servicename + methodname + argssize + args_str
    const google::protobuf::ServiceDescriptor *sd = method->service();
    std::string serviceName = sd->name();
    std::string methodName = method->name();
    uint32_t argsSize = 0;
    std::string args_str;
    if(request->SerializeToString(&args_str)){
        argsSize = args_str.size();
    }else{
        controller->SetFailed("serialize request error!");
        return;
    }

    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(serviceName);
    rpcHeader.set_method_name(methodName);
    rpcHeader.set_args_size(argsSize);

    uint32_t headerSize = 0;
    std::string rpcHeader_str;
    if(rpcHeader.SerializeToString(&rpcHeader_str)){
        headerSize = rpcHeader_str.size();
    }else{
        controller->SetFailed("serialize rpcHeader error!");
        return;
    }

    //组织待发送的rpc请求字符串
    std::string send_rpc_str;
    send_rpc_str.insert(0, std::string((char*)&headerSize,4));  //固定字符串的前四个字节存储headerSize
    send_rpc_str += rpcHeader_str;
    send_rpc_str += args_str;

    //使用socket tcp编程完成远程调用
    //int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    std::unique_ptr<int, std::function<void(int*)>> clientfd(
        new int(socket(AF_INET, SOCK_STREAM, 0)),
         [](int* fd) {
            if(-1 != *fd) close(*fd);
            delete fd;
        }
    );
    if(-1 == *clientfd){
        char errtxt[512] = {0};
        sprintf(errtxt, "create socket error:%d", errno);
        controller->SetFailed(errtxt);
        return;
    }

    // 读取配置文件rpcserver的信息
    // std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    // uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    // rpc调用方想调用service_name的method_name服务，需要查询zk上该服务所在的host信息
    ZkClient zkCli;
    zkCli.Start();
    //  /UserServiceRpc/Login
    std::string method_path = "/" + serviceName + "/" + methodName;
    // 127.0.0.1:8000
    std::string host_data = zkCli.GetData(method_path.c_str());
    if (host_data == "")
    {
        controller->SetFailed(method_path + " is not exist!");
        return;
    }
    int idx = host_data.find(":");
    if (idx == -1)
    {
        controller->SetFailed(method_path + " address is invalid!");
        return;
    }
    std::string ip = host_data.substr(0, idx);
    uint16_t port = atoi(host_data.substr(idx+1, host_data.size()-idx).c_str()); 
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if(-1 == connect(*clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr))){
        close(*clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "connect error:%d", errno);
        controller->SetFailed(errtxt);
        return;
    }
    
    if(-1 == send(*clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0)){
        close(*clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "send error:%d", errno);
        controller->SetFailed(errtxt);
        return;
    }

    //接收rpc请求的响应值
    char recv_buf[1024] = {0};
    int recvSize = 0;
    if(-1 == (recvSize = recv(*clientfd, recv_buf, 1024, 0))){
        //close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "recv error:%d", errno);
        controller->SetFailed(errtxt);
        return;
    }
    
    if(!response->ParseFromArray(recv_buf, recvSize))
    {
        //close(clientfd);
        char errtxt[2048] = {0};
        sprintf(errtxt, "parse error: %s", recv_buf);
        controller->SetFailed(errtxt);
        return;
    }

    //使用智能指针管理clientfd
    //close(clientfd);
}