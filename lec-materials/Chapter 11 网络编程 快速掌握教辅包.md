# 第11章 网络编程——从零开始手把手学

> **适合人群**：会写 C 程序，但从没自己建过网络连接的同学
> **学完你能做到**：独立写出能跑通的客户端和服务器，正确处理 TCP 粘包，把顺序服务器升级为并发版
> **预计时间**：认真读 + 动手跑每段代码，约 3.5 小时

---

## 第一关：先动手感受一次——你已经在用网络编程的成果了

### 1.1 一行命令，看清楚"网络通信"到底传了什么

打开终端（Linux / macOS / WSL），执行：

```bash
$ printf "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n" | nc example.com 80
```

你会看到：

```
HTTP/1.0 200 OK
Content-Type: text/html; charset=UTF-8
...
<html>
<head><title>Example Domain</title></head>
<body>...
</html>
```

**刚才发生了什么**：`nc`（netcat）帮你连接到 `example.com` 的 80 端口，把那段纯文本发了过去，服务器回来了一个网页。

**你用 C 代码做的，就是这同一件事**——要么扮演客户端（发请求），要么扮演服务器（接请求、返响应）。本章教你把每一步都自己实现。

### 1.2 三个基本问题

| 问题 | 答案 | 本关对应 |
|------|------|---------|
| 怎么找到对方？ | IP 地址 + 端口号 | 第二关 |
| 怎么传数据？ | TCP（可靠字节流） | 第三关 API |
| 传什么格式？ | 应用层自定义（HTTP / 自己的协议）| 第七、九关 |

---

## 第二关：找到对方——IP 地址、端口、字节序

### 2.1 IP 地址是门牌，端口是房间号

互联网上每台机器有一个 IP 地址（门牌），机器上每个服务用端口号（房间号）区分：

```
一台服务器（IP: 93.184.216.34）
┌─────────────────────────────────┐
│  端口 22  → SSH 服务            │
│  端口 80  → HTTP 服务           │
│  端口 443 → HTTPS 服务          │
│  端口 8888 → 你自己写的程序  ←  │
└─────────────────────────────────┘
```

数据包到达这台机器后，靠**端口号**决定交给哪个程序处理。

**常用端口**（记住就够用）：

| 端口 | 服务 | 端口 | 服务 |
|------|------|------|------|
| 22 | SSH | 80 | HTTP |
| 443 | HTTPS | 3306 | MySQL |
| 6379 | Redis | 5432 | PostgreSQL |

端口 0–1023 需要 root 权限绑定；自己的程序用 8000–9999 这段安全。

### 2.2 字节序——一个必须踩的坑

端口号 8888 是 16 位整数 `0x22B8`。发到网络上时，先发 `0x22` 还是先发 `0xB8`？

```
同一个数字 0x22B8 在两种 CPU 上的内存布局：

小端（Little-Endian）—— x86/x64 默认：
地址:  1000   1001
数据:  0xB8   0x22    ← 低字节在低地址

大端（Big-Endian）—— 网络字节序用大端：
地址:  1000   1001
数据:  0x22   0xB8    ← 高字节在低地址
```

**网络协议规定用大端（网络字节序）**。你在 x86 上编程，放进 `sockaddr` 之前**必须转换**：

```c
#include <arpa/inet.h>

uint16_t htons(uint16_t x);   // host → network, short（端口号用这个）
uint32_t htonl(uint32_t x);   // host → network, long （IP 地址用这个）
uint16_t ntohs(uint16_t x);   // network → host, short
uint32_t ntohl(uint32_t x);   // network → host, long
```

**记忆方法**：`h` = host，`n` = network，`s` = short（16 位），`l` = long（32 位）。

### 2.3 IP 地址和字符串互转

