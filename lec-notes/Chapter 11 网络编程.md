# Chapter 11 网络编程（Network Programming）

> **课程对应**：CMU 15-213 Lecture 21–22 — Network Programming
> **教材章节**：CSAPP 第 11 章

---

## 11.0 本章核心问题

> **两台计算机上的进程如何通过网络通信？Socket API 如何将网络连接抽象为文件描述符？**

本章建立从物理网络到应用层协议的完整认知链，并通过 Socket API 展示如何编写实际的客户端/服务器程序。

---

## 11.1 客户端-服务器模型（§11.1）

**所有网络应用的基础模型**：

```
客户端 ──── 请求 ────▶ 服务器
客户端 ◀─── 响应 ──── 服务器
```

- **服务器**：管理某种资源（文件、数据库、计算能力），长期运行，等待请求
- **客户端**：向服务器发起请求，处理响应

**事务（Transaction）**：一次完整的请求+响应周期。服务器可同时服务多个客户端（并发）。

> 注意：客户端和服务器是**进程角色**，不是主机身份——同一台机器上可以同时运行客户端和服务器进程。

---

## 11.2 网络模型（§11.2）

### 物理层次

```
主机        局域网(LAN)      广域网(WAN/Internet)
  └── Adapter ─── Hub/Switch ─── Router ─── ... ─── Router ─── 目标主机
```

- **以太网（Ethernet）**：最常见的 LAN 技术，帧大小上限约 1500 字节（MTU）
- **路由器（Router）**：连接不同网络，负责转发分组

### 协议层次（TCP/IP 栈）

| 层次 | 协议 | 数据单元 |
|------|------|---------|
| 应用层 | HTTP, SMTP, FTP | 消息 |
| 传输层 | TCP, UDP | 段（Segment） |
| 网络层 | IP | 数据报（Datagram） |
| 链路层 | Ethernet, WiFi | 帧（Frame） |

**封装（Encapsulation）**：每层为数据加上头部，向下传递；接收时逐层剥离头部。

---

## 11.3 Internet 核心概念（§11.3）

### IP 地址

- **IPv4**：32位，点分十进制（如 `128.2.194.242`），共约 43 亿个地址
- **IPv6**：128位，冒号分十六进制，正在普及

```c
/* 网络字节序（大端）与主机字节序转换 */
#include <arpa/inet.h>
uint32_t htonl(uint32_t hostlong);   // host to network long
uint16_t htons(uint16_t hostshort);  // host to network short
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
```

**结构体**：

```c
struct in_addr {
    uint32_t s_addr;  // 网络字节序的IP地址
};
```

### 点分十进制与二进制转换

```c
int inet_pton(AF_INET, const char *src, void *dst);
// 点分十进制字符串 → 网络字节序二进制

const char *inet_ntop(AF_INET, const void *src, char *dst, socklen_t size);
// 网络字节序二进制 → 点分十进制字符串
```

### Internet 域名（DNS）

**DNS（Domain Name System）**：将域名映射到 IP 地址的分布式数据库。

```
www.cs.cmu.edu ──DNS查询──▶ 128.2.131.69
```

```c
#include <netdb.h>

/* 现代接口：域名/端口 → 套接字地址列表 */
int getaddrinfo(const char *host, const char *service,
                const struct addrinfo *hints,
                struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);

/* 反向查询：套接字地址 → 域名/服务名 */
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen, int flags);
```

### Internet 连接

**端口（Port）**：16位整数，标识主机上的具体服务进程。

| 端口范围 | 用途 |
|---------|------|
| 0–1023 | 知名端口（需 root 权限）|
| 1024–49151 | 注册端口 |
| 49152–65535 | 短暂端口（临时客户端端口）|

常用知名端口：HTTP=80, HTTPS=443, SMTP=25, SSH=22, DNS=53

**套接字对（Socket Pair）** 唯一标识一个连接：

```
(客户端IP:客户端端口, 服务器IP:服务器端口)
```

---

## 11.4 Socket 接口（§11.4）

Socket 是操作系统提供的抽象，使网络连接像文件描述符一样操作。

### 套接字地址结构

```c
/* 通用套接字地址（历史原因，用于强制类型转换） */
struct sockaddr {
    uint16_t sa_family;   // 协议族（AF_INET, AF_INET6）
    char     sa_data[14];
};

/* IPv4 专用套接字地址 */
struct sockaddr_in {
    uint16_t       sin_family;  // AF_INET
    uint16_t       sin_port;    // 端口号（网络字节序）
    struct in_addr sin_addr;    // IP地址（网络字节序）
    unsigned char  sin_zero[8]; // 填充
};
```

### 服务器端流程

```c
/* 1. 创建套接字（端点） */
int listenfd = socket(AF_INET, SOCK_STREAM, 0);

/* 2. 绑定地址和端口 */
bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr));

/* 3. 转换为监听套接字（设置等待队列大小） */
listen(listenfd, LISTENQ);  // LISTENQ 通常为 1024

/* 4. 等待并接受连接（阻塞直到客户端连接） */
int connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
// connfd 是新的已连接套接字，专用于此次通信

/* 5. 读写通信 */
rio_readlineb(&rio, buf, MAXLINE);
rio_writen(connfd, buf, strlen(buf));

/* 6. 关闭已连接套接字 */
close(connfd);
```

