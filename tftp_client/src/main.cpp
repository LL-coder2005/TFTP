#include <iostream>
#include "tftp_client.h"
using namespace std;
int main(int argc,const char* argv[]){
    // ===== COPILOT-MOD [M1] 参数校验失败时立即返回，避免访问 argv[1] 越界。 =====
    if(argc!=2){
        cout << "用法: " << argv[0] << " <服务器地址>" << endl;
        return 1;
    }
    try
    {     
        tftp_client tftp_c(argv[1]);
        tftp_c.run();
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return 0;
}