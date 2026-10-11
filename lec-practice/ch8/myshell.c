#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXARGS 64

int main(){
    char line[1024];        // 存储用户输入的命令行
    char *argv[MAXARGS];    // 存储分词后的参数数组，最后以 NULL 结尾

    while(1){
        printf("myshell> ");    // 打印提示符
        fflush(stdout);    // 刷新输出缓冲区，确保提示符立即显示
        if(!fgets(line, sizeof(line), stdin)) break;    // 读一行，如果读到 EOF(Ctrl-D) 则退出循环
        
        // 将输入按照 空格，制表符，换行切分成多个 token
        int argc = 0;
        char *token = strtok(line, " \t\n");

        while(token && argc < MAXARGS - 1){
            argv[argc++] = token;    // 将 token 存入 argv 数组A
            token = strtok(NULL, " \t\n");    // 继续切分剩余的字符串
        }
        argv[argc] = NULL;    // 参数数组最后以 NULL 结尾
        if(argc == 0) continue;    // 如果没有输入命令，则继续下一次循环
        
        // 内置命令 quit
        if(strcmp(argv[0], "quit") == 0){
            exit(0);    // 退出 myshell
        }

        // fork-exec 执行外部命令
        pid_t pid = fork();    // 创建子进程
        if(pid == 0) {
            // 子进程：用 argv[0] 在 PATH 中查找可执行文件，并用 argv 作为参数执行
            execvp(argv[0], argv);    // execvp 会替换当前进程
            // 如果 execvp 返回，说明执行失败
            fprintf(stderr, "myshell: command not found: %s\n", argv[0]);
            exit(1);    // 退出子进程，返回错误码
        }

        // 父进程：等待子进程结束
        waitpid(pid, NULL, 0);    
    }
    return 0;    // 正常退出 myshell
}