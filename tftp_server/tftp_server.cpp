#include "tftp_server.h"
#include "myhead.h"
#include <sstream>
#include <fstream>
void* work(void* argv){
    tftp_work tw = *(tftp_work*)argv;
    tftp_server* server = tw.server;
    auto cleanUp = [&](){
        if(tw.pfd>=0){
            close(tw.pfd);
            tw.pfd = -1;
        }
        delete (tftp_work*)argv;
    };
    //解析客户端的操作码
    unsigned short opcode = static_cast<unsigned short>(tw.buff[0]<<8)|static_cast<unsigned short>(tw.buff[1]&0xff);
    //解析客户端的文件名称    
    auto pos = tw.buff.find('\0',2);
    if(pos==std::string::npos||pos==2){
        ERR_LOG("no filename");
        //send()
        cleanUp();
        return nullptr;
    }
    std::string fileName = tw.buff.substr(2,pos-2);
    //拼接为完整的路径
    std::string fullPath = tftp_server::filePath+fileName;
    auto pos2 = tw.buff.find('\0',pos+1);
    if(pos2==std::string::npos||pos2==pos+1){
        ERR_LOG("no mod:");
        //send()
        cleanUp();
        return nullptr;
    }
    std::string mod = tw.buff.substr(pos+1,pos2-(pos+1));
    if(mod!="octet"&&mod!="OCTET"){
        ERR_LOG("mod error:");
        //send()
        cleanUp();
        return nullptr;
    }
    
    switch(opcode){
    case 1:
        server->doReadRequest(tw.pfd,tw.cin,fullPath);
        break;
    case 2:
        server->doWriteRequest(tw.pfd,tw.cin,fullPath);
        break;
    default:
        //senderr();
        break;
    }
    cleanUp();

    return nullptr;
}

tftp_server::tftp_server(){
    //使用初始化列表初始化路径
    //初始化套接字
    this->sfd = socket(AF_INET,SOCK_DGRAM,0);
    if(sfd<0){
        ERR_LOG("sock error:");
        return;
    }
    //初始化服务器地址信息结构体
    this->sin.sin_family = AF_INET;
    this->sin.sin_port = htons(this->port);
    this->sin.sin_addr.s_addr = htonl(INADDR_ANY);
    
    this->socklen = sizeof(sin);

    //设置套接字快速重用
    int resu = 1;
    if(setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&resu,sizeof(resu))==-1){
        ERR_LOG("reuseaddr error:");
        return;
    }
    //绑定套接字和地址
    if(bind(sfd,(sockaddr*)&sin,socklen)==-1){
        ERR_LOG("bind error");
        return;
    }
}

tftp_server::~tftp_server(){
    if(this->sfd>=0){
        close(sfd); //关闭服务器套接字
    }
}

