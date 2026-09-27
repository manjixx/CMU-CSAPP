# 第10章 系统级 I/O——从零开始手把手学

> **适合人群**：能写 C 程序但从没用过 `open/read/write` 的同学  
> **学完你能做到**：解释"short count 是什么"，能写健壮的文件读写，能用 `dup2` 实现 shell 重定向  
> **预计时间**：认真读 + 动手练，约 3 小时

**第10章的主线是：**

- Unix I/O 系统调用接口：`open/read/write/close/lseek/stat/dup2`
- “一切皆文件”的抽象：普通文件、终端、socket、管道都用 fd 操作
- 三个数据结构：fd 表 → 打开文件表条目 → v-node/inode
- 由三个结构引出的行为：文件共享、`fork` 后共享偏移、`dup2` 重定向
- Short count / EINTR：为什么 `read/write` 不保证一次读写完，怎么健壮地循环
- RIO：CSAPP 提供的健壮 I/O 包装
- stdio vs Unix I/O：标准库缓冲 vs 系统调用，什么时候用哪个

---

## 第一关：为什么不直接用 `printf/fread`？

### 1.1 你已经会的 vs 本章要教的

你可能已经会用 C 标准库：

```c
FILE *f = fopen("data.txt", "r");
fgets(buf, 100, f);     // 读一行
fprintf(f, "hello");    // 格式化写
fclose(f);
```

这些函数是**标准 I/O 库**（stdio），它们工作得很好——但它们是建立在更底层的**系统调用**之上的。

**为什么要学底层？**
- 写网络程序时，`fgets` 和 `printf` 在 socket 上有严重问题（缓冲区导致数据"卡住"）
- 写 shell 时，需要用 `dup2` 实现 `>` 重定向，这只有底层接口提供
- 调试文件描述符泄漏时，必须知道底层发生了什么
- **理解底层，才能真正理解为什么标准库有时会出奇怪的问题**

### 1.2 "一切皆文件"——Unix 最重要的哲学

Unix 把**所有 I/O 设备**都抽象成"文件"，用同一套接口操作：

```
磁盘文件     → 文件
终端（键盘/屏幕）→ 文件  （/dev/tty）
网络连接     → 文件  （socket）
管道         → 文件  （pipe）
设备         → 文件  （/dev/sda, /dev/null）
进程信息     → 文件  （/proc/1234/maps）
```

**好处**：学会了操作普通文件的接口，就学会了操作所有 I/O 设备。

---

## 第二关：文件描述符——程序与文件之间的"号码牌"

### 2.1 什么是文件描述符？

当你打开一个文件，内核不会直接给你文件的指针，而是给你一个**小整数**，叫做**文件描述符（File Descriptor，fd）**。

**类比**：你去餐厅点餐，服务员给你一个**号码牌（如：17号）**，而不是直接让你进厨房。你用号码牌领餐，fd 就是你操作文件时用的"号码牌"。

```c
int fd = open("hello.txt", O_RDONLY);
// fd 可能是 3（因为 0、1、2 已被占用，见下面）
```

### 2.2 三个预定义的文件描述符

**每个程序启动时，就已经打开了 3 个文件**：

| fd 号 | 名称 | 默认连接到 | 符号常量 |
|-------|------|-----------|---------|
| 0 | 标准输入（stdin） | 键盘 | `STDIN_FILENO` |
| 1 | 标准输出（stdout）| 屏幕 | `STDOUT_FILENO` |
| 2 | 标准错误（stderr）| 屏幕 | `STDERR_FILENO` |

```c
// 这两行效果完全相同：
printf("Hello\n");                        // 通过标准库（stdio）
write(STDOUT_FILENO, "Hello\n", 6);       // 直接系统调用

// 同样，这两行效果完全相同：
char c = getchar();
read(STDIN_FILENO, &c, 1);
```


### 2.3 内核用三个数据结构管理 I/O

理解这三个数据结构，是理解“文件共享”和“重定向”的关键：


