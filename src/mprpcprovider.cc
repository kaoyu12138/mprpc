#include "mprpcprovider.h"
#include "mprpcapplication.h"
#include "mprpcheader.pb.h"
#include "zookeeperutil.h"


//提供给外部使用，以供发布rpc方法的函数接口
//使用两个map建立：服务名-服务对象-服务对象提供的函数方法，之间的映射
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo serviceInfo;

    const google::protobuf::ServiceDescriptor *serviceDesc = service->GetDescriptor();
    std::string serviceName = serviceDesc->name();
    int methodCnt = serviceDesc->method_count();
    
    for(int i = 0; i < methodCnt; ++ i){
        const google::protobuf::MethodDescriptor* methodDesc = serviceDesc->method(i);
        std::string methodName = methodDesc->name();
        serviceInfo.method_map.insert({methodName, methodDesc}); 
    }
    serviceInfo.service = service;
    service_map.insert({serviceName, serviceInfo});
}

//开启rpc服务节点，提供远程网络调用服务:使用muduo库，分离网络代码和业务代码
void RpcProvider::Run()
{
    //创建地址
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    muduo::net::InetAddress address(ip, port);
    
    //创建TCPserver对象
    muduo::net::TcpServer server(&event_loop, address, "RpcProvider");
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3));

    server.setThreadNum(4);

    // 把当前rpc节点上要发布的服务全部注册到zk上面，让rpc client可以从zk上发现服务
    // session timeout   30s     zkclient 网络I/O线程  1/3 * timeout 时间发送ping消息
    ZkClient zkCli;
    zkCli.Start();
    // service_name为永久性节点    method_name为临时性节点
    for(auto &sp : service_map){
        // /service_name   /UserServiceRpc
        std::string servicePath = "/" + sp.first;
        zkCli.Create(servicePath.c_str(), nullptr, 0);
        for(auto &mp : sp.second.method_map){
            // /service_name/method_name   /UserServiceRpc/Login 存储当前这个rpc服务节点主机的ip和port
            std::string methodPath = servicePath + "/" + mp.first;
            char methodPathData[128] = {0};
            sprintf(methodPathData, "%s:%d", ip.c_str(), port);
            // ZOO_EPHEMERAL表示znode是一个临时性节点
            zkCli.Create(methodPath.c_str(), methodPathData, strlen(methodPathData), ZOO_EPHEMERAL);
        }
    }

    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;
    server.start();
    event_loop.loop();
}


void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& conn){
    //如果和client的连接断开，shutdown即可
    if(!conn->connected()){
        conn->shutdown();
    }
}

//数据格式：header_size(4B) + header_str + args_str(用args_size来取出，防止沾包)
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& conn, 
                            muduo::net::Buffer* buf, 
                            muduo::Timestamp tamp){
    //利用protobuf反序列化字节流，得到client传来的服务名、方法名以及参数列表
    std::string recv_buf = buf->retrieveAllAsString();

    uint32_t header_size = 0;
    //拷贝recv_buf前4B的内容给header_size
    recv_buf.copy((char*)&header_size, 4, 0);

    std::string rpc_header_str = recv_buf.substr(4, header_size);
    mprpc::RpcHeader rpcHeader;
    std::string serviceName;
    std::string methodName;
    uint32_t argsSize;
    if(rpcHeader.ParseFromString(rpc_header_str)){
        serviceName = rpcHeader.service_name();
        methodName = rpcHeader.method_name();
        argsSize = rpcHeader.args_size();
    }else{
        std::cout<< "rpc_header_str:"<< rpc_header_str <<"parse error!" <<std::endl;
        return;
    }

    std::string rpc_args_str = recv_buf.substr(4 + header_size, argsSize);

    //通过建立好的服务名-服务对象-服务方法映射表，获取service对象和method对象  
    auto it = service_map.find(serviceName);
    if(it == service_map.end()){
        std::cout<< serviceName << "is not exist!" <<std::endl;
        return;
    }
    auto mit = it->second.method_map.find(methodName);
    if(mit == it->second.method_map.end()){
        std::cout<< methodName << "is not exist!" <<std::endl;
        return;
    }

    google::protobuf::Service *service = it->second.service;
    const google::protobuf::MethodDescriptor *method = mit->second;

    //生成rpc方法所需要的requset 与 repsonse
    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    if(!request->ParseFromString(rpc_args_str)){
        std::cout<< rpc_args_str <<"request parse error!" <<std::endl;
        return;
    }
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    //给method方法的调用，绑定一个回调函数SendRpcRsp
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider,
                                                                    const muduo::net::TcpConnectionPtr&,
                                                                    google::protobuf::Message*>
                                                                    (this, 
                                                                    &RpcProvider::SendRpcRsp, 
                                                                    conn, response);

    //框架根据远端请求执行方法
    service->CallMethod(method, nullptr, request, response, done);
}
 
void RpcProvider::SendRpcRsp(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response){
    std::string rsp_str;
    if (response->SerializeToString(&rsp_str)){
        //如果response序列化成功，则返回response
        conn->send(rsp_str);
    }else{
        std::cout <<"serialize response_str error!"<< std::endl;
    }
    conn->shutdown(); //响应结束后，由rpcprovider断开连接
 }