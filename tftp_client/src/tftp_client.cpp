#include "tftp_client.h"
#include "myhead.h"
#include <cerrno>
#include <limits>
#include <filesystem>
#define ERR_LOG(msg) do{ \
    perror(msg);\
    cout<<__LINE__<<" "<<__FILE__<<" "<<__func__<<endl;\
}while(0);

// ===== COPILOT-MOD START [C1] Constructor Networking Setup =====
// COPILOT-MOD-C1: 构造函数初始化与超时配置为本轮修复新增。
tftp_client::tftp_client(string ip):ser_ip(ip)
{
    this->fd = socket(AF_INET,SOCK_DGRAM,0);
    if(fd==-1){
        ERR_LOG("socket error:");
        return;
    }

    this->sin.sin_family = AF_INET;
    this->sin.sin_port = htons(this->port);
    this->sin.sin_addr.s_addr = inet_addr(ser_ip.c_str());

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==-1){
        ERR_LOG("setsockopt error:");
    }

    this->socklen = sizeof(sin);
}
// ===== COPILOT-MOD END   [C1] Constructor Networking Setup =====

tftp_client::~tftp_client(){
    // ===== COPILOT-MOD [C2] fd 关闭边界修复 =====
    // COPILOT-MOD-C2: fd>=0 才关闭，修复了 fd==0 的边界情况。
    if(fd>=0){
        close(fd);
    }
}

void tftp_client::menu(){
    cout << "************简单文件传输协议***************"<< endl;
    cout << "*************1.上传**********************"<< endl;
    cout << "*************2.下载**********************"<< endl;
    cout << "*************3.退出**********************"<< endl;
    cout << "****************************************"<< endl;
}

void tftp_client::cleanScreen(){
    system("clear");
}

void tftp_client::run(){
    // ===== COPILOT-MOD START [C3] Menu Input Robustness =====
    // COPILOT-MOD-C3: 菜单输入增加容错与缓冲区清理，避免 getline 读空。
    int choice = -1;
    while(true){
        cleanScreen();
        menu();
        if(!(cin>>choice)){
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(),'\n');
            cout << "输入无效，请输入数字序号" << endl;
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(),'\n');

        switch(choice){
        case 1:
            doUpload();
            break;
        case 2:
            doDownload();
            break;
        case 3:
            return;
        default:
            cout << "序号错误，请重新输入" << endl;
            break;
        }
    }
}
// ===== COPILOT-MOD END   [C3] Menu Input Robustness =====