```c
#include <arpa/inet.h>

// "192.168.1.100"（字符串）→ 32 位整数（网络字节序）
struct in_addr addr;
inet_pton(AF_INET, "192.168.1.100", &addr);   // pton = presentation to numeric

// 32 位整数 → "192.168.1.100"（字符串）
char buf[INET_ADDRSTRLEN];                    // 固定长度 16
inet_ntop(AF_INET, &addr, buf, sizeof(buf));  // ntop = numeric to presentation
printf("%s\n", buf);  // 输出 "192.168.1.100"
```

### 2.4 getaddrinfo——把域名查成 IP

不用记 `93.184.216.34`，告诉 DNS `example.com`，它帮你查：

```c
#include <netdb.h>

struct addrinfo hints = {0};
hints.ai_family   = AF_INET;      // 只要 IPv4
hints.ai_socktype = SOCK_STREAM;  // TCP

struct addrinfo *result;
int err = getaddrinfo("example.com", "80", &hints, &result);
if (err != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
    exit(1);
}
// result 是链表，result->ai_addr 可以直接传给 connect()
freeaddrinfo(result);   // 用完必须释放！
```

> **为什么不用老的 `gethostbyname`？**  
> 它不支持 IPv6，而且是线程不安全的（第12章会解释这意味着什么）。现代代码一律用 `getaddrinfo`。

---

## 第三关：Socket API——建立连接的流程

### 3.1 先看全景图，再逐个学 API

```
服务器端                                     客户端
───────────────────────────────────────────────────────
socket()   ← 创建套接字                     socket()
bind()     ← 绑定端口和地址
listen()   ← 开始监听，等待连接
                                            connect() → TCP三次握手
accept()   ← 阻塞等待，返回 connfd  ←─────────
（每个客户端一个 connfd）

write(connfd) ───────────────────────────→ read(sockfd)
read(connfd)  ←─────────────────────────  write(sockfd)

close(connfd)                               close(sockfd)
↑ 循环回 accept，等下一个客户端
```

### 3.2 关键区别：`listenfd` vs `connfd`（最容易混淆的点）

```
            服务器进程
  ┌──────────────────────────────────────────┐
  │                                          │
  │  listenfd = 3  ← 监听套接字              │
  │  全程只有一个    只负责调用 accept()       │
  │                                          │
  │  connfd = 4  ← 已连接套接字（客户端A）    │
  │  connfd = 5  ← 已连接套接字（客户端B）    │
  │  每个客户端一个   用来 read/write 通信     │
  └──────────────────────────────────────────┘
```

**类比**：`listenfd` 是餐厅前台（专门迎客），`connfd` 是专属服务员（专门服务某位客人）。

### 3.3 逐个 API

```c
// ① 创建套接字——得到一个文件描述符，还没绑定任何地址
int sockfd = socket(AF_INET,      // IPv4
                    SOCK_STREAM,  // TCP（可靠字节流）
                    0);           // 协议，0 = 自动选
// 失败返回 -1

// ② 服务器：绑定地址和端口（告诉内核：这个 sockfd 监听 8888 端口）
struct sockaddr_in addr = {0};
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 监听所有网卡
addr.sin_port        = htons(8888);         // 注意：必须转字节序！
bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));

// ③ 服务器：开始监听，设置等待队列长度
listen(sockfd, 128);  // 128 = 还没 accept 时最多积压 128 个连接

// ④ 服务器：阻塞直到有客户端来，返回新的 connfd
struct sockaddr_in client_addr;
socklen_t addrlen = sizeof(client_addr);
int connfd = accept(sockfd,
                    (struct sockaddr *)&client_addr,  // 输出：客户端地址
                    &addrlen);
// read/write 必须用 connfd，不能用 sockfd！

// ⑤ 客户端：连接服务器（不需要 bind，OS 自动分配临时端口）
connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
// connect 成功后，sockfd 就可以 read/write 了
```

---

## 第四关：第一个能跑的程序——最小版本

先不做 echo，只做**一件事**：服务器向客户端发 `"Hello!\n"`，客户端打印出来。这是最短的能跑的网络程序。

