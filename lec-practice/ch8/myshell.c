#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXARGS 64

int main() {
    char line[1024];        // 存放用户输入的一整行
    char *argv[MAXARGS];    // 存放分词后的参数数组，最后以 NULL 结尾

    while (1) {
        printf("myshell> ");                // 打印提示符
        fflush(stdout);                     // 立即刷新，否则提示符可能卡在缓冲区
        if (!fgets(line, sizeof(line), stdin)) break;   // 读一行；EOF（Ctrl-D）则退出循环

        // 把这一行按空格、制表符、换行切分成多个 token
        int argc = 0;
        char *token = strtok(line, " \t\n");
        while (token && argc < MAXARGS - 1) {
            argv[argc++] = token;           // 每个 token 作为一个参数
            token = strtok(NULL, " \t\n");  // 继续取下一个 token
        }

        argv[argc] = NULL;                  // execvp 要求参数数组以 NULL 结尾
        if (argc == 0) continue;            // 空行，跳过

        // 内置命令：quit 直接退出 shell
        if (strcmp(argv[0], "quit") == 0) exit(0);

        // fork + exec 执行外部命令
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程：用 argv[0] 在 PATH 中查找可执行文件并替换自身
            execvp(argv[0], argv);
            // 只有 execvp 失败才会走到这里
            fprintf(stderr, "%s cannot find command\n", argv[0]);
            exit(1);
        }

        // 父进程（shell 本体）：等子进程执行完，再回到循环顶部继续接受下一条命令
        waitpid(pid, NULL, 0);
    }
    return 0;
}