```
进程A的文件描述符表       内核的打开文件表条目               v-node 表
（每进程私有）             （所有进程共享）                   （所有进程共享）

fd=0 ──────────────→ [偏移=0, 引用=1, 标志=O_RDONLY, 指针→终端] ──→ [终端设备]
fd=1 ──────────────→ [偏移=0, 引用=1, 标志=O_WRONLY, 指针→终端] ──→ [终端设备]
fd=2 ──────────────→ [偏移=0, 引用=1, 标志=O_WRONLY, 指针→终端] ──→ [终端设备]
fd=3 ──────────────→ [偏移=127, 引用=1, 标志=O_RDONLY, 指针→hello.txt] → [hello.txt 元数据]
                          ↑
                    记录下一次读/写从第几个字节开始
```

补充说明：

- **fd 表**：每进程私有。`fd` 只是小整数，表项指向一个“打开文件表条目”。
- **打开文件表条目**：每次 `open()` 产生一个。记录本次打开的状态：
  - 当前偏移：下次读/写的位置
  - 引用计数：多少个 fd 指向它
  - 状态标志：`O_RDONLY`、`O_WRONLY`、`O_APPEND` 等
  - 指向 v-node 的指针
- **v-node 表**：表示文件/设备本身，存元数据，如类型、权限、大小、设备号等。它不记录“读到哪儿了”。

关键规则：

- **两次 `open` 同一文件** → 两个独立的打开文件表条目，各自有独立偏移；但它们指向同一个 v-node，所以共享文件内容，不共享偏移。
- **`fork()` 或 `dup()`** → 多个 fd 指向同一个打开文件表条目，共享偏移和状态；引用计数增加。
- **重定向**：`dup2(oldfd, newfd)` 让 `newfd` 改指向 `oldfd` 的打开文件表条目。例如 `ls > out.txt`，就是让子进程的 `fd=1` 指向 `out.txt`，而 `ls` 仍以为自己写的是标准输出。
- **终端 fd=0/1/2**：通常都指向同一个终端设备，如 `/dev/tty`；但 `0` 是只读，`1/2` 是只写。读写方向不同，所以不会互相覆盖。
- **`O_APPEND`**：每次写之前原子地移到文件末尾，避免两个独立偏移互相覆盖。

一句话：**fd 表决定用哪个编号，打开文件表条目决定“这一次打开”的偏移和状态，v-node 决定“是哪个文件/设备”。**

**进程只拿到一个小整数 fd，内核通过三层结构把它映射到真正的文件/设备，并记录“这一次打开”的读写位置。**

### 2.4 共享、冲突、终端

**“不共享偏移”是什么意思？**  
两个 fd 各自记录自己读到/写到哪里，互不影响；但它们操作的是同一个文件，所以文件内容还是同一份。  
例：两次 `open` 同一文件，`fd1` 读 5 字节后偏移变 5，`fd2` 仍从 0 开始读。

**两个 fd 同时写同一文件冲突了怎么办？**  
内核只提供机制，不负责业务冲突。两个独立 `open` 同时写，可能后写覆盖先写。常用办法：

- 追加日志：`O_APPEND`
- 互斥写入：`flock` / `fcntl` 文件锁
- 指定位置读写，不干扰偏移：`pread` / `pwrite`
- 原子替换整个文件：写临时文件 + `fsync` + `rename`

**终端 fd=0/1/2 指向同一设备，为什么不冲突？**

- `fd=0` 是 `O_RDONLY`，只从输入队列读；`fd=1/2` 是 `O_WRONLY`，只往输出队列写。
- 读和写方向不同，队列分开。
- 内核在系统调用入口检查标志：`write(0, ...)` 或 `read(1, ...)` 会被拒绝。
- 多个写者写终端，输出可能交错，这是允许的；驱动用锁保证队列安全。
- 终端没有普通文件那种“偏移覆盖”问题。

## 第三关：打开、读写、关闭文件

### 3.1 open() — 打开文件

```c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int fd = open(char *filename, int flags, mode_t mode);
// 成功：返回最小的未使用 fd 号
// 失败：返回 -1，并设置 errno
```

**flags 参数**（必须指定其一，可用 | 组合）：

