# 第12章 并发编程——从零开始手把手学

> **适合人群**：会写 C 程序，知道 `fork` 是什么，但从没写过多线程代码的同学
> **学完你能做到**：写出无竞态条件的多线程程序，用互斥锁和条件变量同步线程，解释死锁是怎么发生的并能修复它
> **预计时间**：认真读 + 动手跑每段代码，约 3.5 小时

---

## 第一关：为什么需要并发？——先建直觉

### 1.1 假设没有并发，会怎样？

回顾第11章的 Echo 服务器（顺序版）：

```
时间轴：
t=0  accept() → 开始处理客户端 A（A 在慢慢打字）
t=1  客户端 B 连接，进入等待队列
t=2  客户端 C 连接，进入等待队列
...
t=5  A 终于断开
t=5  开始处理 B（B 已经等了 5 秒！）
```

一个"慢客户端"能让整个服务器对其他人完全冻住。

**并发解决了什么**：让程序能**同时处理多件事**——不是等一件做完再做另一件。

### 1.2 三种实现并发的方式

| 方式 | 机制 | 特点 |
|------|------|------|
| **多进程** | `fork()` 创建独立进程 | 隔离好，开销大，进程间通信麻烦 |
| **多线程** | `pthread_create()` 轻量执行流 | 共享内存，轻量，需要同步 |
| **I/O 多路复用** | `select`/`epoll` 单线程监控多 fd | 极低开销，代码复杂，无法用多核 |

**本章重点：多线程**。它最常用，涉及的概念也最多。

### 1.3 线程 vs 进程——室友 vs 独栋

```
进程（每个进程独立地址空间）：
┌──────────────────┐   ┌──────────────────┐
│  进程 A 的地址空间 │   │  进程 B 的地址空间 │
│  代码/堆/栈/文件  │   │  代码/堆/栈/文件  │
└──────────────────┘   └──────────────────┘
  像两栋独立的楼，互不影响

线程（同一进程内的多个执行流）：
┌──────────────────────────────────────────────┐
│                  同一个进程                    │
│  代码段 / 数据段 / 堆 / 打开的文件  ← 所有线程共享 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ 线程1的栈 │  │ 线程2的栈 │  │ 线程3的栈 │   │
│  └──────────┘  └──────────┘  └──────────┘   │
│           各线程私有：栈、寄存器、PC              │
└──────────────────────────────────────────────┘
  像一套公寓里的室友：共用客厅（堆），各有卧室（栈）
```

**共享地址空间的好处**：线程间通信只需读写共同的全局变量，不需要管道或消息队列。  
**共享地址空间的代价**：两个线程同时写同一块内存，结果可能是乱的——这是本章的核心问题。

---

## 第二关：创建和使用线程

### 2.1 pthread 基本 API

```c
#include <pthread.h>
// 编译时加 -lpthread

// 创建线程
int pthread_create(pthread_t *tid,             // 输出：线程 ID
                   const pthread_attr_t *attr, // 线程属性，通常传 NULL
                   void *(*func)(void *),       // 线程函数（必须是这个签名）
                   void *arg);                  // 传给线程函数的参数
// 返回 0 表示成功

// 等待线程结束（类似 waitpid）
int pthread_join(pthread_t tid, void **retval);

// 分离线程：线程结束后自动回收，不需要 join
int pthread_detach(pthread_t tid);

// 线程自己退出
void pthread_exit(void *retval);

// 获取自己的线程 ID
pthread_t pthread_self(void);
```

**线程函数的签名必须是**：`void *func(void *arg)`——接受一个 `void *` 参数，返回 `void *`。

### 2.2 第一个多线程程序

```c
// hello_threads.c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

void *greet(void *arg) {
    int id = *((int *)arg);
    printf("线程 %d：你好！TID = %lu\n", id, (unsigned long)pthread_self());
    return NULL;
}

int main() {
    pthread_t tids[5];
    int ids[5];

    for (int i = 0; i < 5; i++) {
        ids[i] = i;
        pthread_create(&tids[i], NULL, greet, &ids[i]);
    }
    for (int i = 0; i < 5; i++) {
        pthread_join(tids[i], NULL);
    }
    printf("所有线程完成\n");
    return 0;
}
```

```bash
$ gcc -o hello_threads hello_threads.c -lpthread && ./hello_threads
线程 2：你好！TID = 140234567890432
线程 0：你好！TID = 140234567881216   ← 顺序每次可能不同！
线程 3：你好！TID = 140234567899648
线程 1：你好！TID = 140234567886336
线程 4：你好！TID = 140234567904864
所有线程完成
```

