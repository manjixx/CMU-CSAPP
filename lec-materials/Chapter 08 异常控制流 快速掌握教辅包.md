# 第8章 异常控制流——从零开始手把手学

> **适合人群**：已掌握 C 语言基础、了解进程概念的同学  
> **学完你能做到**：解释"按下 Ctrl-C 时发生了什么"，能用 fork/exec/waitpid 写一个简单 Shell，能正确安装信号处理函数，能用 setjmp/longjmp 实现 C 语言的错误恢复  
> **预计时间**：认真读 + 动手练，约 3 小时


```text
第一关：ECF 是什么          → 建立全局观，说明它不只是“异常”
第二关：硬件/OS、OS/应用 交界 ECF     → 异常（Interrupt/Trap/Fault/Abort）
第三关：OS 层 ECF（进程）    → 上下文切换、fork/exec/waitpid
第四关：OS 层 ECF（信号）    → 异步通知、handler
第五关：应用层 ECF           → setjmp/longjmp
```

---

## 第一关：为什么需要 ECF？（先建直觉）

### 1.1 正常程序，只会"往前走"

在没有 ECF 之前，一个程序的执行流就像一条直线：

```
main() {
    读输入 → 计算 → 输出结果 → 退出
}
PC：地址100 → 104 → 108 → 112 → ...（顺序或分支跳转，始终由程序代码决定方向）
```

程序只能响应**它自己代码里写好的逻辑**。但真实世界里，有一些事情发生在程序"意识"之外：

### 1.2 来自程序之外的事件

```
你的程序正在运行...
                ← 用户按了 Ctrl-C（键盘产生中断）
                ← 磁盘读取完成了（I/O 完成中断）
                ← 时钟芯片每 10ms 触发一次（定时器中断）
                ← 当前指令发生了除以零（算术错误）
                ← 访问了未映射的虚拟地址（缺页故障）
```

程序的代码里**根本没有任何地方**处理这些事件——**但系统必须能响应它们（程序正常控制流之外的事件响应机制）**。

**ECF（Exceptional Control Flow，异常控制流）** 就是这个机制的总称：**当特殊事件发生时，强制改变正在执行的控制流，跳转到对应的处理代码**。

### 1.3 ECF 在系统各层次中都存在

```
┌──────────────────────────────────────────────────────┐
│  应用层：非局部跳转（setjmp / longjmp）                 │  ← C 运行库实现
├──────────────────────────────────────────────────────┤
│  OS 层：进程上下文切换、信号（Signals）                  │  ← OS 软件实现
├──────────────────────────────────────────────────────┤
│  硬件/OS 交界：异常（Exceptions）                       │  ← 硬件 + OS 协同
└──────────────────────────────────────────────────────┘
         ↑ 越底层越接近硬件，越顶层越接近应用程序员
```

本章从底往上学，先搞懂底层的"异常"，再理解建立在它之上的进程、信号、非局部跳转。

### 1.4 ECF 的准确含义与分层

ECF 是一个**总称**，指“所有正常控制流之外的控制流”。它不等于狭义的 Exception（异常）。Exception 只是 ECF 中偏底层的一种，发生在硬件/OS 交界处。

ECF 包含：

- **异常（Interrupt / Trap / Fault / Abort）**：硬件 + OS 协同
- **进程上下文切换**：OS 调度
- **信号（Signals）**：OS 异步通知
- **非局部跳转（setjmp / longjmp）**：C 运行库，纯用户态

它既包含**被动兜底**（缺页、Ctrl-C、子进程退出），也包含**主动请求**（系统调用、`fork`、`execve`、`longjmp`）。

更精确的分层可以看成：

```text
用户态
  应用代码
  C 运行库：
    - setjmp/longjmp：应用层 ECF，纯用户态保存/恢复寄存器
    - stdio、malloc：普通库函数
  系统调用包装：glibc 的 open/read/write → syscall 指令
    → 触发 Trap 异常，属于 ECF 的异常层
────────────────────────────────────────────── 用户态/内核态边界
内核态
  系统调用入口、内核子系统：VFS、文件系统、设备驱动、进程调度、信号、异常处理
────────────────────────────────────────────── 硬件/OS 边界
硬件
  CPU 异常/中断、MMU、设备
```

`setjmp/longjmp` 是纯用户态实现，**不经过内核，也不依赖系统调用**。原因是：CPU 通用寄存器（PC、SP、BP、被调用者保存寄存器等）在用户态可以直接读写；只有控制寄存器、I/O 端口、页表基址等才需要内核特权。

- `setjmp(env)`：把当前寄存器快照保存到 `jmp_buf` 里。
- `longjmp(env, val)`：把快照加载回寄存器，并跳回 `setjmp` 处。
- 这些操作都是普通用户态指令，不涉及 `syscall`。

对比：

| | setjmp/longjmp | 进程上下文切换 | 信号处理 |
|---|---|---|---|
| 谁实现 | C 运行库，纯用户态 | 内核 | 内核 + 用户态处理函数 |
| 是否进内核 | 否 | 是 | 是 |
| 保存什么 | 部分通用寄存器 | 完整 CPU 状态 + 地址空间 + 内核栈等 | 内核保存现场，再调用用户处理函数 |
| 作用范围 | 同一进程内跳转 | 不同进程/线程之间切换 | 异步通知 + 用户态响应 |

---


### 1.5 学 ECF 有什么用？

1. **I/O、进程、虚拟内存都依赖 ECF**：你觉得"无关紧要"的东西其实是基础中的基础
2. **理解系统调用**：应用程序与 OS 通信的唯一正规入口（Trap）就是异常的一种，read / write 等系统调用的本质，就是通过执行 syscall 指令触发一次 Trap 异常。
3. **能写系统类工具**：Shell、Web 服务器的核心都是 fork/exec/wait/signal
4. **理解并发**：信号、线程、中断处理本质上是并发的表现形式
5. **理解高级语言异常**：Java/C++ 的 try-catch-throw 底层依赖非局部跳转

---

## 第二关：异常——硬件与 OS 的接力

### 2.1 "异常"不等于"错误"

初学者常把"异常"理解为"程序出错了"。在 CSAPP 中，**Exception（异常）** 的含义更宽泛：

> **任何导致 CPU 跳转到 OS 处理代码的事件，都叫异常**

按触发方式和返回行为，分为 4 类：

