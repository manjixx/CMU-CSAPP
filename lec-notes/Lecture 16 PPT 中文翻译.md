# Lecture 16: System-Level I/O (系统级 I/O)
## PPT 16 中文翻译

**课程信息**
- **课程**：15-213 / 18-213: Introduction to Computer Systems（计算机系统导论）
- **第15讲**，2013年10月17日
- **讲师**：Randy Bryant, Dave O'Hallaron, Greg Kesden

---

## 今天的内容

• Unix I/O  
• Metadata, sharing, and redirection（元数据、共享和重定向）  
• Standard I/O（标准 I/O）  
• RIO (robust I/O) package（RIO 健壮 I/O 包）  
• Closing remarks（总结）

---

## Unix 文件

• 一个 Unix 文件是 m 字节的序列：B₀, B₁, …, Bₘ₋₁

• 所有 I/O 设备都被表示为文件：
  - `/dev/sda2` （用户磁盘分区）
  - `/dev/tty2` （终端）

• 甚至内核也被表示为文件：
  - `/dev/kmem` （内核内存镜像）
  - `/proc` （内核数据结构）

---

## Unix 文件类型

• **普通文件（Regular file）**
  - 包含任意数据
  - 应用程序通常区分文本文件（text file）和二进制文件（binary file）
  - 内核不区分这两种类型

• **目录（Directory）**
  - 包含一组链接（link）的文件，每个链接将一个文件名映射到一个文件（可能是另一个目录）
  - 每个目录至少包含两个条目：`.`（指向目录本身）和 `..`（指向父目录）

• **套接字（Socket）**
  - 用于与另一台机器上的进程进行网络通信

---

## 打开文件

• 应用程序通过调用 `open` 函数来通知内核它想要访问一个 I/O 设备：
  ```c
  int open(char *filename, int flags, mode_t mode);
  ```

• `open` 函数返回一个小的非负整数，称为**文件描述符（file descriptor）**

• 内核记录所有关于这个打开文件的信息，应用程序只需记住描述符

• 内核在三个预定义描述符上打开文件：
  - `0`：标准输入（stdin）
  - `1`：标准输出（stdout）
  - `2`：标准错误（stderr）

---

## 关闭文件

• 应用程序通过调用 `close` 函数通知内核它已完成对文件的访问：
  ```c
  int close(int fd);
  ```

• 关闭已关闭的描述符会出错

---

## 读文件

• 应用程序通过调用 `read` 函数来从文件读取 n 个字节：
  ```c
  ssize_t read(int fd, void *buf, size_t n);
  ```

• `read` 从描述符 `fd` 的当前文件位置开始，最多复制 n 个字节到内存位置 `buf`

• 返回值：
  - -1：表示错误
  - 0：表示 EOF（文件结束）
  - 否则返回值：从文件中实际读取的字节数（short count）

---

## 写文件

• 应用程序通过调用 `write` 函数向文件写入 n 个字节：
  ```c
  ssize_t write(int fd, const void *buf, size_t n);
  ```

• `write` 从内存位置 `buf` 开始复制最多 n 个字节到描述符 `fd` 的当前文件位置

• 写入文件将字节从内存复制到当前文件位置，然后更新当前文件位置

• 返回值：
  - 返回写入的字节数
  - nbytes < 0 表示错误
  - 与读操作一样，**short counts（短计数）是可能的，并且不是错误！**

---

## 简单的 Unix I/O 示例

将标准输入一个字节一个字节地复制到标准输出：

```c
#include "csapp.h"

int main(void)
{
    char c;
    
    while(Read(STDIN_FILENO, &c, 1) != 0)
        Write(STDOUT_FILENO, &c, 1);
    
    exit(0);
}
```

---

## 关于 Short Counts（短计数）

**Short counts 可能在以下情况发生：**
• 读取时遇到 EOF
• 从终端读取文本行
• 读取和写入网络套接字或 Unix 管道

**Short counts 在以下情况永远不会发生：**
• 从磁盘文件读取（除了 EOF 之外）
• 写入磁盘文件

**在代码中处理 short counts 的一种方法：**
• 使用教科书中 `csapp.c` 文件的 RIO (Robust I/O) 包（附录 B）

---

## RIO 包

• RIO 是一组包装函数，为应用程序提供高效且健壮的 I/O，特别是那些容易遇到 short counts 的网络程序

• RIO 提供两种不同类型的函数：
  - **无缓冲的二进制数据输入/输出**：`rio_readn`, `rio_writen`
  - **带缓冲的二进制数据和文本行输入**：`rio_readlineb`, `rio_readnb`

• 带缓冲的 RIO 例程是**线程安全的**，可以在同一个描述符上任意交错使用

---

## RIO 函数

### 无缓冲的 RIO 输入/输出函数

```c
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
```

• 用于在网络程序中高效地传输二进制数据
• 自动处理不足值（short counts）

### 带缓冲的 RIO 输入函数

```c
void rio_readinitb(rio_t *rp, int fd);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
```

• 用于高效地读取文本行和二进制数据
• 使用内部缓冲区
• 自动处理不足值

---

## 文件元数据（Metadata）

• **元数据**是关于数据的数据，在这里是文件的数据

• 每个文件的元数据由内核维护，用户通过 `stat` 和 `fstat` 函数访问

• `struct stat` 包含：
  - **设备（device）**
  - **inode 编号**
  - **模式（mode）**（文件类型和权限位）
  - **链接数（number of links）**
  - **所有者 ID**（UID 和 GID）
  - **大小（size）**（字节数）
  - **块大小（block size）**
  - **访问/修改/状态改变时间**