**注意**：输出顺序每次可能不同。线程的执行顺序由操作系统调度决定，程序**不能依赖**特定顺序。

---

## 第三关：竞态条件——先亲眼看到错误，再学解法

### 3.1 先猜答案

```c
// race_demo.c：两个线程各做 100 万次 counter++，期望结果是 200 万
#include <stdio.h>
#include <pthread.h>

long counter = 0;

void *increment(void *arg) {
    for (long i = 0; i < 1000000; i++)
        counter++;
    return NULL;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, increment, NULL);
    pthread_create(&t2, NULL, increment, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("最终计数：%ld（期望：2000000）\n", counter);
    return 0;
}
```

**你觉得运行结果是 2000000 吗？**

<details>
<summary>先猜一下，再点开</summary>

不是！而且每次运行结果都不一样：

```bash
$ gcc -O0 -o race_demo race_demo.c -lpthread
$ ./race_demo
最终计数：1374218   ← 不是 2000000！
$ ./race_demo
最终计数：1582934   ← 每次都不同！
$ ./race_demo
最终计数：1731056
```

</details>

### 3.2 为什么 `counter++` 不安全？汇编级分析

`counter++` 看起来是一行代码，但在 CPU 层面是**三步操作**：

```asm
LOAD    R1, [counter]   ; 第1步：从内存读到寄存器
ADD     R1, 1           ; 第2步：寄存器加 1
STORE   [counter], R1   ; 第3步：写回内存
```

当两个线程并发执行，可能发生：

```
时间→  线程1（执行 counter++）    线程2（执行 counter++）
t=1    LOAD  R1 = 5（读到 5）
t=2    ADD   R1 = 6
t=3                              LOAD  R1 = 5   ← 读到的还是 5！（线程1还没写回）
t=4    STORE counter = 6
t=5                              ADD   R1 = 6
t=6                              STORE counter = 6   ← 覆盖了线程1的写入！

结果：counter = 6，期望是 7。一次加法白做了！
```

两个线程各做 100 万次，就丢掉了几十万次加法。

### 3.3 临界区的概念

**临界区（Critical Section）**：访问共享资源的代码段，**同一时间只能有一个线程执行**。

`counter++` 是临界区，因为它读写了共享变量 `counter`。

**这就是竞态条件（Race Condition）**：程序的输出依赖多个线程执行的相对时序，产生不确定的结果。竞态条件是**最难发现和调试的 Bug**——有时候在开发机上跑 100 次都没事，部署到生产环境就崩了。

---

## 第四关：互斥锁——保护临界区

### 4.1 思路：公共洗手间的门锁

**类比**：公共洗手间只有一个马桶，门上有锁。你进去之前先锁门，出来后开锁。如果门锁着，你就在外面等。

```c
#include <pthread.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  // 定义并初始化一把锁

pthread_mutex_lock(&mutex);    // 加锁：如果已被锁住，阻塞等待
/* 临界区：同一时刻只有这一个线程在这里 */
counter++;
/* 临界区结束 */
pthread_mutex_unlock(&mutex);  // 解锁：通知等待的线程可以进入
```

### 4.2 修复竞态条件

```c
// race_fixed.c
#include <stdio.h>
#include <pthread.h>

long counter = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *increment(void *arg) {
    for (long i = 0; i < 1000000; i++) {
        pthread_mutex_lock(&mutex);    // 加锁
        counter++;                     // 临界区（现在是原子的了）
        pthread_mutex_unlock(&mutex);  // 解锁
    }
    return NULL;
}

int main() {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, increment, NULL);
    pthread_create(&t2, NULL, increment, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("最终计数：%ld（期望：2000000）\n", counter);
    return 0;
}
```

```bash
$ gcc -O0 -o race_fixed race_fixed.c -lpthread
$ ./race_fixed
最终计数：2000000   ← 每次都正确！
$ ./race_fixed
最终计数：2000000   ← 稳定！
```

### 4.3 互斥锁使用规则（必须遵守）