| 类型 | 触发原因 | 同步/异步 | 处理完后返回到哪里 | 粗粒度理解 | Java 类比 | 说明 |
|---|---|---|---|---|---|---|
| **Interrupt** | I/O 设备信号，如网卡、键盘、定时器 | 异步 | 下一条指令（程序通常无感） | **正常行为** | 类似事件回调 / 异步通知 | 来自外部，与当前指令无关，不是程序错误 |
| **Trap** | 主动执行 `syscall`，如 `read/write/fork` | 同步 | 下一条指令 | **正常行为** | 类似主动调用系统 API | 程序主动请求 OS 服务，完全正常 |
| **Fault** | 潜在可恢复的错误，如缺页、段错误 | 同步 | 重新执行出错的那条指令 | **一半正常，一半错误** | 类似可恢复的 `Exception` | 缺页是正常机制，OS 补页后重试；非法访问才是错误 |
| **Abort** | 不可恢复的致命错误，如硬件故障 | 同步 | 不返回，进程终止 | **错误/致命** | 类似 `Error`，不可恢复 | 无法挽救，直接杀死进程 |

**一句话总结：**

- **Interrupt + Trap**：不是错误，是正常控制流的一部分。  
- **Fault**：要分情况，缺页正常，段错误才是异常。  
- **Abort**：真正的不可恢复错误。  
- **Java 类比**：Fault ≈ `Exception`，Abort ≈ `Error`，Interrupt/Trap 不是异常，是正常机制。

**记忆口诀**：中（Interrupt）陷（Trap）故（Fault）终（Abort）— 异步无感、主动服务、可修重试、不可挽救

### 2.2 每种类型一个具体例子

**Interrupt（中断）— 异步，来自外部**：
```
你的程序正在执行第 1000 条指令 ...
  → 网卡接收到数据包，拉高了 CPU 的中断引脚
  → CPU 执行完第 1000 条指令后，检测到中断
  → 跳转到"网卡中断处理程序"（内核代码）
  → 处理完成，返回执行第 1001 条指令
你的程序完全不知道这件事发生过！
```

**Trap（陷阱）— 主动触发，系统调用就是这个**：
```c
// 你写的代码：
ssize_t n = read(fd, buf, 1024);

// 背后发生的事：
// ① 参数放入寄存器：%rdi=fd, %rsi=buf, %rdx=1024
// ② 系统调用号 0（read）放入 %rax
// ③ 执行 syscall 指令 → 触发 Trap
// ④ CPU 切换到内核态，执行 read 的内核实现
// ⑤ 读取完成，返回用户态，n = 读取字节数
```

**Fault（故障）— 可修复，缺页是典型**：
```
程序第一次访问某个虚拟地址（对应页在磁盘上）...
  → 触发 Page Fault（缺页故障）
  → OS 从磁盘把对应数据加载到物理内存
  → 更新页表，返回，重新执行那条内存访问指令
  → 这次成功！（对程序透明，感觉不到任何延迟）
```

**Fault（故障）— 也可能修复失败，段错误是典型：**
```
程序访问了一个非法地址（如空指针解引用）...
  → 触发 Page Fault / General Protection Fault
  → OS 检查页表，发现这个地址根本不合法，无法映射
  → 无法修复 → OS 向进程发送 SIGSEGV
  → 进程被终止（core dump）
```

**Abort（终止）— 无法恢复**：
```
内存检测到 DRAM 位翻转（硬件故障）
  → 触发 Abort
  → 进程直接被杀死，没有商量余地
```

### 2.3 异常表：OS 维护的"紧急联系册"

系统启动时，OS 在内存中建立**异常表（Exception Table）**，每个**异常号对应一个处理程序地址**：

```
异常表（Exception Table）：
┌──────┬─────────────────────────────────────┐
│  0   │ 除法错误处理程序的地址（Fault）          │
│  13  │ 一般保护故障处理程序的地址（Fault）       │
│  14  │ 缺页处理程序的地址（Fault）              │  ← Page Fault Handler
│  18  │ 机器检查处理程序的地址（Abort）           │
│ ...  │ ...                                   │
│  0   │ read 系统调用处理程序（Trap，Linux约定） │
│ ...  │ ...                                   │
└──────┴─────────────────────────────────────┘
         索引 = 异常号（exception number）
```

异常触发流程：
```
① CPU 检测到事件发生
② 确定对应的异常号 k
③ 查异常表第 k 项，取出处理程序地址
④ 跳转过去（CPU 自动切换到内核态，压入返回地址等状态）
⑤ 处理程序执行完毕，根据类型决定返回到哪里
```

**四类异常在异常表中的典型例子**（x86 为例）：

| 向量号 | 名称 | 类型 | 说明 |
|---|---|---|---|
| 0 | Divide Error | Fault | 除以零 |
| 3 | Breakpoint | Trap | `int3` 断点 |
| 6 | Invalid Opcode | Fault | 非法指令 |
| 13 | General Protection | Fault | 段错误、权限违规 |
| 14 | Page Fault | Fault | 缺页，可能可修复，也可能不可修复 |
| 18 | Machine Check | Abort | 硬件致命故障 |
| 32 | Timer Interrupt | Interrupt | 时钟中断 |
| 33 | Keyboard Interrupt | Interrupt | 键盘中断 |
| 0x80（32 位） | System Call | Trap | Linux 传统 `int 0x80` 系统调用入口 |
| —（`syscall` 指令） | System Call | Trap | x86-64 Linux，用 `syscall` 指令进入 |

**注意**：异常表中的“异常号/向量号”和“系统调用号”是两回事。  
- 异常向量号：用于查异常表，决定跳到哪个处理程序。  
- 系统调用号：如 `read=0`、`write=1`，放在 `%rax` 中，用于内核分派具体系统调用。  
- 系统调用入口本身是一个 Trap，但具体调用哪个服务由 `%rax` 决定。

**异常表与分层图的对应**：

```
┌──────────────────────────────────────────────────────────────┐
│ 用户态                                                        │
│   应用代码                                                    │
│    ├─ setjmp/longjmp ──► 纯用户态跳转，不经过异常表            │
│    └─ glibc read/write ──► syscall 指令 ──┐                   │
└────────────────────────────────────────────┼─────────────────┘
                                             │ ← OS/应用交界
                                             │   Trap 作为系统调用入口
                                             ▼
┌──────────────────────────────────────────────────────────────┐
│ 内核态                                                        │
│   系统调用入口、异常表、信号递送、进程调度、文件系统、设备驱动   │
└──────────────────────────────────────────────────────────────┘
                    ▲
                    │ ← 硬件/OS 交界
                    │   异常号 / 向量号 / 中断
┌───────────────────┴──────────────────────────────────────────┐
│ 硬件层                                                        │
│   CPU 异常/中断、MMU、设备、特权级机制                         │
└──────────────────────────────────────────────────────────────┘
```

### 2.4 系统调用：程序的"受控服务窗口"

> 注意：glibc 的 `read/write` 包装本身不是 ECF，它只是进入内核的入口；真正属于 ECF 的是它执行的 `syscall` 指令触发的 **Trap 异常**。

系统调用是 **用户程序请求 OS 服务的唯一正规途径**，是 Trap 最重要的应用。