| flags | 含义 |
|-------|------|
| `O_RDONLY` | 只读 |
| `O_WRONLY` | 只写 |
| `O_RDWR` | 读写 |
| `O_CREAT` | 如果文件不存在则创建 |
| `O_TRUNC` | 如果文件存在则清空 |
| `O_APPEND` | 每次写入前移到文件末尾 |

**mode 参数**（只在创建文件时有效，指定权限）：

```c
// 常见组合：
S_IRUSR | S_IWUSR  // 所有者可读可写（rw-------）
0644               // 等同于 rw-r--r--（八进制）
```

**完整示例**：

```c
// 创建或覆盖一个文件用于写入
int fd = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (fd < 0) {
    perror("open");  // perror 会打印 "open: 具体错误信息"
    exit(1);
}
```

### 3.2 read() 和 write() — 读写文件

```c
#include <unistd.h>

ssize_t read(int fd, void *buf, size_t n);
// 返回：实际读到的字节数
//       0 = 到达文件末尾（EOF）
//      -1 = 出错

ssize_t write(int fd, const void *buf, size_t n);
// 返回：实际写出的字节数
//      -1 = 出错
```

**最简单的用法**：

```c
// 读取文件的前100字节
char buf[100];
int fd = open("data.txt", O_RDONLY);
ssize_t n = read(fd, buf, 100);
printf("读到了 %zd 字节\n", n);
close(fd);
```

### 3.3 close() — 关闭文件

```c
int close(int fd);  // 成功返回 0，失败返回 -1
```

**一定要记得关闭文件！** 否则文件描述符泄漏，程序最终会耗尽所有 fd（系统默认每进程最多 1024 个打开文件）。

```bash
# 查看进程当前打开的文件描述符
$ ls -la /proc/$(pgrep your_program)/fd
```

### 3.4 动手：实现最简单的 cp 命令

```c
// mycp.c：复制文件
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFSIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "用法: %s <源文件> <目标文件>\n", argv[0]);
        exit(1);
    }

    int src = open(argv[1], O_RDONLY);
    if (src < 0) { perror("打开源文件失败"); exit(1); }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) { perror("打开目标文件失败"); exit(1); }

    char buf[BUFSIZE];
    ssize_t n;
    while ((n = read(src, buf, BUFSIZE)) > 0) {
        // ⚠️ 注意：这里有个 Bug！下一关会揭示
        write(dst, buf, n);
    }

    close(src);
    close(dst);
    printf("复制完成\n");
    return 0;
}
```

```bash
$ gcc -o mycp mycp.c
$ ./mycp /etc/passwd /tmp/passwd_copy
$ cmp /etc/passwd /tmp/passwd_copy
# 通常能通过，但有时会静默失败！（下一关揭示原因）
```

---

## 第四关：Short Count——初学者最容易踩的坑

### 4.1 什么是 short count？

**`read(fd, buf, 100)` 不保证能读到 100 字节！** 它可能只返回 20、50、1 字节——这叫 **short count（不足值）**。

同样，**`write(fd, buf, 100)` 也不保证能写出 100 字节**。

这不是错误，这是**系统调用的正常行为**！

### 4.2 什么情况会产生 short count？

```
情况1：读到文件末尾（EOF）
  文件剩余 20 字节，你 read(fd, buf, 100)
  → 返回 20

情况2：从终端（键盘）读
  用户输入 "hello\n"（6字节），你 read(fd, buf, 100)
  → 返回 6（遇到换行符就返回）

情况3：从网络 socket 或管道读（最常见的坑！）
  TCP 每次传输多少数据是不确定的
  你 read(socket_fd, buf, 1000)
  → 可能只返回 256（只收到这么多）

情况4：被信号中断（EINTR）
  read 执行期间收到信号
  → 返回 -1，errno = EINTR（需要重试，不是真正的错误）
```

**磁盘文件例外**：从普通磁盘文件读（非 EOF）**不会**产生 short count，但写网络/管道时绝对会！

### 4.3 上一关的 Bug 在哪？

```c
// 有 Bug 的代码：
while ((n = read(src, buf, BUFSIZE)) > 0) {
    write(dst, buf, n);  // ← Bug！没有检查 write 是否写完了！
}
```

问题：如果 `write` 返回的字节数小于 `n`（short count），剩余的字节就**悄悄丢失**了！