int tftp_client::doDownload()
{
    // ===== COPILOT-MOD START [C4] Download State Machine Rewrite =====
    // COPILOT-MOD-C4: 下载流程重写为二进制安全 + 超时重试 + TID 校验。
    if(fd<0){
        cout << "套接字不可用" << endl;
        return -1;
    }

    cout << "请输入下载文件名"<< endl;
    string filename;
    getline(cin,filename);
    if(filename.empty()){
        cout << "文件名不能为空" << endl;
        return -1;
    }

    ofstream file(filename,ios::binary|ios::trunc);
    if(!file.is_open()){
        ERR_LOG("file open error:");
        return -1;
    }

    uint16_t opcode = htons(1);
    string packet;
    packet.append(reinterpret_cast<const char*>(&opcode),2);
    packet+=filename;
    packet+=char(0);
    packet+="octet";
    packet+=char(0);
    if(sendto(fd,packet.data(),packet.size(),0,(struct sockaddr*)&sin,socklen)==-1){
        ERR_LOG("sendto error:");
        file.close();
        return -1;
    }

    string lastSentPacket = packet; //上一次发送的报文，用于重试发送
    const int maxRetry = 3; //最大重传次数
    int retryCount = 0; //已经传输的次数
    unsigned short expectedBlock = 1; //期待的下一个块编号
    sockaddr_in peerAddr;  //锁定的地址信息结构体
    bzero(&peerAddr,sizeof(peerAddr)); //清空
    bool hasPeer = false; //发送端的地址是否被锁定

    while(true){
        char buff_data[BUFF_SIZE]="";
        //用来接收发送方的地址信息的结构体
        sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        int ret = recvfrom(fd,buff_data,BUFF_SIZE,0,(struct sockaddr*)&fromAddr,&fromLen);
        if(ret<0){
            // COPILOT-MOD-C4A: 接收超时后重发最近报文，最多重试 maxRetry 次。
            if(errno==EAGAIN || errno==EWOULDBLOCK){
                if(retryCount>=maxRetry){
                    cout << "下载超时，重试次数已达上限" << endl;
                    file.close();
                    return -1;
                }
                retryCount++;
                if(sendto(fd,lastSentPacket.data(),lastSentPacket.size(),0,(struct sockaddr*)&sin,socklen)==-1){
                    ERR_LOG("sendto error:");
                    file.close();
                    return -1;
                }
                continue;
            }
            ERR_LOG("recvfrom error:");
            file.close();
            return -1;
        }
        //把重试计数器清零，因为已经成功收到数据了
        retryCount = 0;

        if(ret<4){
            // COPILOT-MOD-C4B: 丢弃异常短包，避免越界解析。
            cout << "收到异常短包，已丢弃" << endl;
            continue;
        }

        if(!hasPeer){
            peerAddr = fromAddr; //用于校验后续数据包是否来自同一地址
            sin = fromAddr;
            socklen = fromLen;
            hasPeer = true;
        }else if(fromAddr.sin_addr.s_addr!=peerAddr.sin_addr.s_addr || fromAddr.sin_port!=peerAddr.sin_port){
            // COPILOT-MOD-C4C: TID 不匹配时丢弃，防止串包。
            cout << "收到非会话端口数据包，已丢弃" << endl;
            continue;
        }

        unsigned short opcode = (static_cast<unsigned char>(buff_data[0])<<8)
            | static_cast<unsigned char>(buff_data[1]);
        unsigned short block = (static_cast<unsigned char>(buff_data[2])<<8)
            | static_cast<unsigned char>(buff_data[3]);

        if(opcode==3){
            if(block==expectedBlock){
                // COPILOT-MOD-C4D: 按实际长度二进制写入，替代旧版字符串写入。
                file.write(buff_data+4,ret-4);
                if(!file.good()){
                    ERR_LOG("file write error:");
                    file.close();
                    return -1;
                }
            }else if(block!=static_cast<unsigned short>(expectedBlock-1)){
                continue;
            }

            char ackPacket[4];
            ackPacket[0] = 0;
            ackPacket[1] = 4;
            // COPILOT-MOD-C4E: 按网络字节序拆分 block 组装 ACK。
            ackPacket[2] = static_cast<char>((block>>8)&0xff);
            ackPacket[3] = static_cast<char>(block&0xff);
            if(sendto(fd,ackPacket,4,0,(sockaddr*)&sin,socklen)==-1){
                ERR_LOG("sendto error");
                file.close();
                return -1;
            }
            lastSentPacket.assign(ackPacket,4);

            if(block==expectedBlock){
                expectedBlock++;
            }

            // COPILOT-MOD-C4F: 负载 < 512 视为最后一块，替代 strlen 判定。
            if(ret-4<512 && block==static_cast<unsigned short>(expectedBlock-1)){
                cout << "下载完成" << endl;
                break;
            }
        }else if(opcode==5){
            cout << (buff_data+4) << endl;
            file.close();
            
            return -1;
        }else{
            cout << "收到未知操作码: " << opcode << endl;
            file.close();
            return -1;
        }
    }

    if(file.is_open()){
        file.close();
    }
    return 0;
}
// ===== COPILOT-MOD END   [C4] Download State Machine Rewrite =====