### 4.1 最小服务器

```c
// min_server.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    // 端口重用——防止重启时报 "Address already in use"
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(8888);

    bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listenfd, 10);
    printf("[服务器] 等待连接...\n");

    int connfd = accept(listenfd, NULL, NULL);   // 接受一个连接
    write(connfd, "Hello from server!\n", 19);   // 发送消息
    close(connfd);
    close(listenfd);
    return 0;
}
```

### 4.2 最小客户端

```c
// min_client.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);  // 连本机

    connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));

    char buf[64] = {0};
    int n = read(sockfd, buf, sizeof(buf) - 1);
    printf("[客户端] 收到 %d 字节：%s", n, buf);

    close(sockfd);
    return 0;
}
```

### 4.3 运行

```bash
# 终端1：先启动服务器
$ gcc -o min_server min_server.c && ./min_server
[服务器] 等待连接...

# 终端2：再运行客户端
$ gcc -o min_client min_client.c && ./min_client
[客户端] 收到 19 字节：Hello from server!
```

**遇到 "Connection refused"**：服务器还没启动，或端口号不一致——先确认服务器跑起来，再运行客户端。

**遇到 "Address already in use"**：加上 `SO_REUSEADDR`（代码里已经有），或换个端口号，或等几秒。

> **恭喜！** 人生第一个网络程序跑通了。接下来让它双向通信——客户端发什么，服务器原样返回。

---

## 第五关：write 不一定写完——short write 的坑

### 5.1 先看这段"有 Bug 的 cp"

```c
// 有 Bug 的代码（类比第10章的 mycp.c）：
while ((n = read(src_fd, buf, BUFSIZE)) > 0) {
    write(dst_fd, buf, n);   // ← Bug！没检查是否写完了！
}
```

对普通磁盘文件，这通常没问题。但对网络 socket：

```
write(sockfd, buf, 1000) 可能只返回 256！
原因：
  - 网络发送缓冲区暂时满了
  - 被信号打断（errno == EINTR）
```

剩余的 744 字节就**悄悄丢失**了，对方收到的数据不完整，而程序没有报任何错误。

### 5.2 正确做法：循环直到写完

```c
// 保证写出 n 字节
ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;  // 信号打断，重试
            return -1;
        }
        p += w;
        left -= w;
    }
    return (ssize_t)n;
}
```

`read` 也可能 short count（从 socket 读时，数据还没全到就先返回了）。**规则：套接字上的写操作一律用 `write_all`，需要读固定字节数时也用类似的循环**。

---

## 第六关：完整 Echo 服务器——把所有积木拼起来

### 6.1 服务器

```c
// echo_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    8888
#define BUFSIZE 4096

ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf; size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; left -= w;
    }
    return (ssize_t)n;
}

// 处理单个客户端：读到什么就原样返回，直到对方关闭连接
void handle_client(int connfd) {
    char buf[BUFSIZE];
    ssize_t n;
    while ((n = read(connfd, buf, sizeof(buf))) > 0) {
        printf("[服务器] 收到 %zd 字节，回显\n", n);
        write_all(connfd, buf, n);
    }
    if (n == 0) printf("[服务器] 客户端主动关闭\n");
    else        perror("[服务器] read 出错");
}

int main() {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);
    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    listen(listenfd, 128);
    printf("[服务器] 启动，监听端口 %d\n", PORT);

    while (1) {
        struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
        int connfd = accept(listenfd, (struct sockaddr *)&caddr, &clen);
        if (connfd < 0) { perror("accept"); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
        printf("[服务器] 新连接：%s:%d\n", ip, ntohs(caddr.sin_port));

        handle_client(connfd);
        close(connfd);   // 关闭这个连接的 fd，继续等下一个
    }
}
```

### 6.2 客户端

