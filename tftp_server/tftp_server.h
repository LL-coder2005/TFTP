#ifndef _TFTP_SERVER_H
#define _TFTP_SERVER_H

#include "myhead.h"
#include <string>
#define ERR_LOG(msg) do{\
    perror(msg);\
    std::cout << __LINE__<<" "<<__func__<<" "<<__FILE__<<std::endl;\
}while(0);

class tftp_server{

friend void* work(void* argv);

private:
    const int port = 1069; //服务器所用的端口号
    int sfd = -1; //服务器套接字
    sockaddr_in sin; //服务器的地址信息结构体
    socklen_t socklen = sizeof(sin);
    const int BUFF_SIZE=516;
    inline static const std::string filePath = "./file/";// 处理的文件路径
private:
    void doReadRequest(int fd,sockaddr_in cin,std::string filePath);
    void doWriteRequest(int fd,sockaddr_in cin,std::string filePath);
    void sendErr(int fd,sockaddr_in cin,std::string errMsg);
public:
    
    tftp_server();
    ~tftp_server();
    void run();
};


struct tftp_work{
    int pfd = -1; //会话套接字
    std::string buff; //客户端的请求报文
    sockaddr_in cin; //客户端地址信息结构体
    socklen_t socklen; //结构体长度
    tftp_server* server;
};

#endif