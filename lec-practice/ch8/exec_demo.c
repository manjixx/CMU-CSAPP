# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <sys/wait.h>

int main() {
    pid_t pid = fork();

    if (pid == 0) {
       // Child process load /bin/ls replace itself
       char *argv[] = {"ls", "-l", NULL};
       execve("/bin/ls", argv, NULL);
       // only reached if execve fails
       perror("execve failed");
       exit(1);
    }

    // parent process waits for the child to finish
    waitpid(pid, NULL, 0);
    printf("ls process finished\n");
    return 0;
}