#include "server.hpp"
int main()
{
    // MySQL连接参数按本地环境修改
    gobang_server srv("127.0.0.1", "root", "", "gobang", 3306);
    srv.start(8080);
    return 0;
}
