#include "myhead.h"
#include "tftp_server.h"

int main(int argc,const char* argv[]){

    tftp_server ts;
    try
    {
        ts.run();
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        std::cerr << __LINE__<<" "<<__func__<<" "<<std::endl;
    }
    
    return 0;
}