void tftp_server::run(){
    std::cout << "tftp servering on port:" << this->port<<std::endl;
    std::cout << "file path:" << this->filePath << std::endl;

    while(true){
        std::string buff; //接受信息的容器
        buff.resize(BUFF_SIZE);
        sockaddr_in cin; //客户端信息
        socklen_t len = sizeof(cin);
        int ret = recvfrom(sfd,buff.data(),BUFF_SIZE,0,(sockaddr*)&cin,&len);
        if(ret<0){
            ERR_LOG("recvfrom error:");
            return;
        }
        buff.resize(ret);
        if(buff.size()<=4){
            std::cerr<<"收到异常短包，忽略" << std::endl;
            std::cerr<<buff.size()<<std::endl;
            continue;
        }
        tftp_work pw;
        //创建用于会话的套接字
        pw.pfd = socket(AF_INET,SOCK_DGRAM,0);
        pw.buff = buff;
        pw.cin = cin;
        pw.socklen = len;
        pw.server = this;

        if(pw.pfd==-1){
            ERR_LOG("pthread socket error : ");
            //sendErr();
            return;
        }
        timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        if(setsockopt(pw.pfd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==-1){
            ERR_LOG("setsockopt rcvtime error : ");
             //sendErr();
            close(pw.pfd);
            return;
        }
        //连接客户端
        if(connect(pw.pfd,(sockaddr*)&pw.cin,pw.socklen)==-1){
            ERR_LOG("pthread connect error : ");
            //sendErr();
            close(pw.pfd);
            return;
        }
        
        tftp_work* pthreadWork = new tftp_work(pw);
        pthread_t pthread;
        if(pthread_create(&pthread,nullptr,work,pthreadWork)==-1){
            ERR_LOG("pthread_create error:");
            //sendErr();
            delete(pthreadWork);
            return;
        }
        if(pthread_detach(pthread)==-1){
            ERR_LOG("pthread_detach error:");
            //sendErr();
            delete(pthreadWork);
            return;
        }
        
    }
    
}

void tftp_server::doReadRequest(int fd,sockaddr_in cin,std::string filePath){
    //先判断文件名是否非空
    if(filePath.empty()){
        ERR_LOG("fileName empty");
        //send
        return;
    }
    
    //以二进制和读的方式打开文件
    std::ifstream file(filePath,std::ios_base::binary|std::ios_base::in);
    if(!file.is_open()){
        ERR_LOG("file open error:");
        //send
        return;
    }

    std::string lastPacket;  //用于超时重传上一个数据包
    int maxRepeat = 3; //最大重传次数
    int repeat = 0; //已经重传次数
    int nextBlockNum = 1; //下一次要发的数据块编号
    bool waitForFinaly = false; //最后数据包已发出，等待最终确认
    int finalyBlock = -1; //最后的块编号

    std::string packet;
    packet+=char(0);
    packet+=char(3);

    //转变为网络字节序
    uint16_t n = htons(static_cast<uint16_t>(nextBlockNum));
    //重新解释地址，以char*的角度来看
    packet.append(reinterpret_cast<const char*>(&n),2);

    std::string data;
    data.resize(512); 
    file.read(data.data(),data.size()); //尝试读取512字节数据
    std::streamsize fileSize = file.gcount(); //得到实际读取的字节数
    data.resize(fileSize);
    packet+=data;

    //向客户端发送数据报
    if(send(fd,packet.c_str(),packet.size(),0)==-1){
        ERR_LOG("pthread send error:");
        //send
        file.close();
        return;
    }

    lastPacket = packet;
    if(fileSize<512){
        //首包不足512字节时，首包就是最后一个数据包
        waitForFinaly = true;
        finalyBlock = nextBlockNum;
    }
    nextBlockNum++;

    while(true){
        std::string rbuff;
        rbuff.resize(BUFF_SIZE);
        int ret=recv(fd,rbuff.data(),BUFF_SIZE,0);
        if(ret<0){
            if((errno==EAGAIN||errno==EWOULDBLOCK)&&repeat<=maxRepeat){
                repeat++;
                if(send(fd,lastPacket.c_str(),lastPacket.size(),0)==-1){
                    ERR_LOG("pthread send error:");
                    file.close();
                    //send
                    return;
                }
                continue;
            }
            else{
                ERR_LOG("time out :");
                file.close();
                //send
                return;
            }
        }

        rbuff.resize(ret);
        if(ret<4){
            //ACK和ERROR包头至少4字节，短包直接忽略
            continue;
        }

        repeat = 0;

        uint16_t opcode = static_cast<uint16_t>(uint8_t(rbuff[0])<<8)|static_cast<uint16_t>(uint8_t(rbuff[1]));
        if(opcode==4){
            uint16_t ackBlock = static_cast<uint16_t>(uint8_t(rbuff[2])<<8)|static_cast<uint16_t>(uint8_t(rbuff[3]));
            
            if(waitForFinaly==true&&ackBlock==finalyBlock){
                std::cout << "file transfer over" << std::endl;
                break;
            }
            
            if(ackBlock==nextBlockNum-1&&waitForFinaly==false){
                //如果上一次发送的数据块被确认，那么可以发送下一个数据块
                std::string packet;
                packet+=char(0);
                packet+=char(3);

                //转变为网络字节序
                uint16_t n= htons(static_cast<uint16_t>(nextBlockNum));
                //重新解释地址，以char*的角度来看
                packet.append(reinterpret_cast<const char*>(&n),2);

                std::string data;
                data.resize(512); 
                file.read(data.data(),data.size()); //尝试读取512字节数据
                if(file.bad()){
                    ERR_LOG("file error:");
                    file.close();
                    //send
                    return;
                }
                std::streamsize fileSize = file.gcount(); //得到实际读取的字节数
                data.resize(fileSize);
                packet+=data;

                //向客户端发送数据报
                if(send(fd,packet.c_str(),packet.size(),0)==-1){
                    ERR_LOG("pthread send error:");
                    file.close();
                    //send
                    return;
                }
                lastPacket = packet;

                if(fileSize<512){
                    //最后一个数据包已经发送
                    waitForFinaly = true;
                    finalyBlock = nextBlockNum;
                }
                nextBlockNum++;
            }else if(ackBlock<=(nextBlockNum-1)){
                //再重新发送一遍上一次的数据包
                if(send(fd,lastPacket.c_str(),lastPacket.size(),0)==-1){
                    ERR_LOG("pthread send error:");
                    file.close();
                    //send
                    return;
                }
            }
            else{
                //收到异常包，忽略
                continue;
            }
        }
        else if(opcode==5){
            //收到错误包，打印错误信息，终止
            std::cerr<<rbuff<<std::endl;
            file.close();
            return;
        }else{
            //收到异常包，终止信息传输
            std::cerr<<"error"<<std::endl;
            file.close();
            return;
        }
    }

    file.close();
}

void tftp_server::doWriteRequest(int fd,sockaddr_in cin,std::string filePath){

}

void tftp_server::sendErr(){

}