```
规则1：所有访问共享变量的代码（读和写），都必须持有同一把锁
       ❌ 错误：一处加锁写，另一处不加锁读

规则2：持锁时间尽量短，不要在锁内做 I/O、sleep 或其他耗时操作
       ❌ 错误：lock(); read(sockfd, buf, 1000); unlock();

规则3：所有出错路径都要解锁，不能忘记
       ❌ 错误：
       lock();
       if (error) return -1;   // 忘记 unlock，其他线程永远等待！
       unlock();
       ✅ 正确：
       lock();
       if (error) { unlock(); return -1; }
       unlock();

规则4：不要对同一把非递归锁加锁两次（会死锁，第七关讲）
```

---

## 第五关：信号量——比互斥锁更通用的同步工具

### 5.1 信号量是什么？

信号量（Semaphore）是一个**非负整数计数器**，支持两种原子操作：

```
P（sem_wait）：等待并减一
  如果计数 > 0：计数 -= 1，立即继续
  如果计数 = 0：阻塞，直到有人做 V 操作

V（sem_post）：加一并唤醒
  计数 += 1
  如果有线程在等待，唤醒其中一个
```

**互斥锁是信号量的特例**：初始值为 1 的信号量就是互斥锁（同时只允许一个线程进入）。

```c
#include <semaphore.h>

sem_t sem;
sem_init(&sem, 0, 1);    // 初始值 1，线程间共享（第二个参数 0）

sem_wait(&sem);    // P 操作（减一）
/* 临界区 */
sem_post(&sem);    // V 操作（加一）

sem_destroy(&sem);
```

### 5.2 信号量作为计数器：生产者-消费者问题

这是最经典的并发模式：

```
生产者线程：产生数据 → 放入缓冲区
消费者线程：从缓冲区取数据 → 处理

三个约束：
① 缓冲区满时，生产者必须等待（等空槽）
② 缓冲区空时，消费者必须等待（等数据）
③ 不能同时写缓冲区（竞态保护）
```

**用三个变量解决**：

```c
// bounded_buffer.c：容量为 N 的有界缓冲区
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#define N 5   // 缓冲区容量

int buf[N];
int in = 0, out = 0;   // 写入/读出位置（环形队列）

sem_t empty_slots;     // 空槽数（初始 = N，生产者等这个）
sem_t filled_slots;    // 已填槽数（初始 = 0，消费者等这个）
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;  // 保护 buf/in/out

void produce(int item) {
    sem_wait(&empty_slots);        // 等待有空槽（空槽 -1）
    pthread_mutex_lock(&mutex);
    buf[in] = item;
    in = (in + 1) % N;
    pthread_mutex_unlock(&mutex);
    sem_post(&filled_slots);       // 通知消费者有新数据（填槽 +1）
}

int consume() {
    sem_wait(&filled_slots);       // 等待有数据（填槽 -1）
    pthread_mutex_lock(&mutex);
    int item = buf[out];
    out = (out + 1) % N;
    pthread_mutex_unlock(&mutex);
    sem_post(&empty_slots);        // 通知生产者有空槽（空槽 +1）
    return item;
}

void *producer(void *arg) {
    for (int i = 0; i < 20; i++) {
        produce(i);
        printf("生产了: %d\n", i);
    }
    return NULL;
}

void *consumer(void *arg) {
    for (int i = 0; i < 20; i++) {
        int item = consume();
        printf("  消费了: %d\n", item);
    }
    return NULL;
}

int main() {
    sem_init(&empty_slots, 0, N);  // 初始有 N 个空槽
    sem_init(&filled_slots, 0, 0); // 初始没有数据

    pthread_t prod_tid, cons_tid;
    pthread_create(&prod_tid, NULL, producer, NULL);
    pthread_create(&cons_tid, NULL, consumer, NULL);
    pthread_join(prod_tid, NULL);
    pthread_join(cons_tid, NULL);

    sem_destroy(&empty_slots);
    sem_destroy(&filled_slots);
    return 0;
}
```

```bash
$ gcc -o bounded_buffer bounded_buffer.c -lpthread
$ ./bounded_buffer
生产了: 0
生产了: 1
  消费了: 0
生产了: 2
  消费了: 1
...（生产者和消费者交替，缓冲区不会溢出也不会空读）
```

---

## 第六关：条件变量——等待任意条件

### 6.1 信号量的局限

信号量只能等"计数"条件（如：槽位数 > 0）。有时需要等更复杂的条件，比如"等某个标志变为 true"、"等队列非空且队列里有高优先级任务"。

**条件变量（Condition Variable）** 允许线程等待**任意条件**成立。

```c
pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
```

**两个核心操作**：

