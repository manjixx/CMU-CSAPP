# include <unistd.h>

int main() {
    const char *msg = "Hello from syscall!\n";
    write(1, msg, 20);    // 系统调用 write(fd=1, buf=msg, len=20)
    _exit(0);    // 系统调用 _exit(status=0) 直接调用 _exit，不走 C 库的清理流程
}