# Chapter 12 并发编程（Concurrent Programming）

> **课程对应**：CMU 15-213 Lecture 23–25 — Concurrent Programming（Threads, Synchronization）
> **教材章节**：CSAPP 第 12 章

---

## 12.0 本章核心问题

> **如何让程序同时处理多件事？并发带来的共享数据问题如何正确解决？**

并发（Concurrency）不仅用于提升性能，更是应对多个同时发生的外部事件（I/O、用户输入、网络连接）的基本手段。本章覆盖三种并发实现方式，以及共享内存同步的核心机制。

---

## 12.1 并发的三种实现方式（§12.1–12.3）

| 方式 | 机制 | 优点 | 缺点 |
|------|------|------|------|
| **基于进程** | `fork` + IPC | 独立地址空间，健壮 | 创建开销大，共享数据复杂（需IPC） |
| **I/O 多路复用** | `select`/`epoll` | 单进程/线程，无同步问题 | 编程复杂，无法利用多核 |
| **基于线程** | `pthread` | 共享地址空间，轻量 | 共享数据需同步，难以调试 |

---

## 12.2 基于进程的并发（§12.1）

```c
/* 并发服务器：每个连接 fork 一个子进程 */
int main() {
    listenfd = open_listenfd(port);
    while (1) {
        connfd = accept(listenfd, &clientaddr, &clientlen);
        if (fork() == 0) {        // 子进程
            close(listenfd);      // 子进程不需要监听fd
            echo(connfd);
            close(connfd);
            exit(0);
        }
        close(connfd);            // 父进程关闭已连接fd（子进程持有引用）
    }
}
```

**必须关闭 `connfd`**：文件描述符引用计数，父子进程各持一份，两者都关闭才真正关闭连接。

**回收僵尸进程**：安装 `SIGCHLD` 处理函数调用 `waitpid`，否则子进程结束后变为僵尸。

---

## 12.3 I/O 多路复用（§12.2）

### `select` 系统调用

```c
#include <sys/select.h>

int select(int n, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
// n：监听的最大fd+1
// 返回：就绪的fd总数，0=超时，-1=错误

/* fd_set 操作宏 */
FD_ZERO(&set);       // 清空集合
FD_SET(fd, &set);    // 添加fd到集合
FD_CLR(fd, &set);    // 从集合删除fd
FD_ISSET(fd, &set);  // 检查fd是否在集合中且就绪
```

**基于状态机的事件驱动服务器**：

```c
/* 每个客户端对应一个状态（缓冲区+读取状态） */
typedef struct {
    int maxfd;
    fd_set read_set;    // 当前监听的所有fd
    fd_set ready_set;   // select返回后就绪的fd
    int nready;
    int listenfd;
    client_state clients[FD_SETSIZE];
} pool;

while (1) {
    pool.ready_set = pool.read_set;
    pool.nready = select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);

    if (FD_ISSET(listenfd, &pool.ready_set))
        add_client(accept(...), &pool);       // 新连接
    for (each connfd in pool)
        if (FD_ISSET(connfd, &pool.ready_set))
            echo_client(connfd, &pool);       // 服务已有连接
}
```

**Linux `epoll`（高性能替代）**：`select` 每次调用需重置 fd_set 且为 O(n)；`epoll` 内核维护就绪列表，`epoll_wait` 为 O(就绪fd数)，是 Nginx、Node.js 的基础。

---

## 12.4 基于线程的并发（§12.3）

### 线程基础

**线程（Thread）**：进程内的执行流，共享进程的地址空间（堆、代码、数据、打开的文件），但各有独立的**线程ID、栈、寄存器、PC**。

```c
#include <pthread.h>

/* 创建线程 */
int pthread_create(pthread_t *tid, pthread_attr_t *attr,
                   void *(*func)(void *), void *arg);

/* 等待线程结束，获取返回值 */
int pthread_join(pthread_t tid, void **thread_return);

/* 线程自行终止 */
void pthread_exit(void *retval);

/* 获取自身线程ID */
pthread_t pthread_self(void);

/* 分离线程（不需要join时使用，资源自动回收） */
int pthread_detach(pthread_t tid);
```

### 并发服务器（线程版）

```c
/* 错误版本：传递栈变量地址，存在竞争 */
while (1) {
    connfd = accept(listenfd, &clientaddr, &clientlen);
    pthread_create(&tid, NULL, thread, &connfd);  // 危险！
}

/* 正确版本：为每个连接分配堆内存 */
while (1) {
    connfd = accept(listenfd, &clientaddr, &clientlen);
    int *connfdp = malloc(sizeof(int));
    *connfdp = connfd;
    pthread_create(&tid, NULL, thread, connfdp);
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());  // 分离，自动回收资源
    free(vargp);                     // 释放堆内存
    echo(connfd);
    close(connfd);
    return NULL;
}
```

