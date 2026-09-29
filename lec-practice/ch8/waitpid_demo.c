#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    int status;
    pid_t pid = fork();

    if (pid == 0) {
        // 子进程：很快退出，变成僵尸
        printf("Child process %d will exit with code 42\n", getpid());
        exit(42);
    }

    // 父进程：先睡 10 秒，故意不回收
    printf("Parent process %d sleeps 10 seconds...\n", getpid());
    printf("In these 10 seconds, run in another terminal:\n");
    printf("  ps -ef | grep %d\n", pid);
    sleep(10);                       // ← 僵尸窗口

    // 10 秒后收尸
    printf("Parent process starts reaping...\n");
    pid_t reaped = waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        printf("Parent process: child %d reaped, exit code = %d\n",
               reaped, WEXITSTATUS(status));
    }
    return 0;
}