```c
// 等待条件（必须在持锁时调用！）
pthread_cond_wait(&cond, &mutex);
// 内部三步：
//   1. 原子释放 mutex（让其他线程能修改共享状态）
//   2. 睡眠，等待 signal
//   3. 被唤醒后，重新加锁 mutex，返回

// 唤醒一个等待的线程
pthread_cond_signal(&cond);

// 唤醒所有等待的线程
pthread_cond_broadcast(&cond);
```

### 6.2 最重要的规则：用 `while`，不用 `if`

```c
// ❌ 错误写法：用 if
pthread_mutex_lock(&mutex);
if (队列为空) {
    pthread_cond_wait(&cond, &mutex);  // 被唤醒后直接往下走
}
item = dequeue();   // 此时队列可能还是空的！
pthread_mutex_unlock(&mutex);

// ✅ 正确写法：用 while（始终如一）
pthread_mutex_lock(&mutex);
while (队列为空) {              // 被唤醒后重新检查条件
    pthread_cond_wait(&cond, &mutex);
}
item = dequeue();   // 这次条件一定成立
pthread_mutex_unlock(&mutex);
```

**为什么必须用 `while`？有两个原因：**

1. **虚假唤醒（Spurious Wakeup）**：某些系统下，`cond_wait` 可能在没有 `signal` 的情况下自己醒来（POSIX 标准允许这种实现）
2. **条件被抢走**：多个线程等同一个条件，`broadcast` 唤醒了全部，但只有一个能拿到数据，其余醒来时条件已不成立

**用 `while` 能处理上面两种情况**，`if` 不行。这是 POSIX 多线程编程中最重要的惯用法之一。

### 6.3 完整示例：用条件变量实现一个任务队列

```c
// task_queue.c：简单任务队列，一个生产者，多个消费者
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_TASKS 100

int tasks[MAX_TASKS];
int task_count = 0;
int done = 0;   // 生产者完成标志

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  not_empty = PTHREAD_COND_INITIALIZER;

void submit_task(int val) {
    pthread_mutex_lock(&mutex);
    tasks[task_count++] = val;
    pthread_cond_signal(&not_empty);   // 通知等待的消费者
    pthread_mutex_unlock(&mutex);
}

int get_task(int *val) {
    pthread_mutex_lock(&mutex);
    while (task_count == 0 && !done)   // 注意：while 而不是 if
        pthread_cond_wait(&not_empty, &mutex);
    if (task_count == 0) {
        pthread_mutex_unlock(&mutex);
        return 0;   // 没有任务且 done，退出
    }
    *val = tasks[--task_count];
    pthread_mutex_unlock(&mutex);
    return 1;
}

void *worker(void *arg) {
    int id = *(int *)arg;
    int val;
    while (get_task(&val)) {
        printf("线程 %d 处理任务 %d\n", id, val);
        usleep(100000);   // 模拟处理耗时
    }
    return NULL;
}

int main() {
    int ids[3] = {1, 2, 3};
    pthread_t workers[3];
    for (int i = 0; i < 3; i++)
        pthread_create(&workers[i], NULL, worker, &ids[i]);

    for (int i = 0; i < 10; i++) {
        submit_task(i);
        usleep(50000);
    }

    pthread_mutex_lock(&mutex);
    done = 1;
    pthread_cond_broadcast(&not_empty);   // 唤醒所有工作线程，让它们检查 done
    pthread_mutex_unlock(&mutex);

    for (int i = 0; i < 3; i++)
        pthread_join(workers[i], NULL);
    printf("所有任务完成\n");
    return 0;
}
```

---

## 第七关：死锁——两个线程永远等待对方

### 7.1 最简单的死锁场景

```c
// 线程 A：
pthread_mutex_lock(&lock1);   // 拿到 lock1
// ... 被切走 ...
pthread_mutex_lock(&lock2);   // 等待 lock2（B 持有）

// 线程 B（同时运行）：
pthread_mutex_lock(&lock2);   // 拿到 lock2
// ... 被切走 ...
pthread_mutex_lock(&lock1);   // 等待 lock1（A 持有）
```

```
A 等 B 释放 lock2
B 等 A 释放 lock1
→ 两者永远等待，程序卡死！
```

### 7.2 死锁的四个必要条件（Coffman 条件）

死锁发生，**必须同时满足**以下四个条件，破坏任意一个即可预防：