对普通磁盘文件，这通常不会发生（但不能依赖"通常"！）。对网络 socket，这**经常发生**。

### 4.4 正确做法：循环直到读完/写完

```c
// 正确的"读满 n 字节"：
ssize_t read_full(int fd, void *buf, size_t n) {
    size_t remaining = n;
    char *ptr = (char *)buf;
    while (remaining > 0) {
        ssize_t got = read(fd, ptr, remaining);
        if (got < 0) {
            if (errno == EINTR) continue;  // 被信号打断，重试
            return -1;                      // 真正的错误
        }
        if (got == 0) break;               // EOF
        ptr += got;
        remaining -= got;
    }
    return n - remaining;  // 实际读到的字节数
}

// 正确的"写满 n 字节"：
ssize_t write_full(int fd, const void *buf, size_t n) {
    size_t remaining = n;
    const char *ptr = (const char *)buf;
    while (remaining > 0) {
        ssize_t wrote = write(fd, ptr, remaining);
        if (wrote < 0) {
            if (errno == EINTR) continue;  // 重试
            return -1;
        }
        ptr += wrote;
        remaining -= wrote;
    }
    return n;  // 必然写完了 n 字节（或出错返回-1）
}
```

---

## 第五关：RIO——CSAPP 提供的健壮 I/O 库

### 5.1 什么是 RIO？

CSAPP 教材提供了一套封装好的 I/O 函数（Robust I/O），**自动处理 short count 和 EINTR**，让你专注于业务逻辑。

```c
// 使用前包含：
#include "csapp.h"
```

### 5.2 两套函数

**套装一：无缓冲（适合二进制数据、网络传输）**

```c
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
// 内部循环，直到读满 n 字节 或 EOF 或 出错
// 返回：实际读到的字节数（EOF时可能<n）

ssize_t rio_writen(int fd, void *usrbuf, size_t n);
// 内部循环，保证写出 n 字节（除非出错）
// 返回：n（成功），-1（出错）
```

**套装二：带缓冲（适合文本行读取，如 HTTP 请求）**

```c
// 第一步：初始化缓冲区结构（每个 fd 一个）
void rio_readinitb(rio_t *rp, int fd);

// 读一行（遇到'\n'或maxlen-1时停止，结果包含'\n'，末尾加'\0'）
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

// 读固定字节数（带缓冲版本，适合混合行读和块读）
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
```

### 5.3 rio_t 缓冲区是怎么工作的？

带缓冲版本在用户空间维护一个 8KB 的内部缓冲区，**批量读取，按需返回**：

```
用户调用 rio_readlineb（想要一行文本）

缓冲区内有数据？
├── 是 → 直接从缓冲区复制，不调用 read()
└── 否 → 一次性 read() 读入 8KB
         从 8KB 中取出需要的内容
         剩余的留在缓冲区，下次用

效果：一次 read() 系统调用，可以服务多次 rio_readlineb() 调用
     减少系统调用次数，提升性能
```

### 5.4 完整示例：用 RIO 读取文件的每一行

```c
// read_lines.c
#include "csapp.h"

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "用法: %s <文件>\n", argv[0]); exit(1); }

    int fd = Open(argv[1], O_RDONLY, 0);  // Open = open + 出错自动退出（csapp.h 包装版）

    rio_t rio;
    rio_readinitb(&rio, fd);  // 初始化缓冲区

    char buf[MAXLINE];
    int line_num = 1;
    ssize_t n;

    while ((n = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        printf("%4d: %s", line_num++, buf);  // buf 末尾已有 '\n'
    }

    Close(fd);
    return 0;
}
```

```bash
$ gcc -o read_lines read_lines.c csapp.c -lpthread
$ ./read_lines /etc/passwd
   1: root:x:0:0:root:/root:/bin/bash
   2: daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin
   ...
```

---

## 第六关：文件元数据——stat() 的用法

### 6.1 如何获取文件信息？

```c
#include <sys/stat.h>

int stat(const char *filename, struct stat *buf);   // 用路径
int fstat(int fd, struct stat *buf);                 // 用已打开的 fd
// 成功返回 0，失败返回 -1
```