```c
// echo_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    8888
#define BUFSIZE 4096

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); exit(1);
    }
    printf("[客户端] 已连接，输入消息（Ctrl+D 退出）：\n");

    char buf[BUFSIZE];
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        size_t len = strlen(buf);
        write(sockfd, buf, len);

        char rbuf[BUFSIZE] = {0};
        ssize_t n = read(sockfd, rbuf, sizeof(rbuf) - 1);
        if (n > 0) printf("[回显] %.*s", (int)n, rbuf);
    }
    close(sockfd);
    return 0;
}
```

### 6.3 运行效果

```bash
# 终端1：
$ gcc -o echo_server echo_server.c && ./echo_server
[服务器] 启动，监听端口 8888

# 终端2：
$ gcc -o echo_client echo_client.c && ./echo_client
[客户端] 已连接，输入消息（Ctrl+D 退出）：
Hello CSAPP
[回显] Hello CSAPP
^D

# 终端1 同步显示：
[服务器] 新连接：127.0.0.1:54231
[服务器] 收到 11 字节，回显
[服务器] 客户端主动关闭
```

---

## 第七关：TCP 粘包——先亲眼看到问题，再学解法

### 7.1 先猜答案

看这段发送代码：

```c
write(sockfd, "Hello", 5);
write(sockfd, "World", 5);
```

**你觉得对方用 `read()` 接收，一定会收到两条消息 "Hello" 和 "World" 吗？**

<details>
<summary>先猜一下，再点开答案</summary>

不一定！可能收到的是：
- `"HelloWorld"`（两条粘在一起）
- `"Hel"` + `"loWorld"`（从中间截断）
- `"Hello"` + `"World"`（恰好对齐——但不能依赖这种运气）

**根本原因：TCP 是字节流（Byte Stream），不是消息流（Message Stream）。**

</details>

### 7.2 为什么会这样？

TCP 的承诺：你写的每个字节，对方都能按顺序收到。  
TCP **没有**承诺：你 `write` 几次，对方就 `read` 几次。

```
你的视角：                TCP 看到的：
write("Hello")    →
write("World")    →      H e l l o W o r l d    （10 个字节的流）

对方 read 时：可以在任意位置截断这 10 个字节
```

### 7.3 解决：在应用层定义消息边界

**方案一：行协议**（每条消息以 `\n` 结尾，HTTP 1.x / SMTP / Redis 命令都用这个）

```c
// 发：每条消息加 \n
write_all(fd, "Hello\n", 6);
write_all(fd, "World\n", 6);

// 收：读到 \n 为止
// 需要自己实现 readline，或用 CSAPP 的 rio_readlineb
```

缺点：消息内容里不能含 `\n`（除非做转义处理）。

---

**方案二：定长消息**（每条固定 N 字节，不足补 `\0`）

```c
#define MSG_SIZE 64
char msg[MSG_SIZE] = {0};
strncpy(msg, "Hello", 5);
write_all(fd, msg, MSG_SIZE);   // 总是发 64 字节

// 接收：
char msg[MSG_SIZE];
read_all(fd, msg, MSG_SIZE);    // 总是读 64 字节
```

适合结构体数据（如传感器读数、游戏指令）。

---

**方案三：长度前缀**（先发 4 字节消息长度，再发消息体，HTTP/2、gRPC、MySQL 都用）

```c
// 发：先发 4 字节长度（网络字节序！），再发内容
void send_msg(int fd, const char *data, uint32_t len) {
    uint32_t len_net = htonl(len);    // 长度也要转字节序
    write_all(fd, &len_net, 4);
    write_all(fd, data, len);
}

// 收：先读 4 字节长度，再读对应字节数的内容
char *recv_msg(int fd, uint32_t *out_len) {
    uint32_t len_net;
    if (read_all(fd, &len_net, 4) != 4) return NULL;
    uint32_t len = ntohl(len_net);

    char *buf = malloc(len + 1);
    if (read_all(fd, buf, len) != (ssize_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    *out_len = len;
    return buf;    // 调用方负责 free
}
```