```c
// x86-64 Linux 系统调用的寄存器约定：
// %rax = 系统调用号
// %rdi, %rsi, %rdx, %r10, %r8, %r9 = 最多 6 个参数
// 返回值在 %rax 中（负数表示出错，对应 errno）

// 常用系统调用号：
// 0 = read     1 = write    2 = open     3 = close
// 57 = fork    59 = execve  60 = _exit   62 = kill
```

普通函数调用 vs 系统调用：

```
普通函数调用：           系统调用（Trap）：
  call foo               mov $0, %rax     // read 的调用号
  ...                    syscall          // 触发陷阱，进入内核
  ret                    ...              // 内核执行完毕返回
用户态全程              用户态 → 内核态 → 用户态
```

### 2.5 动手实验：用 strace 看系统调用

```c
// syscall_hello.c — 直接使用系统调用，不借助 printf
#include <unistd.h>

int main() {
    const char *msg = "Hello from syscall!\n";
    write(1, msg, 20);   // 系统调用 write(fd=1, buf, len)
    _exit(0);            // 直接调用 _exit，不走 C 库的清理流程
}
```

```bash
$ gcc -o syscall_hello syscall_hello.c
$ ./syscall_hello
Hello from syscall!

# strace 追踪所有系统调用：
$ strace ./syscall_hello 2>&1 | grep -E "write|exit"
write(1, "Hello from syscall!\n", 20)  = 20
exit_group(0)                          = ?
```


### 2.6 从系统调用回到 Trap：看见那条 `syscall` 指令

2.5 用 strace 看到的是**系统调用的结果**：调了哪个、参数是什么、返回值是多少。  
但 Trap 发生在更底层——**执行 `syscall` 指令的那一瞬间**。strace 看不到这个瞬间，但它的存在依赖这个瞬间。

**第一步：反汇编 `main`，先看看它到底调了什么**

```bash
$ objdump -d -M intel syscall_hello | grep -A20 "<main>:"
```

```asm
0000000000001169 <main>:
    1169:   f3 0f 1e fa             endbr64                    # CET 间接跳转保护，main 入口
    116d:   55                      push   %rbp                # 保存调用者的帧指针
    116e:   48 89 e5                mov    %rsp,%rbp           # 建立当前栈帧：rbp = rsp
    1171:   48 83 ec 10             sub    $0x10,%rsp          # 在栈上分配 16 字节局部空间
    1175:   48 8d 05 88 0e 00 00    lea    0xe88(%rip),%rax    # rax = msg 的地址（"Hello from syscall!\n"）
    117c:   48 89 45 f8             mov    %rax,-0x8(%rbp)     # 把 msg 指针存到局部变量 [rbp-8]
    1180:   48 8b 45 f8             mov    -0x8(%rbp),%rax     # rax = msg（准备作为 write 的第 2 个参数）
    1184:   ba 14 00 00 00          mov    $0x14,%edx          # edx = 20，write 的第 3 个参数 len
    1189:   48 89 c6                mov    %rax,%rsi           # rsi = msg，write 的第 2 个参数 buf
    118c:   bf 01 00 00 00          mov    $0x1,%edi           # edi = 1，write 的第 1 个参数 fd=STDOUT
    1191:   e8 da fe ff ff          call   1070 <write@plt>    # 调用 glibc 的 write 包装，不是直接 syscall！
    1196:   bf 00 00 00 00          mov    $0x0,%edi           # edi = 0，_exit 的参数：退出码 0
    119b:   e8 c0 fe ff ff          call   1060 <_exit@plt>    # 调用 glibc 的 _exit 包装
```

**关键观察：这里没有 `syscall` 指令！**

- 你看到的是 `call write@plt`，不是 `syscall`。
- `write@plt` 是 PLT 跳转桩，它会跳到 glibc 里的 `write` 包装函数。
- 真正的 `syscall` 指令在 **glibc 的 `libc.so.6` 里**，不在你的可执行文件里。
- 所以，想看到 Trap 的触发点，需要往里再走一层。

**第二步：用 gdb 跟进 glibc，看到真正的 `syscall`**

```bash
$ gdb ./syscall_hello
(gdb) break write
(gdb) run
(gdb) disassemble
```

你会看到 glibc 里 `write` 的核心：

```asm
Dump of assembler code for function __GI___libc_write:
=> 0x00007ffff7ea19c0 <+0>:     endbr64
   0x00007ffff7ea19c4 <+4>:     mov    eax,DWORD PTR fs:0x18      # 读线程状态，判断是否需要走取消处理路径
   0x00007ffff7ea19cc <+12>:    test   eax,eax
   0x00007ffff7ea19ce <+14>:    jne    0x7ffff7ea19e0             # 非 0 → 走慢路径
   0x00007ffff7ea19d0 <+16>:    mov    eax,0x1                    # write 系统调用号
   0x00007ffff7ea19d5 <+21>:    syscall                            # ← 这里触发 Trap
   0x00007ffff7ea19d7 <+23>:    cmp    rax,0xfffffffffffff000
   0x00007ffff7ea19dd <+29>:    ja     0x7ffff7ea1a30             # 出错 → 设置 errno
   0x00007ffff7ea19df <+31>:    ret
   0x00007ffff7ea19e0 <+32>:    sub    rsp,0x28                   # 慢路径：有取消需求
   0x00007ffff7ea19e4 <+36>:    mov    QWORD PTR [rsp+0x18],rdx
   0x00007ffff7ea19e9 <+41>:    mov    QWORD PTR [rsp+0x10],rsi
   0x00007ffff7ea19ee <+46>:    mov    DWORD PTR [rsp+0x8],edi
   0x00007ffff7ea19f2 <+50>:    call   0x7ffff7e1d9b0 <__GI___pthread_enable_asynccancel>
   0x00007ffff7ea19f7 <+55>:    mov    rdx,QWORD PTR [rsp+0x18]
   0x00007ffff7ea19fc <+60>:    mov    rsi,QWORD PTR [rsp+0x10]
   0x00007ffff7ea1a01 <+65>:    mov    r8d,eax
   0x00007ffff7ea1a04 <+68>:    mov    edi,DWORD PTR [rsp+0x8]
   0x00007ffff7ea1a08 <+72>:    mov    eax,0x1                    # 慢路径下同样放系统调用号
   0x00007ffff7ea1a0d <+77>:    syscall                            # ← 慢路径的 Trap
   0x00007ffff7ea1a0f <+79>:    cmp    rax,0xfffffffffffff000
   0x00007ffff7ea1a15 <+85>:    ja     0x7ffff7ea1a48
   0x00007ffff7ea1a17 <+87>:    mov    edi,r8d
   0x00007ffff7ea1a1a <+90>:    mov    QWORD PTR [rsp+0x8],rax
   0x00007ffff7ea1a1f <+95>:    call   0x7ffff7e1da20 <__GI___pthread_disable_asynccancel>
   0x00007ffff7ea1a24 <+100>:   mov    rax,QWORD PTR [rsp+0x8]
   0x00007ffff7ea1a29 <+105>:   add    rsp,0x28
   0x00007ffff7ea1a2d <+109>:   ret
   ...
```

