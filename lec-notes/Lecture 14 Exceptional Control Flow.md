# 📘 课堂笔记：异常控制流（ECF）  
## —— 异常、进程与信号机制

---

## 第一部分：异常与进程（14-ecf-procs.pdf）

### 1. 异常控制流（ECF）简介
- **ECF（Exceptional Control Flow）** 是系统对“异常事件”（如错误、外部中断等）做出响应的机制。
- 它存在于系统的所有层级：
  - 硬件级别：中断/异常（Exceptions）
  - OS 内核：上下文切换（Context Switch）
  - 应用层：信号（Signals）和非局部跳转（setjmp/longjmp）

### 2. 异常（Exceptions）
- **定义**：由处理器状态变化（如除零、非法指令）或外部事件（如键盘中断）触发的内核转跳。
- 分为三类：
  - **Traps（陷阱）**：有意触发（如系统调用 syscall），执行完后返回下一条指令。
  - **Faults（故障）**：意外但可恢复（如缺页 Page Fault），可能重试当前指令。
  - **Aborts（终止）**：严重错误（如非法指令），直接终止程序。

> 举例：访问非法内存 → 触发 Page Fault → 若地址无效 → 内核发送 SIGSEGV → 程序崩溃。

### 3. 进程（Process）
- **进程 = 正在运行的程序实例**。
- 提供两个关键抽象：
  - **逻辑控制流（Logical Control Flow）**：看似独占 CPU（通过上下文切换实现并发）。
  - **私有地址空间（Private Address Space）**：看似独占内存（通过虚拟内存实现）。

### 4. 进程控制
#### (1) 创建进程：`fork()`
- **特点**：调用一次，返回两次。
  - 子进程返回 0
  - 父进程返回子进程 PID
- 子进程几乎完全复制父进程（地址空间、打开的文件描述符等），但拥有独立 PID。

```c
// fork 示例
int main() {
    pid_t pid;
    int x = 1;
    pid = fork(); // 调用一次
    if (pid == 0) {          // 子进程
        printf("child: x=%d\n", ++x);
        exit(0);
    }
    printf("parent: x=%d\n", --x); // 父进程
    exit(0);
}
// 输出（顺序不确定）：
// parent: x=0
// child: x=2
```

#### (2) 终止进程
- 三种方式：
  - 从 `main` 返回
  - 调用 `exit(int status)`
  - 收到终止信号（如 SIGINT）

#### (3) 回收子进程（Reaping）
- 子进程终止后若未被回收 → 成为 **僵尸进程（Zombie）**，仍占用内核资源。
- 父进程调用 `wait()` 或 `waitpid()` 回收子进程，获取其退出状态。

```c
// wait 示例
void fork9() {
    if (fork() == 0) {
        printf("HC: hello from child\n");
        exit(0);
    } else {
        printf("HP: hello from parent\n");
        int status;
        wait(&status); // 阻塞等待子进程
        printf("CT: child has terminated\n");
    }
    printf("Bye\n");
}
```

#### (4) 加载新程序：`execve()`
- 用新程序**替换当前进程的代码、数据、堆栈**，但保留 PID、打开文件、信号上下文。
- 调用后**不返回**（除非出错）。

```c
if ((pid = fork()) == 0) {
    // 子进程执行 /bin/ls -lt /usr/include
    char *myargv[] = {"/bin/ls", "-lt", "/usr/include", NULL};
    if (execve(myargv[0], myargv, environ) < 0) {
        printf("Command not found.\n");
        exit(1);
    }
}
```

---

## 第二部分：信号与非局部跳转（15-ecf-signals.pdf）

### 1. 为什么需要信号？
- 简单 Shell 无法回收**后台作业（background jobs）** → 成为僵尸。
- **解决方案**：使用 **信号（Signals）** 通知父进程子进程已终止。

### 2. 信号基础
- **信号 = 小整数 ID（1~30）**，表示某类事件。
- 常见信号：
  - `SIGINT (2)`：Ctrl-C
  - `SIGKILL (9)`：强制终止（不可捕获/忽略）
  - `SIGSEGV (11)`：段错误
  - `SIGCHLD (17)`：子进程终止或停止（默认忽略）

### 3. 信号的发送与接收
- **发送**：
  - 内核自动发送（如子进程退出）
  - 进程调用 `kill(pid, sig)` 或使用 `/bin/kill`
  - 键盘：Ctrl-C → SIGINT；Ctrl-Z → SIGTSTP
- **接收**：
  - 忽略（`SIG_IGN`）
  - 终止（默认）
  - **捕获（Catch）**：执行用户定义的 **信号处理函数（signal handler）**