---

## 文件类型和权限

文件类型编码在 `st_mode` 字段中：

• **S_ISREG(m)**：是否为普通文件？
• **S_ISDIR(m)**：是否为目录文件？
• **S_ISSOCK(m)**：是否为套接字？

权限位：
• **S_IRUSR**：用户读
• **S_IWUSR**：用户写
• **S_IXUSR**：用户执行

---

## 目录

• 目录的内容包含**文件名和指向底层文件的链接**的映射

• 对目录的每个条目，内核维护：
  - 文件名
  - 文件类型
  - 位置信息（i-number, inode number）

---

## 文件共享

文件共享通过以下三层结构实现：

1. **描述符表（Descriptor table）**
   - 每个进程有自己的描述符表
   - 每个打开的描述符表条目指向打开文件表中的条目

2. **打开文件表（Open file table）**
   - 所有进程共享
   - 每个打开文件的表条目包含：
     - 当前文件位置
     - 引用计数（reference count）
     - 指向 v-node 表条目的指针

3. **v-node 表（v-node table）**
   - 所有进程共享
   - 每个表条目包含：
     - stat 结构中的大部分信息
     - st_mode, st_size 等

---

## 父子进程如何共享文件

• `fork()` 创建一个新进程，它是调用进程的副本

• 子进程获得父进程描述符表的副本

• **父子进程共享相同的打开文件表项**，因此也共享相同的文件位置

• 当父进程或子进程写入文件时，文件位置被更新，两个进程都能看到新的位置

---

## I/O 重定向

• **I/O 重定向**允许用户将磁盘文件和标准输入/输出关联

• Shell 提供 I/O 重定向运算符：
  - `linux> ls > foo.txt`
  - 标准输出重定向到磁盘文件
  - `linux> ls >> foo.txt`
  - 标准输出追加到磁盘文件

• 如何实现重定向？

---

## I/O 重定向：如何使用 dup2

```c
#include <unistd.h>
int dup2(int oldfd, int newfd);
```

• `dup2` 函数复制描述符表条目 `oldfd` 到描述符表条目 `newfd`

• 如果 `newfd` 已经打开，`dup2` 在复制 `oldfd` 之前关闭 `newfd`

• **重定向示例：**
  ```c
  fd = Open("foo.txt", O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
  dup2(fd, STDOUT_FILENO);
  Close(fd);
  ```

---

## 标准 I/O 流

• C 标准库（libc）提供了一组更高级的输入/输出函数

• **标准 I/O 函数：**
  - `fopen`, `fclose`
  - `fread`, `fwrite`
  - `fgets`, `fputs`, `fscanf`, `fprintf`

• 将打开的文件建模为**流（stream）**：`FILE*`

• 每个 C 程序以三个流开始：
  - `stdin`（标准输入）
  - `stdout`（标准输出）
  - `stderr`（标准错误）

---

## 标准 I/O 流：缓冲

• **标准 I/O 函数使用缓冲来提高效率**

• 缓冲类型：
  - **无缓冲（unbuffered）**：立即写入（stderr 总是无缓冲）
  - **行缓冲（line buffered）**：遇到换行符时写入（stdout）
  - **全缓冲（fully buffered）**：缓冲区满时写入（磁盘文件）

---

## 优缺点比较：Unix I/O vs Standard I/O vs RIO

### Unix I/O（低级 I/O）
**优点：**
• 开销最低
• 异步信号安全（async-signal-safe）
• 提供对文件元数据的完全访问
• 与网络套接字兼容

**缺点：**
• 处理 short counts 很棘手
• 没有格式化功能

---

### Standard I/O（标准 I/O）
**优点：**
• 缓冲功能（提高效率）
• 易于使用
• 提供格式化功能（`printf`, `scanf`）

**缺点：**
• 不是信号安全的
• 对套接字有一些限制
• 不是线程安全的（某些情况下）

---

### RIO（健壮 I/O）
**优点：**
• 结合了健壮性和效率
• 处理 short counts
• 线程安全
• 在网络环境中安全使用
• 提供缓冲和非缓冲函数

**缺点：**
• 没有格式化功能
• 仅在 CS:APP 包中可用

---

## 选择 I/O 函数

**一般规则：**
• **尽可能使用最高级别的 I/O**

**具体建议：**

• **磁盘和终端文件：**
  - 大多数情况下使用**标准 I/O**

• **信号处理器：**
  - 使用**原始 Unix I/O**（异步信号安全）
  - 必须使用 `rio_writen` 或 `write`

• **网络套接字：**
  - 使用**RIO** 或**原始 Unix I/O**
  - 避免使用标准 I/O（缓冲问题）

• **二进制文件和原始字节流：**
  - 使用**Unix I/O** 或 **RIO**

• **文本文件和格式化输出：**
  - 使用**标准 I/O**

---

## 关闭备注（Closing Remarks）

• Unix I/O、Standard I/O 和 RIO 都在系统级 I/O 中发挥作用

• 不同的任务需要不同的抽象级别

• **关键是了解每种方法的优势和局限性**

• 对于网络编程，RIO 或 Unix I/O 通常是更好的选择

• 对于磁盘文件和格式化 I/O，Standard I/O 更合适

• 在信号处理器中，必须使用异步信号安全的函数（Unix I/O 或 `rio_writen`）

---

**注：** 本翻译基于 CSAPP (15-213/18-213) 课程的 Lecture 16 PPT 内容，保持了原始结构和核心概念。