**关键观察：**

- main 里看到的是 `call write@plt`，不是 `syscall`。
- gdb 跟进后，才在 glibc 的 `__GI___libc_write` 里看到真正的 `syscall` 指令。
- glibc 的 `write` 分两条路径：
  - **快路径**（+16～+31）：直接 `mov eax,0x1; syscall`，适合不需要取消处理的场景。
  - **慢路径**（+32～+109）：先调用 `__pthread_enable_asynccancel`，再 `syscall`，最后 `__pthread_disable_asynccancel`，适合支持线程取消的场景。
- 无论快慢路径，最终都是 `mov eax,0x1` + `syscall` 触发 Trap。
- 出错时 `cmp rax,0xfffffffffffff000; ja ...`，把负数错误码转成 `-1` 并设置 `errno`。

**第三步：断在 `syscall` 上，看执行前；再 `si`，看返回后**

用户态 gdb 进不了内核。`si` 遇到 `syscall` 会把整段 Trap 一次做完，停在下一条指令。所以要先用断点停在 `syscall` **还没执行**的那一拍，记下 RIP 和寄存器，再 `si` 看回来之后。地址每次运行都不同，必须从自己的 `disassemble` 里抄，下面的 `0x7ffff7ea19d5` 只是示例。

```bash
$ gdb ./syscall_hello
(gdb) break write
(gdb) run
(gdb) disassemble
# 找到快路径那条 syscall，例如：
#   0x00007ffff7ea19d5 <+21>: syscall
(gdb) break *0x7ffff7ea19d5
(gdb) continue
```

这一次停住时，指令还没执行。用 `x/i` 确认箭头打在 `syscall` 上，而不是下一条：

```text
(gdb) x/2i $rip
=> 0x7ffff7ea19d5 <__GI___libc_write+21>:  syscall
   0x7ffff7ea19d7 <__GI___libc_write+23>:  cmp    rax,0xfffffffffffff000
(gdb) info registers rax rdi rsi rdx rip
rax            0x1                 1          # 系统调用号，write
rdi            0x1                 1          # fd
rsi            0x555555556004      ...        # buf
rdx            0x14                20         # count
rip            0x7ffff7ea19d5      ...+21     # 停在 syscall 这一条上
```

然后单步。终端会先印出字符串，gdb 再停在下一条：

```text
(gdb) si
Hello from syscall!
0x00007ffff7ea19d7 in __GI___libc_write ()
(gdb) x/2i $rip-2
   0x7ffff7ea19d5 <__GI___libc_write+21>:  syscall
=> 0x7ffff7ea19d7 <__GI___libc_write+23>:  cmp    rax,0xfffffffffffff000
(gdb) info registers rax rip
rax            0x14                20         # 返回值：写了 20 字节
rip            0x7ffff7ea19d7      ...+23     # 已经离开 syscall
```

两次停止对上 Trap 的用户态两侧：

| | 执行前（断点） | `si` 之后 |
|---|---|---|
| RIP | `...19d5`，指着 `syscall` | `...19d7`，`syscall` 的下一条 |
| rax | `1`（调用号） | `20`（返回值） |
| rdi / rsi / rdx | `1` / buf / `20` | 内核一般不动这三个参数 |

中间那一段（ring 3 → ring 0、按 `rax=1` 进入 `sys_write`、写完再返回 ring 3）发生在这两次停止之间。`si` 不会在内核里停，所以第三步看不到内核内部。

**第四步：用 `catch syscall write` 看内核入口和出口**

不要把这一步当成第三步的重复。`catch syscall` 停住时，CPU 已经进过内核，ptrace 交给 gdb 的 RIP **始终是 `syscall` 的返回地址**（下一条），两次停止的 RIP 相同。真正要对比的是停住原因，以及 `rax` 和 `orig_rax`。

```bash
$ gdb ./syscall_hello
(gdb) catch syscall write
(gdb) run
```

第一次停住，提示是 **call to syscall**，不是 returned：

```text
Catchpoint 1 (call to syscall write), 0x00007ffff7ea19d7 in __GI___libc_write ()
(gdb) x/i $rip
=> 0x7ffff7ea19d7 <__GI___libc_write+23>:  cmp    rax,0xfffffffffffff000
(gdb) info registers rax orig_rax rdi rsi rdx rip
rax            0xffffffffffffffda  -38        # -ENOSYS，内核放的占位，不是返回值
orig_rax       0x1                 1          # 用户放进 rax 的调用号被内核留在这里
rdi            0x1                 1
rsi            0x555555556004      ...
rdx            0x14                20
rip            0x7ffff7ea19d7      ...+23     # 已经是下一条，不是 syscall 本身
```

`rax = -38` 是 Linux 的约定：系统调用一进内核，就把 `rax` 改成 `-ENOSYS`，原来的调用号挪到 `orig_rax`。这样跟踪器如果改了调用号却没有真正执行，返回值也不会被误当成成功。所以第四步第一次看到的，是**内核入口现场**，不是第三步那种「用户还站在 `syscall` 指令上」的现场。

再继续，才是返回：

```text
(gdb) continue
Hello from syscall!

Catchpoint 1 (returned from syscall write), 0x00007ffff7ea19d7 in __GI___libc_write ()
(gdb) info registers rax orig_rax rip
rax            0x14                20         # 这次才是返回值
orig_rax       0x1                 1          # 调用号不变
rip            0x7ffff7ea19d7      ...+23     # 和入口那次相同
```

两次实验合在一起，才是完整的 Trap，而不是同一种停法看两遍：

| 时刻 | 怎么停 | RIP | rax | 你在看什么 |
|---|---|---|---|---|
| 用户即将执行 | 第三步，`break *syscall` | 指着 `syscall` | `1` | 进内核之前的用户态 |
| 内核刚接住 | 第四步，`call to syscall` | `syscall` 的下一条 | `-38`，调用号在 `orig_rax` | 内核入口 |
| 内核做完返回 | 第三步的 `si`，或第四步的 `returned from` | 还是下一条 | `20` | 回到用户态 |

只看 RIP 会觉得第三步的「`si` 之后」和第四步的两次停止是同一个地方。区分它们要看三样东西：gdb 的提示是 `call to` 还是 `returned from`，`rax` 是 `-38` 还是 `20`，以及第三步第一次停止时 RIP 有没有落在 `syscall` 那一条上。

**第五步：把 2.5、2.6 串起来**

