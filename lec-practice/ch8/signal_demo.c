#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void sigint_handler(int sig) {
    // sig = 2 表示 SIGINT 信号
    write(STDOUT_FILENO, "\nCaught SIGINT (Ctrl-C)\n", 25);
}

int main(){
    signal(SIGINT, sigint_handler);    // 注册 SIGINT 信号处理函数,安装 handler

    printf("Press three times Ctrl-C then end\n");

    int count = 0;

    while(count < 3) {
        pause();    // 暂停进程，等待信号
        count++;    // 每次收到信号后计数加 1
        printf("The signal count is %d\n", count);
    }
    printf("Fine, bye\n");
    return 0;
}