int tftp_client::doUpload(){
    // ===== COPILOT-MOD START [C5] Upload State Machine Rewrite =====
    // COPILOT-MOD-C5: 上传流程重写为 ACK 驱动状态机 + 二进制分块发送。
    if(fd<0){
        cout << "套接字不可用" << endl;
        return -1;
    }

    string filename;
    cout << "请输入上传文件名" << endl;
    getline(cin,filename);
    if(filename.empty()){
        cout << "文件名不能为空" << endl;
        return -1;
    }

    struct stat st;
    if(stat(filename.c_str(),&st)==-1){
        cout << "文件不存在" << endl;
        return -1;
    }

    ifstream file(filename,ios::binary);
    if(!file.is_open()){
        ERR_LOG("file open error :");
        return -1;
    }

    string packet;
    packet+=char(0);
    packet+=char(2);
    packet+=filename;
    packet+=char(0);
    packet+="octet";
    packet+=char(0);
    if(sendto(fd,packet.c_str(),packet.size(),0,(sockaddr*)&sin,socklen)==-1){
        ERR_LOG("sendto error");
        file.close();
        return -1;
    }

    string lastSentPacket = packet;
    const int maxRetry = 3;
    int retryCount = 0;
    unsigned short nextBlock = 1;
    bool waitingFinalAck = false;
    unsigned short finalBlock = 0;
    sockaddr_in peerAddr;
    bzero(&peerAddr,sizeof(peerAddr));
    bool hasPeer = false;

    while(true){
        char buff[BUFF_SIZE]="";
        sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        int ret = recvfrom(fd,buff,BUFF_SIZE,0,(sockaddr*)&fromAddr,&fromLen);
        if(ret<0){
            // COPILOT-MOD-C5A: 接收超时重发最近报文，避免无响应卡死。
            if(errno==EAGAIN || errno==EWOULDBLOCK){
                if(retryCount>=maxRetry){
                    cout << "上传超时，重试次数已达上限" << endl;
                    file.close();
                    return -1;
                }
                retryCount++;
                if(sendto(fd,lastSentPacket.data(),lastSentPacket.size(),0,(sockaddr*)&sin,socklen)==-1){
                    ERR_LOG("sendto error");
                    file.close();
                    return -1;
                }
                continue;
            }
            ERR_LOG("recvfrom error");
            file.close();
            return -1;
        }
        retryCount = 0;

        if(ret<4){
            // COPILOT-MOD-C5B: 丢弃异常短包。
            cout << "收到异常短包，已丢弃" << endl;
            continue;
        }

        if(!hasPeer){
            peerAddr = fromAddr;
            sin = fromAddr;
            socklen = fromLen;
            hasPeer = true;
        }else if(fromAddr.sin_addr.s_addr!=peerAddr.sin_addr.s_addr || fromAddr.sin_port!=peerAddr.sin_port){
            // COPILOT-MOD-C5C: 固定会话对端，非会话端口数据直接丢弃。
            cout << "收到非会话端口数据包，已丢弃" << endl;
            continue;
        }

        unsigned short opcode = (static_cast<unsigned char>(buff[0])<<8)
            | static_cast<unsigned char>(buff[1]);
        unsigned short ackBlock = (static_cast<unsigned char>(buff[2])<<8)
            | static_cast<unsigned char>(buff[3]);

        if(opcode==4){
            if(waitingFinalAck){
                if(ackBlock==finalBlock){
                    cout << "文件上传完毕" << endl;
                    break;
                }
                if(ackBlock<finalBlock){
                    if(sendto(fd,lastSentPacket.data(),lastSentPacket.size(),0,(sockaddr*)&sin,socklen)==-1){
                        ERR_LOG("sendto error");
                        file.close();
                        return -1;
                    }
                }
                continue;
            }

            unsigned short expectedAck = static_cast<unsigned short>(nextBlock-1);
            if(ackBlock!=expectedAck){
                if(ackBlock<expectedAck){
                    if(sendto(fd,lastSentPacket.data(),lastSentPacket.size(),0,(sockaddr*)&sin,socklen)==-1){
                        ERR_LOG("sendto error");
                        file.close();
                        return -1;
                    }
                }
                continue;
            }

            char dataPacket[BUFF_SIZE];
            dataPacket[0] = 0;
            dataPacket[1] = 3;
            dataPacket[2] = static_cast<char>((nextBlock>>8)&0xff);
            dataPacket[3] = static_cast<char>(nextBlock&0xff);

            // COPILOT-MOD-C5D: 每次最多读取 512 字节并按实际读到字节发送。
            file.read(dataPacket+4,512);
            streamsize readBytes = file.gcount();
            int sendLen = static_cast<int>(4+readBytes);

            if(sendto(fd,dataPacket,sendLen,0,(sockaddr*)&sin,socklen)==-1){
                ERR_LOG("sendto error");
                file.close();
                return -1;
            }
            lastSentPacket.assign(dataPacket,sendLen);

            if(readBytes<512){
                // COPILOT-MOD-C5E: 最后一块发送后进入等待最终 ACK 状态。
                waitingFinalAck = true;
                finalBlock = nextBlock;
            }
            nextBlock++;
        }else if(opcode==5){
            cout << (buff+4) << endl;
            file.close();
            std::filesystem::remove(filename);
            return -1;
        }else{
            cout << "收到未知操作码: " << opcode << endl;
            file.close();
            return -1;
        }
    }

    if(file.is_open()){
        file.close();
    }
    return 0;
}
// ===== COPILOT-MOD END   [C5] Upload State Machine Rewrite =====