**推荐用长度前缀**：消息内容可以包含任意字节（`\0`、`\n` 都没问题），最为通用。

---

## 第八关：顺序服务器的致命缺陷和三种解法

### 8.1 演示：一个慢客户端拖垮整个服务器

第六关的服务器是**顺序**的：`handle_client` 处理完 A，才能处理 B。

```
时间轴：
t=0  accept() → connfd_A，开始 handle_client(A)...
t=1  客户端 B 连接，进入等待队列
t=2  A 在慢慢打字，服务器卡在 read(connfd_A) 这里
t=5  A 终于断开
t=5  accept() → connfd_B（B 已经等了 5 秒！）
```

如果 A 永远不断开，B 永远无法被服务。

### 8.2 解法一：多进程（最简单，最安全）

```c
while (1) {
    struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
    int connfd = accept(listenfd, (struct sockaddr *)&caddr, &clen);

    pid_t pid = fork();
    if (pid == 0) {
        close(listenfd);       // 子进程不需要监听 fd
        handle_client(connfd);
        close(connfd);
        exit(0);
    }
    close(connfd);   // 父进程关闭 connfd（子进程还持有，连接不会断）
    // 父进程立即回到 accept()，不等子进程
}
signal(SIGCHLD, SIG_IGN);   // 忽略子进程退出信号，防止僵尸进程
```

**优点**：简单；子进程崩溃不影响其他客户端（地址空间隔离）。  
**缺点**：`fork()` 开销大，1000 并发 = 1000 个进程，内存压力大。

### 8.3 解法二：多线程（最常用）

```c
#include <pthread.h>

typedef struct {
    int connfd;
    struct sockaddr_in addr;
} client_t;

void *thread_func(void *arg) {
    client_t *ca = (client_t *)arg;
    pthread_detach(pthread_self());  // 线程结束后自动回收，不需要 join

    handle_client(ca->connfd);
    close(ca->connfd);
    free(ca);        // 释放主线程 malloc 的参数
    return NULL;
}

while (1) {
    client_t *ca = malloc(sizeof(client_t));   // ← 每次必须 malloc！
    socklen_t len = sizeof(ca->addr);
    ca->connfd = accept(listenfd, (struct sockaddr *)&ca->addr, &len);

    pthread_t tid;
    pthread_create(&tid, NULL, thread_func, ca);
    // 主线程立即回到 accept()
}
```

**为什么必须 `malloc`，不能用局部变量？**

```c
// ❌ 危险：传局部变量地址
client_t ca;
ca.connfd = accept(...);
pthread_create(&tid, NULL, thread_func, &ca);
// 下次循环主线程会修改 ca！
// 线程还没来得及读，就被覆盖了（竞态条件）

// ✅ 正确：每次 malloc 独立的堆空间
client_t *ca = malloc(sizeof(client_t));
ca->connfd = accept(...);
pthread_create(&tid, NULL, thread_func, ca);
```

**优点**：比进程轻量，创建快，共享地址空间方便。  
**缺点**：共享状态需要同步（第12章的主题）。

### 8.4 解法三：I/O 多路复用（了解概念）

用 `select`/`epoll` 让**单线程**同时监控多个 fd——哪个就绪处理哪个：

```c
fd_set read_fds;
FD_ZERO(&read_fds);
FD_SET(listenfd, &read_fds);
// 把所有 connfd 也加进去...
select(maxfd + 1, &read_fds, NULL, NULL, NULL);
// 返回后看哪些 fd 可读
```

这是 Nginx、Node.js 的基础。代码复杂，适合追求极高并发时使用，先把多线程用熟再考虑。

---

## 第九关：HTTP——用 30 行实现一个网页服务器

### 9.1 HTTP 就是有格式的纯文本

回到第一关的 `nc` 命令，现在你能完全解读它：