| 条件 | 含义 | 怎么破坏 |
|------|------|---------|
| **互斥** | 资源同时只能被一个线程持有 | 使用无锁数据结构（复杂，不常用）|
| **持有并等待** | 持有资源的同时等待其他资源 | 一次性申请所有锁（不常用）|
| **不可抢占** | 资源只能由持有者主动释放 | 使用 try_lock（不常用）|
| **循环等待** | 存在等待环路 A→B→A | **✅ 固定加锁顺序（最常用！）**|

### 7.3 修复死锁：固定加锁顺序

```c
// ❌ 死锁版本：两个线程加锁顺序不同
void *thread_a(void *arg) {
    pthread_mutex_lock(&lock1);   // 先 lock1
    pthread_mutex_lock(&lock2);   // 后 lock2
    /* ... */
    pthread_mutex_unlock(&lock2);
    pthread_mutex_unlock(&lock1);
    return NULL;
}

void *thread_b(void *arg) {
    pthread_mutex_lock(&lock2);   // 先 lock2（顺序和A相反！）
    pthread_mutex_lock(&lock1);   // 后 lock1
    /* ... */
    pthread_mutex_unlock(&lock1);
    pthread_mutex_unlock(&lock2);
    return NULL;
}

// ✅ 修复版本：所有线程按相同顺序加锁
void *thread_b_fixed(void *arg) {
    pthread_mutex_lock(&lock1);   // 和 A 一样：先 lock1
    pthread_mutex_lock(&lock2);   // 后 lock2
    /* ... */
    pthread_mutex_unlock(&lock2);
    pthread_mutex_unlock(&lock1);
    return NULL;
}
```

**工程规则**：给所有互斥锁编号，任何线程都必须**按编号从小到大申请锁**，消除循环等待。

### 7.4 动手复现死锁（然后修复它）

```c
// deadlock_demo.c：能稳定复现的死锁
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t lock1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock2 = PTHREAD_MUTEX_INITIALIZER;

void *thread_a(void *arg) {
    pthread_mutex_lock(&lock1);
    printf("A：拿到 lock1，sleep 1 秒...\n");
    sleep(1);   // 给 B 时间拿到 lock2
    printf("A：等待 lock2...\n");
    pthread_mutex_lock(&lock2);   // ← 永远等待
    printf("A：拿到 lock2（这行不会打印）\n");
    pthread_mutex_unlock(&lock2);
    pthread_mutex_unlock(&lock1);
    return NULL;
}

void *thread_b(void *arg) {
    pthread_mutex_lock(&lock2);
    printf("B：拿到 lock2，sleep 1 秒...\n");
    sleep(1);
    printf("B：等待 lock1...\n");
    pthread_mutex_lock(&lock1);   // ← 永远等待
    printf("B：拿到 lock1（这行不会打印）\n");
    pthread_mutex_unlock(&lock1);
    pthread_mutex_unlock(&lock2);
    return NULL;
}

int main() {
    pthread_t ta, tb;
    pthread_create(&ta, NULL, thread_a, NULL);
    pthread_create(&tb, NULL, thread_b, NULL);
    pthread_join(ta, NULL);   // 主线程也卡在这里
    pthread_join(tb, NULL);
    printf("完成（永远不会打印）\n");
    return 0;
}
```

```bash
$ gcc -o deadlock_demo deadlock_demo.c -lpthread && ./deadlock_demo
A：拿到 lock1，sleep 1 秒...
B：拿到 lock2，sleep 1 秒...
A：等待 lock2...
B：等待 lock1...
（程序卡死，用 Ctrl+C 终止）
```

**修复任务**：把 `thread_b` 里的加锁顺序改为先 `lock1` 后 `lock2`，重新编译，验证死锁消失。

---

## 第八关：线程安全——哪些函数可以放心用？

### 8.1 什么是线程安全？

**线程安全函数**：被多个并发线程同时调用时，总能产生正确结果。

**四类线程不安全函数**（要特别注意！）：

| 类别 | 典型函数 | 问题 | 解决方法 |
|------|---------|------|---------|
| 1. 不保护共享变量 | 自写的无锁计数器 | 数据竞争 | 加锁 |
| 2. 跨调用保持状态 | `strtok`（用静态缓冲区）| 线程间互相干扰 | 用 `strtok_r` |
| 3. 返回静态存储区指针 | `gethostbyname`, `ctime` | 静态缓冲区被覆盖 | 用 `_r` 后缀版本 |
| 4. 调用不安全函数 | 封装了上述函数的函数 | 传递不安全性 | 替换底层调用 |