### 6.2 struct stat 的重要字段

```c
struct stat {
    mode_t  st_mode;   // 文件类型和权限（下面详解）
    off_t   st_size;   // 文件大小（字节）
    time_t  st_mtime;  // 最后修改时间
    nlink_t st_nlink;  // 硬链接数
    uid_t   st_uid;    // 所有者 ID
    gid_t   st_gid;    // 所属组 ID
    // ...
};
```

### 6.3 判断文件类型

```c
struct stat st;
stat("somefile", &st);

if (S_ISREG(st.st_mode))  printf("普通文件\n");
if (S_ISDIR(st.st_mode))  printf("目录\n");
if (S_ISSOCK(st.st_mode)) printf("套接字\n");
if (S_ISLNK(st.st_mode))  printf("符号链接\n");
```

### 6.4 完整示例：实现简化版 `ls -l`

```c
// myls.c
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

void print_file_info(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        perror(path);
        return;
    }

    // 文件类型
    char type = S_ISREG(st.st_mode) ? '-' :
                S_ISDIR(st.st_mode) ? 'd' :
                S_ISLNK(st.st_mode) ? 'l' : '?';

    // 权限位
    char perm[10];
    snprintf(perm, sizeof(perm), "%c%c%c%c%c%c%c%c%c",
        (st.st_mode & S_IRUSR) ? 'r' : '-',
        (st.st_mode & S_IWUSR) ? 'w' : '-',
        (st.st_mode & S_IXUSR) ? 'x' : '-',
        (st.st_mode & S_IRGRP) ? 'r' : '-',
        (st.st_mode & S_IWGRP) ? 'w' : '-',
        (st.st_mode & S_IXGRP) ? 'x' : '-',
        (st.st_mode & S_IROTH) ? 'r' : '-',
        (st.st_mode & S_IWOTH) ? 'w' : '-',
        (st.st_mode & S_IXOTH) ? 'x' : '-');

    // 修改时间
    char timebuf[20];
    struct tm *t = localtime(&st.st_mtime);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", t);

    printf("%c%s %3lu %8ld %s %s\n",
           type, perm, st.st_nlink, (long)st.st_size, timebuf, path);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++)
        print_file_info(argv[i]);
    return 0;
}
```

```bash
$ gcc -o myls myls.c
$ ./myls /etc/passwd /tmp /usr/bin/ls
-rw-r--r--   1     2821 2024-01-15 09:23 /etc/passwd
drwxrwxrwt 124     4096 2024-01-20 14:30 /tmp
-rwxr-xr-x   1   147984 2023-11-12 03:00 /usr/bin/ls
```

---

## 第七关：文件共享与 fork 后的 I/O

### 7.1 fork 之后，父子进程共享文件偏移！

这是很多初学者忽略的重要细节：

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    // 创建测试文件
    int fd = open("test.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    write(fd, "ABCDEFGHIJ", 10);  // 写入10字节
    lseek(fd, 0, SEEK_SET);        // 回到开头

    pid_t pid = fork();

    if (pid == 0) {
        // 子进程：读2字节
        char buf[3] = {0};
        read(fd, buf, 2);          // 读到 "AB"，偏移变为2
        printf("子进程读到: %s\n", buf);
        close(fd);
        exit(0);
    } else {
        // 父进程：等子进程读完再读
        wait(NULL);
        char buf[3] = {0};
        read(fd, buf, 2);          // ！！！偏移已经是2了，读到 "CD"
        printf("父进程读到: %s\n", buf);
        close(fd);
    }
    return 0;
}
```

```bash
$ gcc -o fork_io fork_io.c && ./fork_io
子进程读到: AB
父进程读到: CD    ← 父进程没有从头开始读！因为共享偏移
```

**图解**：

```
fork() 之后：
父进程的 fd=3 ──┐
                ├──→ 打开文件表条目 [偏移=0, 引用=2] → hello.txt
子进程的 fd=3 ──┘