```
客户端发送的请求（字节流，到 \r\n\r\n 为止是头部）：
───────────────────────────────────────────────────
GET /index.html HTTP/1.0\r\n       ← 请求行：方法 路径 版本
Host: localhost:8888\r\n           ← 请求头（可有多个）
\r\n                               ← 空行，标志头部结束


服务器返回的响应：
───────────────────────────────────────────────────
HTTP/1.0 200 OK\r\n                ← 响应行：版本 状态码 描述
Content-Type: text/html\r\n        ← 响应头
Content-Length: 79\r\n
\r\n                               ← 空行，标志头部结束
<html>...</html>                   ← 响应体（Content-Length 字节）
```

**HTTP = 特定格式的纯文本 + TCP 传输。** 你会 TCP 了，剩下只是学它的格式规则。

### 9.2 常用状态码

| 状态码 | 含义 | 常见原因 |
|--------|------|---------|
| 200 OK | 成功 | 正常请求 |
| 404 Not Found | 文件不存在 | URL 写错了 |
| 403 Forbidden | 无权限 | 文件权限问题 |
| 500 Internal Server Error | 服务器内部出错 | 服务器代码 Bug |

### 9.3 把 handle_client 换成 handle_http

```c
// 替换第六关服务器中的 handle_client：
void handle_http(int connfd) {
    char buf[4096] = {0};
    read(connfd, buf, sizeof(buf) - 1);   // 读取请求（简化处理）
    printf("[HTTP] %s\n", buf);

    const char *body =
        "<html><body>"
        "<h1>Hello from my C HTTP server!</h1>"
        "<p>CSAPP is awesome.</p>"
        "</body></html>";

    char response[4096];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        strlen(body), body);

    write_all(connfd, response, n);
}
```

把 `main` 里的 `handle_client(connfd)` 改成 `handle_http(connfd)`，重新编译，然后：

```bash
# 用 nc 验证：
$ printf "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n" | nc localhost 8888
HTTP/1.0 200 OK
Content-Type: text/html
Content-Length: 79
Connection: close

<html><body><h1>Hello from my C HTTP server!</h1><p>CSAPP is awesome.</p></body></html>

# 或者直接用浏览器访问：http://localhost:8888
```

---

## 综合练习：实现一个能读取并返回本地文件的 HTTP 服务器

把前面学到的 `open/read`（第10章）和 HTTP 响应格式（第九关）结合起来：

```c
// file_server.c：请求哪个路径就返回哪个文件的内容
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    8888
#define BUFSIZE 65536

ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf; size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; left -= w;
    }
    return (ssize_t)n;
}

void serve_file(int connfd, const char *path) {
    // 安全检查：禁止路径穿越（../../etc/passwd）
    if (strstr(path, "..")) {
        const char *resp = "HTTP/1.0 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        write_all(connfd, resp, strlen(resp));
        return;
    }

    // 打开文件（去掉前导 /）
    const char *filename = (path[0] == '/') ? path + 1 : path;
    if (strlen(filename) == 0) filename = "index.html";

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        const char *resp =
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n\r\n"
            "Not Found";
        write_all(connfd, resp, strlen(resp));
        return;
    }

    // 获取文件大小
    struct stat st;
    fstat(fd, &st);
    off_t size = st.st_size;

    // 发送响应头
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",
        (long long)size);
    write_all(connfd, header, hlen);

    // 分块读取文件，发送响应体
    char buf[BUFSIZE];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write_all(connfd, buf, n);

    close(fd);
}

void handle_request(int connfd) {
    char buf[4096] = {0};
    read(connfd, buf, sizeof(buf) - 1);

    // 解析请求行：GET /path HTTP/1.0
    char method[16] = {0}, path[256] = {0};
    sscanf(buf, "%15s %255s", method, path);
    printf("[请求] %s %s\n", method, path);

    serve_file(connfd, path);
}

int main() {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);
    bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listenfd, 128);
    printf("文件服务器启动：http://localhost:%d\n", PORT);
    printf("会返回当前目录下的文件\n");

    while (1) {
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) continue;
        handle_request(connfd);
        close(connfd);
    }
}
```

