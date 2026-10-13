#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

sigjmp_buf env;    // 声明一个 sigjmp_buf 类型的变量，用于保存环境信息

void alarm_handler(int sig) {
    // sig = 14 表示 SIGALRM 信号
    write(STDOUT_FILENO, "\nCaught SIGALRM\n", 17);
    siglongjmp(env, 1);    // 回到 sigsetjmp 的位置，返回值为 1
}

int main(){
    signal(SIGALRM, alarm_handler);    // 注册 SIGALRM 信号处理函数,安装 handler

    int rc = sigsetjmp(env, 1);    // 保存当前环境，返回值为 0
    if(rc == 0){
        alarm(5);    // 设置一个 5 秒的闹钟
        printf("main: please input character in 5 seconds\n");
        char buf[100];
        fgets(buf, sizeof(buf), stdin);    // 等待用户输入
        alarm(0);    // 取消闹钟
        printf("main: you entered: %s", buf);
    } else {
        printf("main: timeout, rc = %d\n", rc);
    }
    printf("main: end\n");
    return 0;
}