**常用替换对照表**：

```c
// ❌ 不安全                       ✅ 线程安全替代
strtok(s, delim)              → strtok_r(s, delim, &saveptr)
gethostbyname(name)           → getaddrinfo(...)          // 第11章用过！
rand()                        → rand_r(&seed)
ctime(&t)                     → ctime_r(&t, buf)
```

### 8.2 可重入函数：最强的线程安全

**可重入函数（Reentrant）**：不访问任何共享数据（不用全局变量，也不用静态局部变量），只用参数和局部变量。

```c
// ✅ 可重入：只用参数和局部变量
long factorial(int n) {
    long result = 1;
    for (int i = 2; i <= n; i++)
        result *= i;
    return result;   // 无共享状态，任意多线程同时调用都安全
}

// ❌ 不可重入：使用静态局部变量
int next_id() {
    static int counter = 0;   // 静态变量，所有线程共享！
    return ++counter;          // 数据竞争！
}

// ✅ 改为线程安全
int next_id_safe() {
    static int counter = 0;
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    int id = ++counter;
    pthread_mutex_unlock(&m);
    return id;
}
```

**三个概念的关系**：可重入 ⊂ 线程安全 ⊂ 一般函数。可重入是最强的保证（无需加锁），线程安全是用锁保护的（有加锁开销），线程不安全是都没有。

---

## 第九关：线程池——工程实践中的常用模式

### 9.1 为什么不能"每请求一线程"？

第11章的多线程服务器，每来一个客户端就 `pthread_create`：

```
问题：
① pthread_create 有开销（约 10~100 微秒），突发 1000 个请求 = 1000 次创建
② 1000 个并发线程 = 大量内存（每线程默认栈 8MB → 共 8GB！）
③ 内核调度 1000 个线程的开销极大
```

**线程池的思路**：预先创建固定数量的工作线程，用任务队列分发工作：

```
主线程（生产者）：              工作线程池（消费者，固定数量）：
accept() 新连接                while(1) {
→ 把 connfd 放入任务队列           task = 等待任务队列
                                  handle_client(task.connfd)
                               }
```

### 9.2 完整线程池实现

```c
// thread_pool.c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define POOL_SIZE  4    // 工作线程数
#define QUEUE_SIZE 64   // 任务队列容量

typedef struct {
    void (*func)(void *);  // 任务函数
    void *arg;             // 任务参数
} task_t;

// 环形任务队列
task_t queue[QUEUE_SIZE];
int head = 0, tail = 0, count = 0;
int shutdown = 0;   // 关闭标志

pthread_mutex_t mutex    = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t  not_full  = PTHREAD_COND_INITIALIZER;

// 提交任务（生产者调用）
void pool_submit(void (*func)(void *), void *arg) {
    pthread_mutex_lock(&mutex);
    while (count == QUEUE_SIZE)               // 队列满则等待
        pthread_cond_wait(&not_full, &mutex);
    queue[tail].func = func;
    queue[tail].arg  = arg;
    tail = (tail + 1) % QUEUE_SIZE;
    count++;
    pthread_cond_signal(&not_empty);          // 通知工作线程
    pthread_mutex_unlock(&mutex);
}

// 工作线程（消费者）
void *worker(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex);
        while (count == 0 && !shutdown)       // 队列空则等待
            pthread_cond_wait(&not_empty, &mutex);
        if (shutdown && count == 0) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        task_t task = queue[head];
        head = (head + 1) % QUEUE_SIZE;
        count--;
        pthread_cond_signal(&not_full);       // 通知提交者有空位了
        pthread_mutex_unlock(&mutex);

        task.func(task.arg);                  // 执行任务（锁外执行！）
    }
    return NULL;
}

// 示例任务
void print_task(void *arg) {
    int n = *(int *)arg;
    printf("工作线程 %lu 处理任务 %d\n",
           (unsigned long)pthread_self() % 10000, n);
    free(arg);
}

int main() {
    pthread_t workers[POOL_SIZE];
    for (int i = 0; i < POOL_SIZE; i++)
        pthread_create(&workers[i], NULL, worker, NULL);

    for (int i = 0; i < 20; i++) {
        int *arg = malloc(sizeof(int));
        *arg = i;
        pool_submit(print_task, arg);
    }

    // 等所有任务执行完后关闭线程池
    pthread_mutex_lock(&mutex);
    shutdown = 1;
    pthread_cond_broadcast(&not_empty);  // 唤醒所有工作线程检查 shutdown
    pthread_mutex_unlock(&mutex);

    for (int i = 0; i < POOL_SIZE; i++)
        pthread_join(workers[i], NULL);

    printf("所有任务完成\n");
    return 0;
}
```