```bash
$ gcc -o file_server file_server.c && ./file_server
文件服务器启动：http://localhost:8888

# 在另一个终端，或用浏览器访问：
$ curl http://localhost:8888/Makefile      # 返回 Makefile 内容
$ curl http://localhost:8888/noexist.txt   # 返回 404
$ curl http://localhost:8888/../etc/passwd # 返回 403（安全检查生效）
```

**扩展任务**：把 `main` 里的 `handle_request` 改为多线程版本（参考第八关的多线程模板）。

---

## 自测：你掌握了吗？

**第1关**：`listenfd` 和 `connfd` 分别是什么？能在 `listenfd` 上直接 `read/write` 吗？
<details><summary>答案</summary>
listenfd 是监听套接字，通过 socket+bind+listen 创建，全程只有一个，只用来调用 accept() 等待新连接。connfd 是 accept() 每次返回的已连接套接字，每个客户端一个，通信只在 connfd 上进行。在 listenfd 上 read/write 是错误的。
</details>

**第2关**：`htons(8888)` 和直接写 `8888` 有什么区别？不调用会发生什么？
<details><summary>答案</summary>
htons(8888) 把 0x22B8 的字节顺序转为网络字节序（大端）。在 x86 上 htons(8888) = 0xB822。不调用的话，放进 sockaddr_in.sin_port 的是小端的 0x22B8，内核解读为端口 0xB822 = 47138，不是 8888，客户端连 8888 会报 "Connection refused"。
</details>

**第3关**：发送了两次 `write`，对方 `read` 一定能收到两条消息吗？
<details><summary>答案</summary>
不一定。TCP 是字节流，不保留 write 边界。必须在应用层定义消息边界：行协议（\n 结尾）、定长消息、或长度前缀（先发 4 字节长度再发内容）。
</details>

**第4关**：多线程服务器中，向线程传参为什么必须 `malloc`，不能用局部变量地址？
<details><summary>答案</summary>
主线程的局部变量在下一次循环时会被覆盖。如果子线程还没来得及读取这个地址，主线程就进入了下一次循环并修改了变量，子线程读到的是新连接的 fd（竞态条件）。malloc 给每个线程独立的堆内存，生命周期由线程自己管理。
</details>

**第5关**：重启服务器时遇到 "bind: Address already in use"，原因是什么？怎么解决？
<details><summary>答案</summary>
TCP 连接关闭后有一段 TIME_WAIT 状态（约 1~4 分钟），内核继续占用该端口防止延迟数据包干扰新连接。解决方法：在 bind() 之前设置 setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))，让内核允许复用处于 TIME_WAIT 的端口。
</details>

---

## 3.5 小时学习路径

| 时间段 | 内容 | 动手任务 |
|--------|------|---------|
| 0–15 分钟 | 第一关 | 执行 nc 命令，看 HTTP 响应，理解"我将要实现这个" |
| 15–35 分钟 | 第二关（IP/端口/字节序）| 用 printf 验证 htons(8888) 的返回值 |
| 35–60 分钟 | 第三关（Socket API）| 对着全景图，逐个理解每个 API 的作用 |
| 60–90 分钟 | **第四关** | 编译运行 min_server + min_client，看到 "Hello" |
| 90–110 分钟 | 第五关（write_all）| 把 write_all 抄入自己的代码，理解为什么需要循环 |
| 110–150 分钟 | **第六关** | 编译运行 echo_server + echo_client，两个终端实测 |
| 150–175 分钟 | 第七关（粘包）| 手写 send_msg/recv_msg，替换 echo 服务器的通信部分 |
| 175–200 分钟 | **第八关** | 把 echo_server 改为多线程版本，验证两个客户端同时连 |
| 200–210 分钟 | 第九关 + 综合练习 + 自测 | 编译运行 file_server，用浏览器或 curl 访问 |