| 层次 | 看到什么 | 用什么工具 |
|---|---|---|
| 系统调用结果 | `write(1, "...", 20) = 20` | `strace` |
| 用户态调用点 | `call write@plt` | `objdump`（看 main） |
| 系统调用入口 | `mov eax,0x1` + `syscall` | `gdb`（跟进 glibc）或 `objdump`（看 libc.so） |
| 执行前的用户态 | RIP 停在 `syscall` 上，`rax=1` | `break *syscall` |
| 内核入口 / 出口 | 入口 `rax=-38`、`orig_rax=1`；出口 `rax=20`。两次 RIP 都在下一条 | `catch syscall` |
| 返回后的用户态 | RIP 离开 `syscall`，`rax=20` | 在 `syscall` 上 `si` |

所以：

- **`strace` 看的是“系统调用做了什么”**。
- **`objdump` 看的是“用户态怎么调用、glibc 怎么触发”**。
- **`gdb` 要分两次看**：`break *syscall` 看执行前，`catch syscall` 看内核入口（`orig_rax`）和出口（返回值）。只比 RIP 会把这两次看成同一步。

一句话：

> **系统调用是 Trap 的应用层表现，Trap 是系统调用的底层入口机制。普通程序里 `syscall` 指令在 glibc 里；想在自己的可执行文件里看到它，用内联汇编绕过 glibc。三者是同一件事在不同层次上的观察结果：strace 看结果，objdump 看指令，gdb 看切换。**

---

## 第三关：进程——你的程序活在哪里？

**OS 层 ECF（进程）**

### 3.1 进程 = 程序的运行实例

**程序**是磁盘上的可执行文件（静态的）。  
**进程**是程序在内存中运行时的"实例"（动态的）：

```
一个进程独占（错觉上）：
┌─────────────────────────────────────┐
│  私有虚拟地址空间（别的进程看不到）    │  ← 好像独占整个内存
│  独立的寄存器状态（PC、栈指针等）     │  ← 好像独占整个 CPU
│  打开的文件描述符集合                │
│  独立的 PID（进程 ID）               │
│  父进程 PID（PPID）                  │
└─────────────────────────────────────┘
```

OS 通过**上下文切换**快速在多个进程间轮换，每个进程"感觉"自己独占 CPU：

```
进程A：│▓▓▓│     │▓▓▓│     │▓▓▓│
进程B：│     │▓▓▓│     │     │
进程C：│          │▓▓▓│▓▓▓│
                    实际 CPU 在轮流使用
```

### 3.2 创建进程：fork() 的"复制魔法"

`fork()` 创建进程的方式很独特：**复制当前进程，产生一个几乎完全一样的子进程**。

```c
// fork_demo.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    pid_t pid;
    int x = 1;

    pid = fork();  // ← 调用一次，返回两次！

    if (pid == 0) {
        // 子进程：fork 返回 0
        printf("子进程: pid=%d, x=%d\n", getpid(), ++x);
        exit(0);
    }

    // 父进程：fork 返回子进程的 PID（> 0）
    printf("父进程: pid=%d, 子进程pid=%d, x=%d\n", getpid(), pid, --x);
    exit(0);
}
```

```bash
$ gcc -o fork_demo fork_demo.c && ./fork_demo
父进程: pid=1234, 子进程pid=1235, x=0
子进程: pid=1235, x=2
# 注意：两行的先后顺序不固定！
```

**fork 的三个关键特性**：

```
① "调用一次，返回两次"
   父进程中返回：子进程的 PID（> 0）→ if (pid == 0) 分支不走
   子进程中返回：0               → if (pid == 0) 分支走这里

② 地址空间相互独立（写时复制）
   fork 后，父子进程各自修改变量，互不影响
   上例中父进程 x → 0，子进程 x → 2

③ 执行顺序不可预测
   OS 随机决定先调度父还是子
   永远不要假设固定的先后顺序！
```

### 3.3 用进程图理解多次 fork

多个 fork 会产生多个进程，用进程图（树形图）分析：

```c
void fork2() {
    printf("L0\n");
    fork();          // 第一次 fork：1 → 2 个进程
    printf("L1\n");
    fork();          // 第二次 fork：2 → 4 个进程
    printf("Bye\n");
}
```

进程图：
```
                    main
                      │
               printf("L0")          只打印一次
                      │
               fork() ─────────────────────────┐
               │（进程A）                       │（进程B，新）
        printf("L1")                    printf("L1")
               │                               │
         fork() ──────┐                 fork() ──────┐
         │（A）        │（C，新）          │（B）        │（D，新）
    printf("Bye")  printf("Bye")    printf("Bye")  printf("Bye")
```

**结果**：L0 × 1，L1 × 2，Bye × 4（顺序不确定）

<details>
<summary>思考题：三个连续 fork() 会产生几个进程？</summary>

8 个（包含原始进程自身）：1 → 2 → 4 → 8。
每次 fork，当前所有存活进程都各自 fork 一次，进程数翻倍。
</details>

### 3.4 回收子进程：waitpid()

子进程退出后不会立即消失——它变成**僵尸进程（Zombie）**，保留基本信息直到父进程来"收尸"：

```
进程生命周期：
创建(fork) → 运行 → 退出 → [僵尸状态] → 父进程 waitpid → 彻底销毁

如果父进程先于子进程退出：
子进程变为"孤儿进程"，被 init 进程（PID=1）收养并负责回收
```

`waitpid()` 用法：

```c
// waitpid_demo.c
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    int status;

    pid_t pid = fork();
    if (pid == 0) {
        printf("子进程 %d 即将以退出码 42 退出\n", getpid());
        exit(42);
    }

    // 等待任意子进程退出（pid=-1 表示任意，0 表示默认阻塞）
    pid_t reaped = waitpid(-1, &status, 0);

    if (WIFEXITED(status)) {
        printf("父进程：子进程 %d 正常退出，退出码 = %d\n",
               reaped, WEXITSTATUS(status));
    }
    return 0;
}
```

```bash
$ gcc -o waitpid_demo waitpid_demo.c && ./waitpid_demo
子进程 1235 即将以退出码 42 退出
父进程：子进程 1235 正常退出，退出码 = 42
```

**waitpid 参数速查**：

| `pid` 参数 | 等待谁 |
|-----------|--------|
| `-1` | 任意子进程（最常用）|
| `> 0` | 指定 PID 的子进程 |

| `options` 参数 | 行为 |
|-----------|------|
| `0` | 阻塞，直到有子进程结束 |
| `WNOHANG` | 非阻塞，若无子进程结束则立即返回 0 |
| `WUNTRACED` | 同时等待被暂停（stopped）的子进程 |

| 状态宏 | 含义 |
|--------|------|
| `WIFEXITED(status)` | 子进程正常退出（exit 或 main return）|
| `WEXITSTATUS(status)` | 正常退出时的退出码 |
| `WIFSIGNALED(status)` | 子进程因信号被杀死 |
| `WTERMSIG(status)` | 导致杀死的信号编号 |

### 3.5 加载新程序：execve()