引用=2 表示有两个fd指向这个条目
子进程 read 2字节后，这个共享的偏移变为 2
父进程再 read 时，从偏移=2 开始读
```

### 7.2 什么时候不共享偏移？

如果父进程和子进程都**各自 `open`** 同一文件，那么它们有独立的打开文件表条目，偏移互不影响：

```c
// 各自打开 = 不共享偏移
int fd1 = open("data.txt", O_RDONLY);  // 父进程打开
fork();
int fd2 = open("data.txt", O_RDONLY);  // fork后再开（不管父子各自open）
// fd1 和 fd2 各有独立偏移
```

---

## 第八关：I/O 重定向——dup2 是怎么工作的

### 8.1 shell 里的 `>` 是怎么实现的？

当你在 shell 中输入：
```bash
$ ls > output.txt
```

shell 不是在 `ls` 内部修改任何东西，而是通过 `fork+dup2+exec` 的三步舞：

```c
// shell 执行 "ls > output.txt" 的伪代码：
pid_t pid = fork();

if (pid == 0) {
    // 子进程（将要变成 ls）
    int fd = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // dup2：让 fd=1（标准输出）指向 output.txt
    dup2(fd, STDOUT_FILENO);  // 1 现在指向 output.txt
    close(fd);                 // 关闭多余的 fd（fd=1 已经指向文件了）

    execve("/bin/ls", argv, envp);  // ls 写 stdout（fd=1），实际写到文件！
} else {
    // 父进程等待子进程结束
    wait(NULL);
}
```

### 8.2 dup2 的工作原理

```c
#include <unistd.h>
int dup2(int oldfd, int newfd);
// 让 newfd 指向 oldfd 所指向的打开文件表条目
// 如果 newfd 已经打开，先关闭它
// 返回：成功返回 newfd，失败返回 -1
```

**图解 dup2(fd, 1)**：

```
dup2(fd, STDOUT_FILENO) 执行过程：

执行前：
fd=1 ──→ 终端（stdout）
fd=3 ──→ output.txt

执行后（dup2(3, 1)）：
fd=1 ──→ output.txt    ← fd=1 现在指向文件！
fd=3 ──→ output.txt    ← fd=3 也指向文件（之后需要 close(3)）

close(3) 之后：
fd=1 ──→ output.txt    ← 干净！只有 fd=1 指向文件
```

### 8.3 动手：自己实现一个带重定向的 mini-shell 片段

```c
// redirect_demo.c：演示将 ls 的输出重定向到文件
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    printf("执行前，终端输出：这是父进程的正常输出\n");

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }

    if (pid == 0) {
        // === 子进程：重定向并执行 ===
        int fd = open("ls_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open"); exit(1); }

        // 让 stdout 指向文件
        dup2(fd, STDOUT_FILENO);
        close(fd);  // 关闭多余的 fd

        // execve 后，子进程变成 ls
        // ls 写 stdout（现在是文件）
        char *argv[] = {"/bin/ls", "-la", NULL};
        execve("/bin/ls", argv, NULL);
        perror("execve");  // 如果 execve 成功，这行永远不会执行
        exit(1);
    }

    // === 父进程：等待并查看结果 ===
    wait(NULL);
    printf("子进程完成，ls 的输出已写入 ls_output.txt\n");

    // 读取并显示文件内容
    int fd = open("ls_output.txt", O_RDONLY);
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    printf("=== ls_output.txt 内容 ===\n%s", buf);
    close(fd);
    return 0;
}
```

```bash
$ gcc -o redirect_demo redirect_demo.c && ./redirect_demo
执行前，终端输出：这是父进程的正常输出
子进程完成，ls 的输出已写入 ls_output.txt
=== ls_output.txt 内容 ===
total 32
drwxr-xr-x 2 user user 4096 Jan 20 15:30 .
...
```

---

## 第九关：标准 I/O 库 vs Unix I/O——什么时候用哪个？

### 9.1 标准 I/O（stdio）的优缺点

**stdio 做了什么**：在 Unix I/O 之上加了一层**用户空间缓冲**，把每次 I/O 的系统调用次数减少。

```
你调用 fputc(c, f) 100次，stdio实际只调用 write() 1次（等缓冲满或fflush）
你调用 fgetc(f) 100次，stdio实际只调用 read() 1-2次（一次读大块，按需返回）
```

**stdio 的三种缓冲模式**：

| 模式 | 何时刷新缓冲区 | 适用场景 |
|------|--------------|---------|
| 全缓冲（Fully Buffered） | 缓冲区满 或 `fflush` | 普通磁盘文件 |
| 行缓冲（Line Buffered） | 遇到 `\n` 或缓冲区满 | 连接终端的 stdout |
| 无缓冲（Unbuffered） | 立即输出 | stderr |

### 9.2 为什么网络编程不能用 stdio？

**问题场景**：用 `FILE*` 包装一个网络 socket fd，然后 `fputs` 发送 HTTP 请求：

```c
// ⚠️ 危险！不要这样做：
FILE *f = fdopen(socket_fd, "r+");
fputs("GET / HTTP/1.1\r\n\r\n", f);  // 数据可能留在 stdio 缓冲区，没发出去！
fgets(buf, MAX, f);                   // 可能死等，因为请求根本没发
```

**根本原因**：
1. `fputs` 写的数据先进 stdio 缓冲区，可能没有 `fflush` 就没发出去
2. 如果你想用同一个 socket 既读又写，`fdopen` 必须开两个 `FILE*`（一读一写），它们的缓冲区互不感知，容易导致顺序混乱

**正确做法**：网络编程用 RIO 或直接用 Unix I/O（`read`/`write`）。

### 9.3 I/O 选择决策树

```
你的 I/O 目标是什么？
│
├── 普通磁盘文件，需要格式化输出（printf风格）
│   → 用 stdio（fopen/fprintf/fgets/fclose）
│
├── 网络 socket 或管道
│   → 用 RIO（rio_readn/rio_writen/rio_readlineb）
│   → 或者直接用 Unix I/O + 手动处理 short count
│
├── 在信号处理函数中写日志
│   → 必须用 Unix I/O 的 write()
│   → （stdio 的 printf 不是异步信号安全的！）
│
└── 需要精确控制文件偏移（如数据库）
    → 用 Unix I/O 的 lseek() + read()/write()