---

## 12.5 共享变量（§12.4）

### 线程内存模型

同一进程的所有线程共享：**全局变量、堆、打开的文件描述符**。

每个线程私有：**栈（局部变量）、寄存器、程序计数器**。

> **注意**：局部变量存在栈上，但指向局部变量的指针可以被传递给其他线程，打破"栈私有"假设。

### 什么是共享变量

变量 `v` 是共享的，当且仅当**多于一个线程引用 `v` 的某个实例**。

- 全局变量：总是共享
- 局部变量：通常不共享，但若将指针传递给其他线程则变为共享

---

## 12.6 同步：信号量（§12.5）

### 竞争条件（Race Condition）

```c
/* 两个线程同时对 cnt 加1，实际可能只加了1 */
volatile long cnt = 0;

void *thread(void *vargp) {
    for (long i = 0; i < niters; i++)
        cnt++;  // 非原子操作！汇编为：load、add、store 三条指令
    return NULL;
}
```

`cnt++` 编译为三条汇编指令，线程可能在任意指令间被中断，导致**更新丢失**。

### 临界区与互斥

**临界区（Critical Section）**：访问共享变量的代码段，必须互斥执行。

**互斥锁（Mutex / `pthread_mutex_t`）**：基于信号量的二值锁。

```c
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_lock(&mutex);
/* 临界区：只有一个线程能到达这里 */
cnt++;
pthread_mutex_unlock(&mutex);
```

### Dijkstra 信号量（§12.5.3）

信号量 `s`：非负整数，只能通过两个原子操作访问：

- **P(s)（Wait/Down）**：若 `s > 0`，则 `s--`；否则阻塞直到 `s > 0`
- **V(s)（Signal/Up）**：`s++`，唤醒一个等待线程

```c
#include <semaphore.h>

sem_t s;
sem_init(&s, 0, 1);  // 初始值=1，实现互斥锁

sem_wait(&s);         // P(s)
cnt++;                // 临界区
sem_post(&s);         // V(s)
```

**互斥锁 = 初始值为1的信号量**：P/V 保证临界区在任意时刻只有一个线程执行。

---

## 12.7 信号量应用模式（§12.5.4–12.5.5）

### 生产者-消费者（Producer-Consumer）

典型场景：有界缓冲区、流水线处理。

```c
/* 三个信号量：mutex保护缓冲区，slots=空闲槽数，items=可用项数 */
sem_t mutex, slots, items;

void producer(void) {
    while (1) {
        item = produce();
        sem_wait(&slots);   // 等待空闲槽
        sem_wait(&mutex);   // 加锁
        insert(buf, item);
        sem_post(&mutex);   // 解锁
        sem_post(&items);   // 通知消费者
    }
}

void consumer(void) {
    while (1) {
        sem_wait(&items);   // 等待可用项
        sem_wait(&mutex);
        item = remove(buf);
        sem_post(&mutex);
        sem_post(&slots);   // 通知生产者
        consume(item);
    }
}
```

### 读者-写者（Readers-Writers）

**第一类**（读者优先）：只要有读者在，写者等待；读者之间不互斥。

```c
/* 读者：检查是否是第一个读者，是则锁写锁 */
sem_wait(&mutex);
readcnt++;
if (readcnt == 1)       // 第一个读者
    sem_wait(&w);       // 阻止写者
sem_post(&mutex);

/* 读操作... */

sem_wait(&mutex);
readcnt--;
if (readcnt == 0)       // 最后一个读者
    sem_post(&w);       // 允许写者
sem_post(&mutex);
```

**问题**：可能导致写者**饥饿（Starvation）**。

---

## 12.8 其他同步原语

### 条件变量（Condition Variable）

与互斥锁配合，等待特定条件成立：

```c
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* 等待条件 */
pthread_mutex_lock(&mutex);
while (!condition)              // 必须用 while，不能用 if（防止虚假唤醒）
    pthread_cond_wait(&cond, &mutex);  // 原子释放锁并等待
/* 条件满足，执行操作 */
pthread_mutex_unlock(&mutex);

/* 通知 */
pthread_mutex_lock(&mutex);
condition = true;
pthread_cond_signal(&cond);    // 唤醒一个等待线程
pthread_mutex_unlock(&mutex);
```

---

## 12.9 死锁（Deadlock，§12.5.6）

**死锁**：两个或多个线程互相持有对方需要的锁，永久阻塞。