`fork()` 是"复制"，`execve()` 是"替换"——**在当前进程中加载并运行一个全新程序**：

```c
int execve(const char *filename,  // 可执行文件路径
           const char *argv[],    // 参数数组（argv[0]=程序名）
           const char *envp[]);   // 环境变量数组
// 成功时永远不返回（当前进程内存被完全替换）
// 失败时返回 -1
```

`fork + execve` 是 Shell 执行命令的标准模式：

```c
// exec_demo.c — Shell 执行命令的核心模式
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    pid_t pid = fork();

    if (pid == 0) {
        // 子进程：加载 /bin/ls 替换自身
        char *argv[] = {"ls", "-l", NULL};
        execve("/bin/ls", argv, NULL);
        // 只有 execve 失败才会走到这里
        perror("execve 失败");
        exit(1);
    }

    waitpid(pid, NULL, 0);  // 父进程等待 ls 执行完
    printf("ls 命令执行完毕\n");
    return 0;
}
```

```bash
$ gcc -o exec_demo exec_demo.c && ./exec_demo
total 16
-rwxr-xr-x 1 user user 12345 ... exec_demo
-rw-r--r-- 1 user user   456 ... exec_demo.c
ls 命令执行完毕
```

**fork vs exec 本质区别**：

```
fork()：   "复印机"   ← 复制当前进程，两份几乎相同，分别执行
execve()： "换脑手术" ← 用新程序替换当前进程，PID 不变但全部内容换掉
```

### 3.6 动手：实现一个极简 Shell

```c
// mysh.c — 极简 Shell（核心逻辑约 35 行）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXARGS 64

int main() {
    char line[1024];
    char *argv[MAXARGS];

    while (1) {
        printf("mysh> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        // 分词
        int argc = 0;
        char *tok = strtok(line, " \t\n");
        while (tok && argc < MAXARGS - 1) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t\n");
        }
        argv[argc] = NULL;
        if (argc == 0) continue;

        // 内置命令
        if (strcmp(argv[0], "quit") == 0) break;

        // fork + exec 执行外部命令
        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[0], argv);  // 在 PATH 中搜索命令
            fprintf(stderr, "%s: 命令未找到\n", argv[0]);
            exit(1);
        }
        waitpid(pid, NULL, 0);
    }
    return 0;
}
```

```bash
$ gcc -o mysh mysh.c && ./mysh
mysh> ls mysh.c
mysh.c
mysh> echo hello world
hello world
mysh> quit
$
```

---

## 第四关：信号——进程间的"异步短信"

**OS 层 ECF（信号）**

### 4.1 信号是什么

**信号（Signal）** 是 OS 或其他进程发给进程的一个小型通知，只携带两个信息：**信号编号** 和 **"有信号到达"这个事实**，没有其他数据。

```
你的程序正在跑...
  → 用户按了 Ctrl-C
  → OS 向前台进程组发送 SIGINT（编号 2）
  → 进程收到 SIGINT → 默认行为：终止进程

类比：信号就像手机上的"未读通知提示"——只知道"有消息"，
      但不知道消息内容（内容要通过其他机制传递）
```

**常用信号速查**：

| 信号 | 编号 | 默认行为 | 常见触发方式 |
|------|------|---------|------------|
| SIGINT | 2 | 终止 | Ctrl-C |
| SIGKILL | 9 | 终止（**不可捕获！**）| `kill -9 pid` |
| SIGSEGV | 11 | 终止+core dump | 非法内存访问 |
| SIGALRM | 14 | 终止 | `alarm()` 定时器到期 |
| SIGCHLD | 17 | 忽略 | 子进程终止或停止 |
| SIGSTOP | 19 | 暂停（**不可捕获！**）| Ctrl-Z |
| SIGCONT | 18 | 继续运行 | `kill -CONT pid` |

> SIGKILL 和 SIGSTOP 无法被捕获或忽略，这是 OS 保留的强制控制权。

### 4.2 信号的状态机（关键！）

每类信号在进程端有两个关键概念：

```
信号生命周期：

OS 发送 → [待处理 Pending] → 进程接收 → 执行 handler（或默认行为）
                ↑
         如果同类型信号已经 Pending，新信号直接丢弃！（不排队）
                               ↓
                     如果该信号被阻塞(Blocked)
                     则暂时不被接收，保留在 Pending 中
```

**内核为每个进程维护两个位向量**：

```
pending 位向量：第 k 位 = 1 → 第 k 号信号有待处理实例
blocked 位向量：第 k 位 = 1 → 第 k 号信号被阻塞（暂不接收）

接收时机：内核将控制权交还用户态前，检查 pnb = pending & ~blocked
          pnb 中最低编号的信号会被接收（执行 handler 或默认行为）
```

**最重要的限制**：**信号不排队**！  
同一类型的信号最多只有 1 个处于待处理状态，多余的会被丢弃。

### 4.3 发送信号的方式

```bash
# 命令行方式：
kill -9 1234         # 向 PID 1234 发送 SIGKILL
kill -9 -1234        # 向进程组 1234 的所有进程发送 SIGKILL
kill -SIGTERM 1234   # 发送 SIGTERM（更优雅的终止请求）
```

```c
// C 代码方式：
#include <signal.h>
kill(pid, SIGTERM);    // 向指定 PID 发送
kill(-pgid, SIGTERM);  // 向整个进程组发送
raise(SIGINT);         // 向自己发送

// 键盘方式（作用于前台进程组）：
// Ctrl-C → SIGINT（终止）
// Ctrl-Z → SIGTSTP（暂停）
// Ctrl-\ → SIGQUIT（终止 + core dump）
```

### 4.4 安装信号处理函数

```c
// signal_demo.c
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void sigint_handler(int sig) {
    // sig = 2（SIGINT 的编号）
    write(STDOUT_FILENO, "\n收到 Ctrl-C，但我不会就这样结束的\n", 40);
}

int main() {
    signal(SIGINT, sigint_handler);  // 安装 handler

    printf("按三次 Ctrl-C 我才结束\n");
    int count = 0;
    while (count < 3) {
        pause();   // 等待任意信号
        count++;
        printf("第 %d 次 Ctrl-C\n", count);
    }
    printf("好吧，再见！\n");
    return 0;
}
```

```bash
$ gcc -o signal_demo signal_demo.c && ./signal_demo
按三次 Ctrl-C 我才结束
^C
收到 Ctrl-C，但我不会就这样结束的
第 1 次 Ctrl-C
^C
收到 Ctrl-C，但我不会就这样结束的
第 2 次 Ctrl-C
^C
收到 Ctrl-C，但我不会就这样结束的
第 3 次 Ctrl-C
好吧，再见！
```

### 4.5 安全编写信号处理函数的 5 条戒律

信号 handler 可在任何时刻中断主程序，与主程序**并发执行**，极易引发竞态条件。

