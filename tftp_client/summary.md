# TFTP客户端  

## 初始化客户端   

- 接收传来的服务器IP地址
- 生成用于通信的套接字
- 初始化服务器地址信息结构体
- 设置时间限制，避免无限拥塞

```c++
    struct timeval timeout;
    timeout.tv_sec = 5;  //秒
    timeout.tv_usec = 0; //毫秒
    //设置套接字 setsockopt
    if(setsockopt(fd,SOL_SOCKET, SO_RCVTIMEO,&timeout,sizeof(timeout))==-1){
        ERR_LOG("setsockopt error:");
    }
```  
- 定义用于打印错误信息的宏
```c++
#define ERR_LOG(msg) do{ \
    perror(msg);\
    cout<<__LINE__<<" "<<__FILE__<<" "<<__func__<<endl;\
}while(0);
```  

## 下载功能 

### 准备工作

首先要判断套接字能不能用，是不是大于零  
输入文件名，进行判空。如果文件名为空，报错返回。在输入文件名的时候使用`getline(cin,filename)`，避免因为空格出错。
使用二进制和创建写的方式打开指定的文件，要用二进制的形式  
```c++
ofstream file(filename,ios::binary|ios::trunc);
    if(!file.is_open()){
        ERR_LOG("file open error:");
        return -1;
    }
```  

组装发给服务器端端请求报文。
```c++
    string packet;
    packet+=char(0);
    packet+=char(1);
    packet+=filename;
    packet+=char(0);
    packet+="octet";
    packet+=char(0);

    if(sendto(fd,packet.c_str(),packet.length(),0,(struct sockaddr*)&sin,socklen)==-1){
        ERR_LOG("sendto error:");
        file.close();
        return -1;
    }
```  

在开始循环接受服务器数据之前，先定义几个变量。
```c++
    string lastSentPacket = packet; //上一次发送的报文，用于重试发送
    const int maxRetry = 3; //最大重传次数
    int retryCount = 0; //已经传输的次数
    unsigned short expectedBlock = 1; //期待的下一个块编号
    sockaddr_in peerAddr;  //锁定的地址信息结构体
    bzero(&peerAddr,sizeof(peerAddr)); //清空
    bool hasPeer = false; //发送端的地址是否被锁定
```  

### 核心逻辑

接受服务器发来的数据并对个数进行判断。
如果`<0`，并且错误码为`EAGAIN`或`EWOULDBLOCK`，说明套接字超时。如果没达到最大重发次数，则重新发送`lastSentPacket`，否则直接报错。别忘了最后把`retryCount = 0`
如果`<4`，说明收到异常短包，continue

对接受到的信息来源进行校验，锁定发送方
```c++
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
```  

开始解析服务器报文的操作码和块编号，3为数据包，正常进行。5为错误包，直接打印错误。其他的则为异常操作码。
在解析操作码和块编号时，因为规定一个占两个字节所以要将两个元素拼接为一个数
```c++
unsigned short opcode = (static_cast<unsigned char>(buff_data[0])<<8)|static_cast<unsigned char>(buff_data[1]);
        
unsigned short block =(static_cast<unsigned char>(buff_data[2])<<8)| static_cast<unsigned char>(buff_data[3]);

```  

如果服务器报文中的块编号等于期待块编号，那么直接将报文数据段写入文件中。如果是期待编号的前一个，则可能是因为超时重传，需要再次发送ack报文。若是其他，则直接跳过。  

下一步是组装ack报文。与解析时相反，ack报文中的操作码和块编号都是由一个数字拆成两个字节
```c++
char ackPacket[4];
ackPacket[0] = 0;
ackPacket[1] = 4;
// COPILOT-MOD-C4E: 按网络字节序拆分block 组装 ACK。
ackPacket[2] = static_cast<char>((block>>8)&0xff);
ackPacket[3] = static_cast<char>(block&0xff);
if(sendto(fd,ackPacket,4,0,(sockaddr*)&sin,socklen)==-1){
        ERR_LOG("sendto error");
        file.close();
        return -1;
}
```

更新用于超时重发的数据包，如果这一次收到的块编号就是期待块编号，那么期待块编号++

下载结束的标志是收到的数据长度<512，并且块编号等于期待块编号的前一个。

## 上传功能

准备工作和上面的下载相似，输入文件名称。检查所选对文件是否存在。打开文件准备。

接受服务器报文。超时重传和下载一致。如果操作码为4，说明是服务器的应答包。
检查此时的状态，如果是等待最后ack包阶段并且收到的编号等于最终编号，则上传完成，否则重发上一个包。
平时流程下，判断收到的块编号和（下一个要发的块编号-1）是否相等。相等则说明上一次发送的数据包被确认。从文件中读取最多512字节开始这一次的发送

当读取的字节数小于512字节时，变为准备接受最后应答包阶段，并把最终的块编号更新为next

最后不要忘记next++