```
线程A：lock(m1); lock(m2);  ← 等待m2
线程B：lock(m2); lock(m1);  ← 等待m1，死锁！
```

**预防死锁的互斥锁排序规则**：所有线程按**全局一致的顺序**申请锁（如始终先申请编号小的锁）。

**死锁的四个必要条件**（Coffman）：互斥、持有并等待、不可抢占、循环等待。破坏任一即可避免死锁。

---

## 12.10 线程安全（§12.6）

**线程安全函数**：被多个并发线程调用时总能产生正确结果。

**四类线程不安全函数**：

| 类别 | 示例 | 修复方法 |
|------|------|---------|
| 1. 不保护共享变量 | `rand`（某些实现） | 加锁 |
| 2. 跨调用保持状态 | `strtok`, `rand` | 改用重入版本（`strtok_r`）|
| 3. 返回静态变量指针 | `gethostbyname`, `ctime` | 加锁+复制，或改用`_r`版本 |
| 4. 调用线程不安全函数 | 调用了1-3类的函数 | 修改被调用函数 |

**可重入函数（Reentrant）**：不访问任何共享数据，线程安全的最强形式（无需加锁）。

常见不可重入函数及其替代：

| 不安全 | 安全替代 |
|--------|---------|
| `strtok` | `strtok_r` |
| `gethostbyname` | `getaddrinfo` |
| `ctime` | `ctime_r` |
| `rand` | `rand_r` |

---

## 12.11 竞争与性能（§12.7）

### 竞争（Race）

程序的正确性依赖于一个线程在另一个线程到达某点之前到达另一点。例如：

```c
/* 常见错误：将循环变量地址传给线程 */
for (int i = 0; i < N; i++) {
    pthread_create(&tid, NULL, thread, &i);  // 传栈地址，i会变化！
}

void *thread(void *vargp) {
    int myid = *((int *)vargp);  // 读到的可能不是创建时的i值
    ...
}
```

**修复**：为每次循环创建独立的堆内存存储 i。

### 线程并行性能

```c
/* Amdahl 定律：加速比上限 */
// 假设串行比例为 p，并行比例为 (1-p)，使用 k 个核心
// 最大加速比 S_k = 1 / (p + (1-p)/k)
// 即：当 k→∞ 时，S_∞ = 1/p（由串行部分决定上限）
```

**同步开销**：过细粒度的锁（每个操作都加锁）性能差，应减少临界区进出次数。

### 线程池（Thread Pool）

预先创建固定数量线程，避免频繁 `pthread_create` 开销：

```
主线程 → 任务队列（生产者-消费者） → 工作线程池
```

---

## 12.12 常见陷阱与调试建议

| 问题 | 症状 | 排查方法 |
|------|------|---------|
| 忘记初始化信号量 | 随机崩溃或死锁 | 总是显式调用 `sem_init` |
| 条件变量用 `if` 而非 `while` | 虚假唤醒导致逻辑错误 | 统一使用 `while` 循环 |
| 锁粒度过粗 | 并发性能与单线程相似 | 分析热点，细化锁 |
| 忘记 `pthread_join`/`detach` | 线程资源泄漏 | 每个线程必须被 join 或 detach |
| 在持锁时调用阻塞I/O | 其他线程长时间等待锁 | 持锁时间尽量短，I/O在锁外进行 |

**调试工具**：
- **ThreadSanitizer（TSan）**：`-fsanitize=thread`，运行时检测数据竞争
- **Helgrind**（Valgrind组件）：检测死锁和竞争
- **gdb**：`info threads`、`thread N` 切换线程

---

## 12.13 与其他章节的逻辑联系

- **第8章（ECF）**：信号与线程的交互（哪些函数是异步信号安全的）
- **第9章（VM）**：多线程共享同一虚拟地址空间，这是共享变量问题的根源
- **第10章（I/O）**：`select`/`epoll` 实现事件驱动并发，避免阻塞
- **第11章（网络）**：并发服务器（进程/线程/事件驱动）是网络编程的核心主题

---

## 12.14 要点速览

1. **三种并发模型**：进程（隔离强）、I/O多路复用（单线程，适合事件驱动）、线程（轻量共享，需同步）。
2. **信号量是核心同步原语**：互斥锁=P/V包围临界区；生产者-消费者=三信号量模式。
3. **死锁预防靠全序加锁**：所有线程按相同顺序申请多个锁，消除循环等待。
4. **线程安全 ≠ 可重入**：可重入更强（无共享状态），优先使用 `_r` 后缀的安全版本函数。
5. **竞争条件难复现**：写并发代码时用 TSan 检测，用 `while` 而非 `if` 等待条件变量。