```bash
$ gcc -o thread_pool thread_pool.c -lpthread && ./thread_pool
工作线程 1234 处理任务 0
工作线程 5678 处理任务 1
工作线程 9012 处理任务 2
工作线程 1234 处理任务 3    ← 4 个线程并行处理，同一线程可复用
...
所有任务完成
```

---

## 第十关：调试并发 Bug 的工具

### 10.1 ThreadSanitizer（TSan）：自动检测数据竞争

这是最有用的工具，**编译时加一个选项就能用**：

```bash
$ gcc -g -fsanitize=thread -o race_demo race_demo.c -lpthread
$ ./race_demo
```

如果有数据竞争，会立刻报告：

```
==================
WARNING: ThreadSanitizer: data race (pid=12345)
  Write of size 8 at 0x000104b4a080 by thread T2:
    #0 increment race_demo.c:10

  Previous write of size 8 at 0x000104b4a080 by thread T1:
    #0 increment race_demo.c:10
==================
```

**TSan 几乎能抓出所有数据竞争**，开发阶段强烈建议开启（性能有下降，只在测试时用）。

### 10.2 gdb：调试死锁

```bash
# 程序卡死时，用 gdb attach 进去
$ gdb ./deadlock_demo $(pgrep deadlock_demo)
(gdb) info threads           # 列出所有线程
(gdb) thread apply all bt    # 打印所有线程的调用栈
```

如果所有线程都卡在 `pthread_mutex_lock`，基本就是死锁。根据调用栈可以看出每个线程在等哪把锁。

### 10.3 Helgrind（Valgrind 插件）

```bash
$ valgrind --tool=helgrind ./race_demo
# 输出更详细的竞争分析和加锁历史
```

比 TSan 慢，但报告更详细（包括历史锁信息）。

---

## 综合练习：用线程池实现并发 Echo 服务器

把第11章的 Echo 服务器和本章的线程池结合起来：

```c
// concurrent_echo.c：线程池驱动的并发 Echo 服务器
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT      8888
#define BUFSIZE   4096
#define POOL_SIZE 4
#define QUEUE_CAP 64

/* ---- 复用第11章的 write_all ---- */
ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf; size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; left -= w;
    }
    return (ssize_t)n;
}

/* ---- 线程池（复用第九关代码）---- */
typedef struct { void (*func)(void *); void *arg; } task_t;

task_t   tq_buf[QUEUE_CAP];
int      tq_head = 0, tq_tail = 0, tq_count = 0, tq_done = 0;
pthread_mutex_t tq_mu  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tq_ne  = PTHREAD_COND_INITIALIZER;
pthread_cond_t  tq_nf  = PTHREAD_COND_INITIALIZER;

void pool_submit(void (*f)(void *), void *arg) {
    pthread_mutex_lock(&tq_mu);
    while (tq_count == QUEUE_CAP) pthread_cond_wait(&tq_nf, &tq_mu);
    tq_buf[tq_tail].func = f; tq_buf[tq_tail].arg = arg;
    tq_tail = (tq_tail + 1) % QUEUE_CAP; tq_count++;
    pthread_cond_signal(&tq_ne);
    pthread_mutex_unlock(&tq_mu);
}

void *worker(void *_) {
    while (1) {
        pthread_mutex_lock(&tq_mu);
        while (tq_count == 0 && !tq_done) pthread_cond_wait(&tq_ne, &tq_mu);
        if (tq_count == 0) { pthread_mutex_unlock(&tq_mu); break; }
        task_t t = tq_buf[tq_head];
        tq_head = (tq_head + 1) % QUEUE_CAP; tq_count--;
        pthread_cond_signal(&tq_nf);
        pthread_mutex_unlock(&tq_mu);
        t.func(t.arg);
    }
    return NULL;
}

/* ---- Echo 任务 ---- */
void echo_task(void *arg) {
    int connfd = *(int *)arg;
    free(arg);

    char buf[BUFSIZE]; ssize_t n;
    while ((n = read(connfd, buf, sizeof(buf))) > 0)
        write_all(connfd, buf, n);
    close(connfd);
}

int main() {
    /* 启动线程池 */
    pthread_t workers[POOL_SIZE];
    for (int i = 0; i < POOL_SIZE; i++)
        pthread_create(&workers[i], NULL, worker, NULL);

    /* 启动服务器 */
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(listenfd, 128);
    printf("并发 Echo 服务器启动（%d 个工作线程），端口 %d\n", POOL_SIZE, PORT);

    while (1) {
        int *connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, NULL, NULL);
        if (*connfdp < 0) { free(connfdp); continue; }
        pool_submit(echo_task, connfdp);  // 提交给线程池
    }
}
```

