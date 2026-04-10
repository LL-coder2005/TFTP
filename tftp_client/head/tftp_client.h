#ifndef TFTP_CLIENT_H
#define TFTP_CLIENT_H
#define BUFF_SIZE 516
#include "myhead.h"
using namespace std;

class tftp_client{
    
private:
    int fd = -1; //和服务器连接的套接字
    const int port = 1069;
    string ser_ip; //服务器IP地址
    struct sockaddr_in sin; //接受服务器的套接字地址信息结构体
    socklen_t socklen = sizeof(sin); //地址信息结构体长度
public:
    tftp_client(string ip);
    ~tftp_client();
    int doDownload();
    int doUpload();
    void menu();
    void cleanScreen();
    void waitForInput();
    void run();
};


#endif