```c
// SIGINT 处理示例（不安全！）
void sigint_handler(int sig) {
    printf("So you think you can stop the bomb with ctrl-c?\n");
    sleep(2);
    printf("OK. :-)\n");
    exit(0);
}
int main() {
    signal(SIGINT, sigint_handler);
    pause(); // 等待信号
}
```

⚠️ **问题**：`printf`、`exit` 等**不是 async-signal-safe** 函数，不能在 handler 中安全使用！

### 4. 安全信号处理准则（G0–G5）
- **G0**：handler 尽量简单（如只设置全局 flag）
- **G1**：只调用 **async-signal-safe** 函数（如 `write`, `_exit`, `kill`）
  - ❌ 不安全：`printf`, `malloc`, `exit`
- **G2**：在 handler 入口/出口保存/恢复 `errno`
- **G3**：访问共享数据时临时阻塞信号
- **G4/G5**：全局变量用 `volatile`；标志变量用 `volatile sig_atomic_t`

> 替代方案：使用 CS:APP 提供的 **SIO（Safe I/O）库**，如 `Sio_puts()`、`Sio_putl()`

```c
// 安全的 SIGINT 处理
void sigint_handler(int sig) {
    Sio_puts("So you think...\n");
    sleep(2);
    Sio_puts("OK. :-)\n");
    _exit(0); // 注意：用 _exit() 而非 exit()
}
```

### 5. 信号的阻塞与待处理
- 每个进程有：
  - `pending` 位图：哪些信号已发送但未处理
  - `blocked` 位图（信号掩码）：哪些信号被阻塞（通过 `sigprocmask()` 设置）
- **关键特性**：**信号不排队**！同一类型信号多次发送 → 只保留一个 pending。

> 例：多个子进程同时退出 → SIGCHLD 只 pending 一次 → handler 必须用 `while(wait(...))` 回收所有子进程！

```c
// 正确的 SIGCHLD handler
void child_handler(int sig) {
    pid_t pid;
    while ((pid = waitpid(-1, NULL, 0)) > 0) {
        ccount--;
        Sio_puts("Reaped child ");
        Sio_putl((long)pid);
        Sio_puts("\n");
    }
}
```

### 6. 竞态条件与同步
- **问题**：父进程在 `fork()` 后立即 `addjob()`，但子进程可能先执行 → job list 尚未添加就收到 SIGCHLD！
- **解决方案**：在 `fork()` 前阻塞 SIGCHLD，子进程解除阻塞，父进程 addjob 后再解除。

```c
// procmask2.c：避免 race
Sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // 阻塞 SIGCHLD
if ((pid = Fork()) == 0) {
    Sigprocmask(SIG_SETMASK, &prev_one, NULL); // 子进程解除
    Execve(...);
}
addjob(pid); // 父进程安全添加
Sigprocmask(SIG_SETMASK, &prev_one, NULL); // 父进程解除
```

### 7. 等待信号：`sigsuspend()`
- 替代忙等待（`while(!flag);`）的高效方式。
- 原子地：**解除阻塞 + 进入睡眠**，直到信号到达。

```c
Sigprocmask(SIG_BLOCK, &mask, &prev); // 阻塞 SIGCHLD
pid = 0;
while (!pid)
    Sigsuspend(&prev); // 原子解除阻塞并等待信号
```

### 8. 非局部跳转：`setjmp` / `longjmp`
- 允许从深层函数直接跳回上层（绕过正常 call/return）。
- 常用于错误恢复。

```c
jmp_buf buf;

void foo() {
    if (error1) longjmp(buf, 1);
    bar();
}
void bar() {
    if (error2) longjmp(buf, 2);
}

int main() {
    switch (setjmp(buf)) {
        case 0: foo(); break;
        case 1: printf("error1\n"); break;
        case 2: printf("error2\n"); break;
    }
}
```

> ⚠️ 限制：只能跳转到**尚未返回**的函数栈帧中。

---

## 总结
| 机制 | 作用 | 关键点 |
|------|------|--------|
| **异常** | 硬件/OS 响应事件 | Traps/Faults/Aborts |
| **进程** | 程序运行实例 | fork/execve/wait |
| **信号** | 进程间异步通知 | handler 安全性、阻塞、不排队 |
| **非局部跳转** | 异常控制流 | setjmp/longjmp，注意栈有效性 |

> 掌握 ECF 是理解 Shell、服务器、并发程序的基础！

--- 

如需对某一部分深入展开（如 `sigaction` vs `signal`、进程组等），欢迎继续提问！