```bash
$ gcc -O2 -o concurrent_echo concurrent_echo.c -lpthread
$ ./concurrent_echo
并发 Echo 服务器启动（4 个工作线程），端口 8888

# 同时开多个客户端（多个终端）：
$ nc localhost 8888   # 每个 nc 都能立即得到回显，互不阻塞
```

**用 TSan 验证没有竞态条件**：

```bash
$ gcc -g -fsanitize=thread -o concurrent_echo_tsan concurrent_echo.c -lpthread
$ ./concurrent_echo_tsan
# 运行一段时间，观察有没有 WARNING 报出来
```

---

## 自测：你掌握了吗？

**第1关**：两个线程各做 100 万次 `counter++`，结果为什么不是 200 万？请用"三步汇编"解释。
<details><summary>答案</summary>
counter++ 编译为三步：LOAD（读到寄存器）→ ADD（加1）→ STORE（写回内存）。线程A执行完LOAD和ADD，还没STORE时，线程B完整执行了三步把counter写为6。线程A再STORE也写6，相当于B那次加法白做了。两线程各100万次，丢失的次数不确定，取决于切换时机。
</details>

**第2关**：`pthread_cond_wait` 为什么必须在 `while` 循环里，而不能用 `if`？
<details><summary>答案</summary>
两个原因：1）虚假唤醒（Spurious Wakeup）：POSIX 允许系统在没有 signal 的情况下自己唤醒线程；2）条件被其他线程抢走：broadcast 唤醒了多个线程，但只有一个能拿到资源，其余醒来时条件已不成立。用 while 重新检查条件可以处理这两种情况。
</details>

**第3关**：死锁的四个必要条件是什么？工程上最常用哪种方法预防？
<details><summary>答案</summary>
互斥、持有并等待、不可抢占、循环等待。工程上最常用的是破坏"循环等待"：规定所有线程必须按全局一致的顺序申请锁（如按编号从小到大）。
</details>

**第4关**：`strtok` 为什么是线程不安全的？应该用什么替代？
<details><summary>答案</summary>
strtok 内部使用静态局部变量保存上次分割的位置（下一次调用的起点）。多个线程并发调用会互相覆盖这个静态变量，导致分割结果错误。应该用 strtok_r，它需要调用者传入 saveptr 指针保存状态，每个线程用自己的 saveptr，互不干扰。
</details>

**第5关**：线程池相比"每连接一个线程"有什么优势？
<details><summary>答案</summary>
① 避免频繁 pthread_create 的时间开销；② 控制最大并发线程数，防止内存耗尽（每线程默认栈 8MB）；③ 工作线程复用，减少创建/销毁开销；④ 通过任务队列可以做背压控制（队列满时阻塞提交），防止突发流量压垮系统。
</details>

---

## 3.5 小时学习路径

| 时间段 | 内容 | 动手任务 |
|--------|------|---------|
| 0–20 分钟 | 第一、二关 | 编译运行 `hello_threads.c`，观察顺序不确定性 |
| 20–50 分钟 | **第三关** | 编译 `race_demo.c`，**反复运行**，观察每次结果不同 |
| 50–80 分钟 | **第四关** | 把 `race_demo.c` 改成 `race_fixed.c`，验证结果稳定为 2000000 |
| 80–110 分钟 | **第五关** | 编译运行 `bounded_buffer.c`，观察生产消费交替 |
| 110–135 分钟 | 第六关 | 理解 while 规则，编译运行 `task_queue.c` |
| 135–165 分钟 | **第七关** | 运行 `deadlock_demo.c` 看死锁，然后修复它 |
| 165–185 分钟 | 第八关 | 查找自己代码里有没有用到线程不安全函数，替换 |
| 185–200 分钟 | 第九关 | 编译运行 `thread_pool.c` |
| 200–210 分钟 | **综合练习 + 自测** | 编译 `concurrent_echo.c`，用 TSan 验证，完成自测 |