### 客户端流程

```c
/* 1. 创建套接字 */
int clientfd = socket(AF_INET, SOCK_STREAM, 0);

/* 2. 连接服务器（自动分配客户端端口） */
connect(clientfd, (SA *)&serveraddr, sizeof(serveraddr));

/* 3. 读写通信 */
rio_writen(clientfd, buf, strlen(buf));
rio_readlineb(&rio, buf, MAXLINE);

/* 4. 关闭 */
close(clientfd);
```

### 监听套接字 vs 已连接套接字

| | 监听套接字（listenfd）| 已连接套接字（connfd）|
|--|----------------------|----------------------|
| 生命周期 | 服务器全程存活 | 每次连接一个 |
| 作用 | 等待新连接 | 与特定客户端通信 |
| 数量 | 通常一个 | 每个连接一个 |

### 封装：`open_clientfd` 和 `open_listenfd`

CSAPP 提供的辅助函数，内部使用 `getaddrinfo` 实现协议无关：

```c
/* 客户端：连接到 hostname:port，返回已连接fd */
int clientfd = open_clientfd(hostname, port);

/* 服务器：在 port 上监听，返回监听fd */
int listenfd = open_listenfd(port);
```

---

## 11.5 Web 服务器（§11.5–11.6）

### HTTP 基础

**HTTP（HyperText Transfer Protocol）**：基于文本的请求/响应协议，运行在 TCP 之上。

**HTTP 请求格式**：
```
GET /index.html HTTP/1.1\r\n     ← 请求行：方法 URI 版本
Host: www.example.com\r\n        ← 请求头（可多个）
\r\n                              ← 空行（头部结束）
```

**HTTP 响应格式**：
```
HTTP/1.1 200 OK\r\n              ← 响应行：版本 状态码 状态描述
Content-Type: text/html\r\n      ← 响应头
Content-Length: 1234\r\n
\r\n                              ← 空行
<html>...</html>                  ← 响应体
```

**常用状态码**：

| 状态码 | 含义 |
|--------|------|
| 200 OK | 成功 |
| 301 Moved Permanently | 永久重定向 |
| 404 Not Found | 资源不存在 |
| 403 Forbidden | 权限不足 |
| 500 Internal Server Error | 服务器内部错误 |

### URI 与静态/动态内容

- **静态内容（Static Content）**：服务器直接返回文件（HTML、图片等）
- **动态内容（Dynamic Content）**：服务器执行程序，返回程序输出

**CGI（Common Gateway Interface）**：服务器执行子进程（CGI 程序），通过环境变量和 stdin/stdout 传递请求/响应：

```
GET /cgi-bin/adder?12&42 HTTP/1.1
```

服务器设置环境变量 `QUERY_STRING="12&42"`，`fork+exec` CGI 程序，用 `dup2` 将子进程 stdout 重定向到 connfd。

### Tiny Web Server 核心逻辑

```c
void doit(int fd) {
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio;

    rio_readinitb(&rio, fd);
    rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (!strcasecmp(method, "GET")) {
        // 解析URI，判断静态/动态
        if (is_static(uri)) {
            serve_static(fd, filename, filesize);
        } else {
            serve_dynamic(fd, filename, cgiargs);
        }
    }
}
```

---

## 11.6 常见陷阱与实践建议

| 陷阱 | 说明 |
|------|------|
| **字节序错误** | IP地址和端口必须使用 `htons`/`htonl` 转换为网络字节序 |
| **`SO_REUSEADDR`** | 服务器重启时端口处于 TIME_WAIT，需设置此选项避免 "Address in use" |
| **SIGPIPE** | 客户端关闭连接后继续写入触发 SIGPIPE，需忽略或处理 |
| **TCP 粘包** | TCP 是字节流，`read` 不保证一次读取完整的"消息"，需要应用层分帧 |
| **`getaddrinfo` 替代 `gethostbyname`** | 后者不可重入且不支持 IPv6 |

---

## 11.7 与其他章节的逻辑联系

- **第10章（I/O）**：Socket 是特殊的文件描述符，`read`/`write`/`close` 完全适用
- **第8章（ECF）**：`accept` 阻塞等待连接；`fork` 实现并发服务器
- **第12章（并发）**：单线程顺序服务器性能差，需要进程/线程/I/O多路复用并发处理请求

---

## 11.8 要点速览

1. **Socket 是文件描述符**：网络通信复用 `read`/`write` 接口，降低了编程模型复杂度。
2. **监听fd ≠ 已连接fd**：`accept` 返回新 fd 专用于该连接，监听fd继续等待新连接。
3. **字节序必须转换**：IP地址和端口在网络上必须是大端序，用 `htons`/`htonl` 系列函数。
4. **TCP 是字节流**：没有消息边界，应用必须用长度字段或分隔符自行分帧。
5. **用 `getaddrinfo`**：协议无关的现代接口，自动支持 IPv4/IPv6，避免使用过时的 `gethostbyname`。