**戒律一：handler 只做最简单的事**
```c
// ✅ 好的 handler：只设置一个标志
volatile sig_atomic_t got_signal = 0;
void handler(int sig) { got_signal = 1; }  // 主程序轮询 got_signal

// ❌ 坏的 handler：做了太多事（危险！）
void bad_handler(int sig) {
    printf("处理信号...\n");  // printf 不是 async-signal-safe！
    malloc(100);              // malloc 不是 async-signal-safe！
}
```

**戒律二：只调用 async-signal-safe 函数**
```c
// ✅ 安全（可在 handler 中使用）：
// _exit(), write(), read(), kill(), waitpid(), sleep()

// ❌ 不安全（禁止在 handler 中使用）：
// printf(), fprintf(), malloc(), free(), exit(), sprintf()
// 原因：这些函数内部有全局状态（缓冲区、锁），被中断后再次进入会死锁或数据损坏

// 如果需要在 handler 里输出，直接用 write()：
void safe_output(int sig) {
    const char *msg = "收到信号\n";
    write(STDOUT_FILENO, msg, 9);  // write 是 async-signal-safe 的
}
```

**戒律三：保存和恢复 errno**
```c
void handler(int sig) {
    int saved_errno = errno;   // 保存（很多系统调用会修改 errno）
    // ... handler 逻辑 ...
    errno = saved_errno;       // 恢复，避免影响主程序对 errno 的判断
}
```

**戒律四：全局共享变量用 volatile 声明**
```c
volatile sig_atomic_t flag = 0;
// volatile：防止编译器把变量缓存在寄存器中（handler 修改后主程序看不到）
// sig_atomic_t：POSIX 保证的原子读写类型（防止"读到一半被中断"的情况）
```

**戒律五：SIGCHLD handler 中必须循环 waitpid**
```c
// ✅ 正确：循环回收所有已结束的子进程
void sigchld_handler(int sig) {
    int saved_errno = errno;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // 处理子进程退出信息...
    }
    errno = saved_errno;
}

// ❌ 错误：只调用一次 wait，多个子进程同时退出时会漏掉
// 原因：信号不排队，5 个子进程同时退出可能只触发 1~2 次 SIGCHLD
void bad_handler(int sig) {
    wait(NULL);  // 只回收 1 个！其余变成僵尸进程
}
```

### 4.6 使用 sigprocmask 阻塞信号（避免竞态条件）

Shell 添加作业的经典竞态条件：

```c
// ❌ 危险！子进程可能在 addjob() 前就退出并触发 SIGCHLD handler
if ((pid = fork()) == 0) {
    execve(...);
}
addjob(pid);   // 若子进程已退出，SIGCHLD handler 可能先于 addjob 运行
               // → deletejob 在 addjob 之前执行 → 作业丢失！

// ✅ 正确：在 fork 前阻塞 SIGCHLD，确保 addjob 先执行
sigset_t mask, prev;
sigemptyset(&mask);
sigaddset(&mask, SIGCHLD);

sigprocmask(SIG_BLOCK, &mask, &prev);   // 阻塞 SIGCHLD
if ((pid = fork()) == 0) {
    sigprocmask(SIG_SETMASK, &prev, NULL);  // 子进程解除阻塞
    execve(...);
}
addjob(pid);                              // 父进程：确保先执行
sigprocmask(SIG_SETMASK, &prev, NULL);  // 解除阻塞，SIGCHLD 现在才能被接收
```

---

## 第五关：非局部跳转——C 的"跨函数应急出口"

**应用层 ECF**

> `setjmp/longjmp` 是纯用户态实现，直接保存/恢复通用寄存器，不经过异常表，也不依赖系统调用。

### 5.1 什么是非局部跳转

正常的函数调用返回必须"一层一层往回走"：

```
调用栈：main → foo → bar → error!
恢复：  bar return → foo return → main

非局部跳转：
调用栈：main → foo → bar → longjmp() → 直接回到 main 中 setjmp 的位置！
              ↑
         bar 和 foo 的栈帧被直接跳过（unwound）
```

这类似于其他语言的异常处理（Java 的 try-catch），但是在 C 语言层面、没有语言级支持的情况下实现的。

### 5.2 setjmp / longjmp API

```c
#include <setjmp.h>

// setjmp：保存当前执行环境（寄存器状态、栈指针）到 env
// 第一次调用（正向执行）：返回 0
// 从 longjmp 跳回来时：返回 longjmp 传入的 val（非零）
int setjmp(jmp_buf env);

// longjmp：恢复到 setjmp 时保存的环境
// 永远不返回！直接跳转到对应 setjmp 处
// val 不能为 0（若传 0，setjmp 实际返回 1）
void longjmp(jmp_buf env, int val);
```

### 5.3 完整示例：模拟 try-catch 错误处理

```c
// setjmp_demo.c
#include <stdio.h>
#include <setjmp.h>

jmp_buf error_env;  // "应急返回锚点"

// 深层函数检测到严重错误
void level3() {
    printf("level3: 发现严重错误，直接跳回 main\n");
    longjmp(error_env, 1);       // val=1 表示"发生了错误类型1"
    printf("level3: 这行永远不会执行\n");
}

void level2() {
    printf("level2: 调用 level3...\n");
    level3();
    printf("level2: 这行永远不会执行\n");
}

void level1() {
    printf("level1: 调用 level2...\n");
    level2();
    printf("level1: 这行永远不会执行\n");
}

int main() {
    // 设置"应急返回锚点"
    // 第一次：setjmp 返回 0，走正常流程
    // 从 longjmp 跳回：setjmp 返回 longjmp 传入的 val
    int rc = setjmp(error_env);

    if (rc == 0) {
        printf("main: 开始正常流程\n");
        level1();
        printf("main: 这行永远不会执行\n");
    } else {
        // 从 longjmp 跳回，rc = 错误类型
        printf("main: 捕获到错误，错误码 = %d，进行恢复处理\n", rc);
    }

    printf("main: 程序继续执行...\n");
    return 0;
}
```

```bash
$ gcc -o setjmp_demo setjmp_demo.c && ./setjmp_demo
main: 开始正常流程
level1: 调用 level2...
level2: 调用 level3...
level3: 发现严重错误，直接跳回 main
main: 捕获到错误，错误码 = 1，进行恢复处理
main: 程序继续执行...
```

### 5.4 在信号处理函数中使用：siglongjmp

在信号处理函数中要用 `sigsetjmp` / `siglongjmp`（而不是普通的 setjmp/longjmp），原因是需要正确保存/恢复信号掩码：