```

### 9.4 stdio、RIO、Unix I/O 三者关系

层次：

```text
应用程序
  ├── stdio：fopen/fread/fwrite/fprintf/fgets/fclose
  │     有 FILE*、用户态缓冲、格式化；底层调用 open/read/write/close/lseek
  ├── RIO：rio_readn/rio_writen/rio_readlineb/rio_readnb
  │     CSAPP 教学库；处理 short count 和 EINTR；底层调用 read/write
  └── Unix I/O：open/read/write/close/lseek/stat/dup2
        系统调用层，直接进内核
```

区别与选择：

| | Unix I/O | stdio | RIO |
|---|---|---|---|
| 本质 | 系统调用 | C 标准库 | CSAPP 教学库 |
| 缓冲 | 无用户缓冲 | 有用户缓冲 | 无缓冲版无；带缓冲版有 |
| 格式化 | 无 | 有 | 无 |
| short count | 自己循环 | 内部处理一部分 | 自动处理 |
| 典型场景 | 系统编程、信号处理、精确偏移 | 普通文件、终端格式化输出 | socket、管道、文本行 |

一句话：**Unix I/O 是底座；stdio 和 RIO 都是库层，最终靠 `read/write` 搬数据。stdio 更重，带格式化和标准缓冲；RIO 更轻，专注健壮读写。**

---

## 综合练习：实现一个完整的文件复制程序

```c
// robust_cp.c：健壮的文件复制，处理所有 edge case
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// 健壮的写：保证写出 n 字节
ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *ptr = buf;
    size_t remaining = n;
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;  // 被信号打断，重试
            return -1;
        }
        ptr += written;
        remaining -= written;
    }
    return (ssize_t)n;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "用法: %s <源文件> <目标文件>\n", argv[0]);
        exit(1);
    }

    // 打开源文件
    int src = open(argv[1], O_RDONLY);
    if (src < 0) { perror("打开源文件"); exit(1); }

    // 获取源文件大小（用于验证）
    struct stat st;
    fstat(src, &st);
    off_t expected_size = st.st_size;

    // 打开/创建目标文件
    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (dst < 0) { perror("打开目标文件"); close(src); exit(1); }

    // 复制数据
    char buf[65536];
    ssize_t n;
    off_t total_copied = 0;

    while ((n = read(src, buf, sizeof(buf))) > 0) {
        if (write_all(dst, buf, n) < 0) {
            perror("写入失败");
            close(src); close(dst);
            exit(1);
        }
        total_copied += n;
    }

    if (n < 0) { perror("读取失败"); exit(1); }

    // 验证
    printf("复制完成：%lld 字节（预期 %lld 字节）%s\n",
           (long long)total_copied, (long long)expected_size,
           total_copied == expected_size ? "✓" : "✗ 大小不匹配！");

    close(src);
    close(dst);
    return total_copied == expected_size ? 0 : 1;
}
```

```bash
$ gcc -o robust_cp robust_cp.c
$ ./robust_cp /bin/ls /tmp/ls_copy
复制完成：147984 字节（预期 147984 字节）✓
$ cmp /bin/ls /tmp/ls_copy && echo "二进制完全一致"
二进制完全一致
```

---

## 自测：你掌握了吗？

**第1关**：程序启动时，fd=0、1、2 分别连接到什么？
<details><summary>答案</summary>fd=0 标准输入（键盘），fd=1 标准输出（屏幕），fd=2 标准错误（屏幕）。新打开的文件从 fd=3 开始。</details>

**第2关**：`read(fd, buf, 1000)` 返回 300，这是错误吗？
<details><summary>答案</summary>不是错误，是 short count。对于网络socket、管道、终端这完全正常。需要循环调用直到读满，或使用 rio_readn。</details>

**第3关**：`dup2(5, 1)` 执行后，写 `stdout`（fd=1）的数据会去哪里？
<details><summary>答案</summary>会写入 fd=5 原来指向的文件/设备。dup2 让 fd=1 指向了 fd=5 所指向的打开文件表条目。</details>

**第4关**：为什么网络编程中不推荐用 `fprintf` 直接向 socket 写数据？
<details><summary>答案</summary>stdio 有用户空间缓冲区，fprintf 写的数据可能暂存在缓冲区中，没有立即发出；而且对同一 socket 用 FILE* 同时读写会因两个缓冲区的状态不同步导致顺序错乱。</details>

**第5关**：`fork()` 后，父子进程共享同一个打开文件表条目，这有什么实际影响？
<details><summary>答案</summary>父子进程共享文件偏移。一方 read/write 推进偏移后，另一方再读写会从新偏移处继续，不会从头开始。这是 shell 管道实现的基础，但也是容易 bug 的地方。</details>

**补充自测**：

**第6关**：打开文件表条目里通常有哪些字段？
<details><summary>答案</summary>偏移、引用计数、状态标志、读写模式、指向 v-node 的指针、文件操作函数等。</details>

**第7关**：两个独立 `open` 同一文件，同时写会怎样？
<details><summary>答案</summary>各自偏移独立，可能互相覆盖。要避免就用 `O_APPEND`、文件锁或 `pwrite`。</details>

**第8关**：`fd=0/1/2` 指向同一终端，为什么不会互相覆盖？
<details><summary>答案</summary>读/写方向不同，输入输出队列分开；权限标志在内核入口限制操作方向；终端没有普通文件的偏移覆盖问题。</details>

---

## 3小时学习路径

| 时间 | 任务 |
|------|------|
| 0–20 分钟 | 读第一、二关，理解"一切皆文件"和文件描述符的概念 |
| 20–50 分钟 | 读第三关，编译运行 `mycp.c`，用 cmp 验证 |
| 50–80 分钟 | 读第四关，理解 short count，改造 `mycp.c` 加入 `write_all` |
| 80–110 分钟 | 读第五关，用 RIO 实现 `read_lines.c`，运行测试 |
| 110–140 分钟 | 读第六~七关，运行 `fork_io.c`，观察共享偏移现象 |
| 140–170 分钟 | 读第八关，编译运行 `redirect_demo.c`，追踪 dup2 过程 |
| 170–180 分钟 | 完成综合练习 `robust_cp.c`，完成所有自测题 |