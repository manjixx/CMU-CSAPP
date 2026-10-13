#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <errno.h>

sigjmp_buf env;    // 声明一个 sigjmp_buf 类型的变量，用于保存环境信息
pid_t child_pid;    // 子进程的 PID

void alarm_handler(int sig) {
    siglongjmp(env, 1);    // 回到 sigsetjmp 的位置，返回值为 1
}

void sigchild_handler(int sig) {
    int status;
    pid_t pid;
    int saved_errno = errno;    // 保存 errno，以防被 waitpid 修改
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if(WIFEXITED(status)) {
            alarm(0);    // 子进程正常退出，取消闹钟
            siglongjmp(env, 2);    // 回到 sigsetjmp 的位置，返回值为 2
            printf("Child %d exited with status %d\n", pid, WEXITSTATUS(status));
        } else if(WIFSIGNALED(status)) {
            alarm(0);    // 子进程被信号终止，取消闹钟
            siglongjmp(env, 2);    // 回到 sigsetjmp 的位置，返回值为 2
            printf("Child %d terminated by signal %d\n", pid, WTERMSIG(status));
        }
    }
    errno = saved_errno;    // 恢复 errno
}

int main(int argc, char *argv[]){
    int timeout = (argc > 1) ? atoi(argv[1]) : 3;   // 默认超时时间为 3 秒
    int sleep_time = (argc > 2) ? atoi(argv[2]) : 5;   // 默认子进程睡眠时间为 5 秒


    signal(SIGALRM, alarm_handler);    // 注册 SIGALRM 信号处理函数
    signal(SIGCHLD, sigchild_handler);    // 注册 SIGCHLD 信号处理函数

    child_pid = fork();    // 创建子进程
    if(child_pid == 0) {
        // 子进程：执行 sleep
        char sleep_arg[16];
        snprintf(sleep_arg, sizeof(sleep_arg), "%d", sleep_time);
        char *argv[] = {"sleep", sleep_arg, NULL};
        execvp("sleep", argv);    // 执行 sleep 命令
        perror("execvp failed");    // 如果 execvp 返回，说明执行失败
        exit(1);    // 退出子进程，返回错误码
    }

    printf("main: waiting for child process %d to start execute sleep %d and finish with timeout %d seconds\n", child_pid, sleep_time, timeout);

    int rc = sigsetjmp(env, 1);

    if(rc == 0) {
        alarm(timeout);    // 设置一个超时闹钟
        while (1) {
            pause();    // 暂停进程，等待信号
        }
    } else if (rc == 1) {
        // 超时路径
        printf("main: timeout, killing child process %d\n", child_pid);
        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
    } else {
        // rc == 2，子进程已结束，正常退出
        printf("main: child process %d finished before timeout\n", child_pid);
    }
    printf("main: end\n");
    return 0;
}