```c
// timeout_demo.c — 用信号 + siglongjmp 实现操作超时
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

sigjmp_buf timeout_env;

void alarm_handler(int sig) {
    siglongjmp(timeout_env, 1);  // 超时，跳回 sigsetjmp 处
}

int main() {
    signal(SIGALRM, alarm_handler);

    if (sigsetjmp(timeout_env, 1) == 0) {
        // 正常路径：给 5 秒时限
        alarm(5);
        printf("请在 5 秒内输入内容：\n");
        char buf[100];
        fgets(buf, sizeof(buf), stdin);
        alarm(0);  // 取消闹钟
        printf("你输入了：%s", buf);
    } else {
        // 超时路径
        printf("\n超时！\n");
    }

    return 0;
}
```

### 5.5 重要限制

```c
// ⚠️ longjmp 后，局部变量的值可能不可靠
int main() {
    int x = 1;           // 普通变量：longjmp 后值不确定（可能被编译器优化）
    volatile int y = 1;  // volatile 变量：longjmp 后值是可靠的

    if (setjmp(env) == 0) {
        x = 99;
        y = 99;
        longjmp(env, 1);
    }
    printf("x = %d\n", x);  // 不确定（可能是 1，也可能是 99）
    printf("y = %d\n", y);  // 确定是 99
}

// ⚠️ 不能 longjmp 回一个已经返回的函数
// setjmp 所在函数已经 return 后，jmp_buf 指向的栈帧已无效
// longjmp 到那里会导致未定义行为（通常直接崩溃）
```

---

## 自测：你掌握了吗？

**第1关**：以下四种异常，哪个是异步的？哪个处理后必须"重新执行触发指令"才能正确恢复？

A. Interrupt   B. Trap   C. Fault   D. Abort

<details><summary>点击查看答案</summary>

**异步的**：A（Interrupt）——来自外部 I/O 设备，与当前指令无关，程序不知道何时发生

**必须重新执行的**：C（Fault）——如缺页异常，OS 将缺失的页加载到内存后，必须重新执行触发缺页的那条指令，才能正确读取数据

</details>

**第2关**：下面代码会打印几行 "hello"？

```c
fork(); fork(); fork();
printf("hello\n");
```

<details><summary>点击查看答案</summary>

**8 行**。每次 fork 使进程数翻倍：1 → 2 → 4 → 8。所有 8 个进程都会执行 printf。

</details>

**第3关**：为什么 SIGCHLD handler 必须循环调用 waitpid，而不能只调用一次 wait？

<details><summary>点击查看答案</summary>

因为**信号不排队**。若 3 个子进程同时退出，OS 可能只向父进程发送 1 个 SIGCHLD（后两个在第一个 pending 时被丢弃）。如果 handler 只调用一次 wait，只回收了 1 个子进程，另外 2 个永久变成僵尸进程。

必须在 handler 中循环 `waitpid(-1, NULL, WNOHANG)`，直到返回 0（没有更多已退出子进程）为止。

</details>

**第4关**：`setjmp` 第一次调用返回 0，从 `longjmp(env, 2)` 跳回来时 setjmp 返回什么？若 longjmp 传入 0 又会怎样？

<details><summary>点击查看答案</summary>

从 `longjmp(env, 2)` 跳回，setjmp 返回 **2**。

若 `longjmp(env, 0)`，规范规定 setjmp 返回 **1**（因为 0 是"正常首次调用"的返回值，必须区分）。

</details>

**第5关**：以下信号处理函数有什么安全问题？

```c
void handler(int sig) {
    printf("Got signal %d\n", sig);
}
```

<details><summary>点击查看答案</summary>

`printf` 不是 async-signal-safe 函数：它内部操作全局的 `FILE` 缓冲区结构（含锁）。若信号在 printf 执行中途到来（主程序也在 printf 中），handler 再次调用 printf 会尝试对同一个锁加锁，导致**死锁**；或缓冲区状态被破坏，导致输出乱序、截断等问题。

应改用 `write(STDOUT_FILENO, msg, len)` 直接写，write 是 async-signal-safe 的。

</details>

---

## 综合练习：能"自我管理"的进程

```c
// managed_child.c
// 综合练习：fork + execve + 信号 + waitpid
// 功能：创建子进程执行 sleep，父进程等待，超时后强制终止

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <setjmp.h>

sigjmp_buf timeout_env;
pid_t child_pid;

void alarm_handler(int sig) {
    siglongjmp(timeout_env, 1);
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    int saved_errno = errno;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status))
            printf("子进程 %d 正常退出，退出码 %d\n", pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("子进程 %d 被信号 %d 杀死\n", pid, WTERMSIG(status));
    }
    errno = saved_errno;
}

int main(int argc, char *argv[]) {
    int timeout = (argc > 1) ? atoi(argv[1]) : 3;
    int sleep_time = (argc > 2) ? atoi(argv[2]) : 5;

    signal(SIGALRM, alarm_handler);
    signal(SIGCHLD, sigchld_handler);

    child_pid = fork();
    if (child_pid == 0) {
        // 子进程：执行 sleep
        char sleep_arg[16];
        snprintf(sleep_arg, sizeof(sleep_arg), "%d", sleep_time);
        char *argv[] = {"sleep", sleep_arg, NULL};
        execvp("sleep", argv);
        perror("execvp");
        exit(1);
    }

    printf("子进程 %d 开始执行（sleep %d 秒），超时限制 %d 秒\n",
           child_pid, sleep_time, timeout);

    if (sigsetjmp(timeout_env, 1) == 0) {
        alarm(timeout);
        // 等待 SIGCHLD（子进程结束）
        while (1) pause();
    } else {
        // 超时
        printf("超时！强制终止子进程 %d\n", child_pid);
        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
    }

    printf("程序结束\n");
    return 0;
}
```

```bash
$ gcc -o managed_child managed_child.c

# 测试1：子进程在超时前结束
$ ./managed_child 5 2
子进程 1234 开始执行（sleep 2 秒），超时限制 5 秒
子进程 1234 正常退出，退出码 0
程序结束

# 测试2：子进程超时被强制终止
$ ./managed_child 3 10
子进程 1235 开始执行（sleep 10 秒），超时限制 3 秒
超时！强制终止子进程 1235
子进程 1235 被信号 9 杀死
程序结束
```

---

## 3 小时学习路径

| 时间 | 任务 |
|------|------|
| 0–20 分钟 | 读第一关，理解"ECF = 响应程序外部事件的机制"，建立全局观 |
| 20–50 分钟 | 读第二关，用 strace 观察系统调用，理解 4 种异常类型的区别 |
| 50–90 分钟 | 读第三关，编译运行 fork_demo、waitpid_demo、mysh，在 mysh 里试执行几个命令 |
| 90–120 分钟 | 读第四关，运行 signal_demo，故意在 handler 里用 printf 感受潜在问题 |
| 120–140 分钟 | 读第五关，运行 setjmp_demo，用纸和笔追踪调用栈的变化 |
| 140–165 分钟 | 完成自测 5 道题，检验理解 |
| 165–180 分钟 | 挑战综合练习，修改 timeout 和 sleep_time 参数观察两种不同结果 |