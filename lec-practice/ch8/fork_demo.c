# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>

int main() {
    pid_t pid;
    int x = 1;

    pid = fork(); // Create a child process,调用一次，返回两次

    if(pid == 0){
        // Child process, return value is 0
        printf("Child process: pid=%d, x = %d\n", getpid(),++x);
        exit(0);
    }

    // Parent process, return value is the child's PID
    printf("Parent process: pid=%d, Child PID = %d, x=%d\n", getpid(), pid, --x);
    exit(0);
}