# Chapter 03 机器级程序表示（Machine-Level Representation of Programs）

> **课程对应**：CMU 15-213 Lecture 05–08 — Machine Level Programming I–IV
> **教材章节**：CSAPP 第 3 章

---

## 一、历史视角与背景

### 1.1 为什么学习机器级编程

现代程序员很少直接编写汇编代码，但理解编译器生成的汇编是关键能力：

- **调试**：当程序行为异常时，汇编揭示编译器的实际决策
- **性能调优**：理解哪些 C 代码产生高效汇编，哪些产生低效序列
- **安全研究**：理解缓冲区溢出、ROP 等攻击需要读懂汇编
- **系统编程**：操作系统、编译器、虚拟机等需要理解硬件交互
- **逆向工程**：分析无源码二进制文件

> 核心视角：**阅读**编译器生成的汇编，而非**手写**汇编。

### 1.2 x86 架构演变

| 年代 | 处理器 | 特性 |
|------|--------|------|
| 1978 | Intel 8086 | 16 位，8 个寄存器，1MB 地址空间 |
| 1985 | Intel 80386 (i386) | 32 位扩展，虚拟内存，使 Linux/UNIX 成为可能 |
| 2000 | AMD Athlon 64 | AMD 率先推出 64 位扩展（x86-64） |
| 2004+ | Intel EM64T | Intel 跟进采用 AMD 的 64 位规范 |
| 2010s+ | 多核时代 | 单核频率受功耗限制，转向多核并行 |

**CISC vs RISC**：

| 特性 | x86 (CISC) | ARM (RISC) |
|------|-----------|-----------|
| 指令数 | 数百条 | ~100条 |
| 指令长度 | 可变（1–15字节） | 固定（4字节） |
| 寻址模式 | 丰富（内存操作数） | 简洁（load/store） |
| 市场 | PC、服务器 | 移动、嵌入式 |

x86 因历史遗留而复杂，但 Intel 通过微码在芯片内部将复杂指令转换为 RISC 核心执行，兼顾了兼容性与性能。

---

## 二、程序编码（Program Encodings）

### 2.1 编译流程

```
source.c → [预处理 cpp] → source.i
         → [编译 cc1]   → source.s   (汇编文本)
         → [汇编 as]    → source.o   (目标文件，二进制)
         → [链接 ld]    → executable (可执行文件)
```

**常用命令**：

```bash
gcc -Og -S source.c        # 生成汇编 source.s（-Og 为调试优化级别）
gcc -Og -c source.c        # 生成目标文件 source.o
objdump -d source.o        # 反汇编目标文件
objdump -d executable      # 反汇编可执行文件
```

### 2.2 机器级程序员的视图

机器代码对程序员暴露的状态（ISA 级别抽象）：

- **程序计数器（PC / %rip）**：下一条要执行指令的地址
- **整数寄存器**：16 个 64 位寄存器，存储整数值和指针
- **条件码寄存器**：存储最近算术/逻辑操作的状态（CF, ZF, SF, OF）
- **向量寄存器**：存储多个整数或浮点数

**隐藏的细节**（C 程序员看不到）：
- 内存是平坦的虚拟字节数组（虚拟内存抽象）
- 乱序执行、流水线、超标量（微架构细节）
- 指令内部的微操作分解

### 2.3 反汇编示例

```c
// 源代码 mstore.c
long mult2(long, long);

void multstore(long x, long y, long *dest) {
    long t = mult2(x, y);
    *dest = t;
}
```

```bash
$ gcc -Og -S mstore.c    # 生成汇编
$ objdump -d mstore.o    # 反汇编
```

```asm
; 反汇编输出（AT&T 语法）
0000000000000000 <multstore>:
   0: 53                    push   %rbx          ; 保存被调用者保存寄存器
   1: 48 89 d3              mov    %rdx,%rbx     ; 保存 dest 指针
   4: e8 00 00 00 00        call   9 <multstore+0x9>  ; 调用 mult2(x,y)
   9: 48 89 03              mov    %rax,(%rbx)   ; *dest = 返回值
   c: 5b                    pop    %rbx          ; 恢复寄存器
   d: c3                    ret                  ; 返回
```

**字节编码**：`48 89 d3` 是 `mov %rdx, %rbx` 的机器码（3字节）。

---

## 三、数据格式（Data Formats）

x86-64 中数据大小与汇编后缀的对应关系：

| C 类型 | Intel 名称 | 汇编后缀 | 大小（字节） |
|--------|-----------|---------|------------|
| `char` | 字节（Byte） | `b` | 1 |
| `short` | 字（Word） | `w` | 2 |
| `int` | 双字（Double word） | `l` | 4 |
| `long` / 指针 | 四字（Quad word） | `q` | 8 |
| `float` | 单精度（Single） | `s` | 4 |
| `double` | 双精度（Double） | `d` (SSE)/`l` | 8 |

> 注意：`l` 后缀在整数指令中表示 32 位（double word），在浮点指令中表示 64 位（double）。

---

## 四、访问信息（Accessing Information）

### 4.1 整数寄存器

x86-64 有 **16 个 64 位通用寄存器**，每个寄存器支持多种宽度访问：

| 64 位 | 32 位 | 16 位 | 8 位（低） | 用途约定 |
|-------|-------|-------|----------|---------|
| `%rax` | `%eax` | `%ax` | `%al` | 返回值 |
| `%rbx` | `%ebx` | `%bx` | `%bl` | 被调用者保存 |
| `%rcx` | `%ecx` | `%cx` | `%cl` | 第4参数 |
| `%rdx` | `%edx` | `%dx` | `%dl` | 第3参数 |
| `%rsi` | `%esi` | `%si` | `%sil` | 第2参数 |
| `%rdi` | `%edi` | `%di` | `%dil` | 第1参数 |
| `%rbp` | `%ebp` | `%bp` | `%bpl` | 帧指针（被调用者保存） |
| `%rsp` | `%esp` | `%sp` | `%spl` | 栈指针 |
| `%r8`  | `%r8d` | `%r8w` | `%r8b` | 第5参数 |
| `%r9`  | `%r9d` | `%r9w` | `%r9b` | 第6参数 |
| `%r10` | `%r10d` | `%r10w` | `%r10b` | 调用者保存 |
| `%r11` | `%r11d` | `%r11w` | `%r11b` | 调用者保存 |
| `%r12` | `%r12d` | `%r12w` | `%r12b` | 被调用者保存 |
| `%r13` | `%r13d` | `%r13w` | `%r13b` | 被调用者保存 |
| `%r14` | `%r14d` | `%r14w` | `%r14b` | 被调用者保存 |
| `%r15` | `%r15d` | `%r15w` | `%r15b` | 被调用者保存 |

**关键规则**：对 32 位寄存器（如 `%eax`）写入会将高 32 位**清零**。对 8/16 位写入**不影响**高位。

### 4.2 操作数类型

| 类型 | 表示 | 含义 | 示例 |
|------|------|------|------|
| 立即数（Immediate） | `$Imm` | 常量整数值 | `$0x400`, `$-533` |
| 寄存器（Register） | `Ra` | 寄存器中的值 | `%rax` |
| 内存引用（Memory） | `(Ra)` | 以寄存器值为地址的内存 | `(%rax)` |

### 4.3 内存寻址模式

通用形式：`D(Rb, Ri, S)` = `Mem[Reg[Rb] + S × Reg[Ri] + D]`

- `D`：位移量（整数常量，可正可负）
- `Rb`：基址寄存器（任意寄存器）
- `Ri`：变址寄存器（`%rsp` 不可用作变址）
- `S`：比例因子（1, 2, 4, 8）

| 表示形式 | 计算地址 | 典型用途 |
|---------|---------|---------|
| `(Rb)` | `Reg[Rb]` | 指针解引用 |
| `D(Rb)` | `Reg[Rb] + D` | 结构体字段访问 |
| `(Rb, Ri)` | `Reg[Rb] + Reg[Ri]` | 数组访问 |
| `D(Rb, Ri)` | `Reg[Rb] + Reg[Ri] + D` | 数组+偏移 |
| `(, Ri, S)` | `S × Reg[Ri]` | 比例寻址 |
| `D(Rb, Ri, S)` | `Reg[Rb] + S×Reg[Ri] + D` | 完整形式 |

**示例**：

```asm
movq  (%rdi), %rax        ; %rax = Mem[%rdi]（指针解引用）
movq  8(%rdi), %rax       ; %rax = Mem[%rdi + 8]（结构体第二字段）
movq  (%rdi, %rcx, 8), %rax  ; %rax = Mem[%rdi + 8×%rcx]（数组元素）
```

### 4.4 数据传送指令 MOV

```
MOV S, D    ; D ← S（不能两个都是内存操作数）
```

| 指令 | 效果 | 说明 |
|------|------|------|
| `movb S, D` | D ← S | 1字节传送 |
| `movw S, D` | D ← S | 2字节传送 |
| `movl S, D` | D ← S | 4字节传送（同时清零高32位） |
| `movq S, D` | D ← S | 8字节传送 |
| `movabsq I, D` | D ← I | 64位立即数传送（唯一能传64位立即数的） |

**零扩展传送**（MOVZ，目标寄存器高位填0）：

| 指令 | 效果 |
|------|------|
| `movzbw` | 字节→字（零扩展） |
| `movzbl` | 字节→双字（零扩展） |
| `movzbq` | 字节→四字（零扩展） |
| `movzwl` | 字→双字（零扩展） |
| `movzwq` | 字→四字（零扩展） |

> 注意：没有 `movzlq`，因为 `movl` 写 32 位寄存器会自动零扩展到 64 位！

**符号扩展传送**（MOVS，目标寄存器高位填符号位）：

| 指令 | 效果 |
|------|------|
| `movsbw` | 字节→字（符号扩展） |
| `movsbl` | 字节→双字（符号扩展） |
| `movsbq` | 字节→四字（符号扩展） |
| `movswl` | 字→双字（符号扩展） |
| `movswq` | 字→四字（符号扩展） |
| `movslq` | 双字→四字（符号扩展） |
| `cltq`   | `%eax` 符号扩展到 `%rax`（等价 `movslq %eax, %rax`） |

### 4.5 压栈与弹栈

栈向**低地址**方向增长，`%rsp` 指向**栈顶**（最低地址）。

```asm
pushq  %rbp    ; %rsp -= 8; Mem[%rsp] = %rbp
popq   %rbp    ; %rbp = Mem[%rsp]; %rsp += 8
```

---

## 五、算术与逻辑操作（Arithmetic & Logical Operations）

### 5.1 加载有效地址 LEA

```asm
leaq  S, D    ; D ← &S（计算地址，不访问内存）
```

`leaq` 看起来像 `movq` 的内存引用，但**不读取内存**，只计算地址并存入寄存器。
编译器常用它做**快速算术**：

```c
long scale(long x, long y, long z) {
    return 5 * x + y + z * 8;
}
```

```asm
scale:
    leaq   (%rdi, %rdi, 4), %rax  ; %rax = x + 4x = 5x
    addq   %rsi, %rax              ; %rax += y
    leaq   (%rdx, %rdx, 7), %rdx  ; 错误示例，此处应为 leaq (,%rdx,8)
    leaq   (%rax, %rdx, 8), %rax  ; 实际：%rax = 5x + y + 8z
    ret
```

`leaq (%rdi, %rdi, 4), %rax` 计算 `%rdi + 4×%rdi = 5×%rdi`，无需乘法指令。

### 5.2 一元操作

| 指令 | 效果 |
|------|------|
| `incq D` | D ← D + 1 |
| `decq D` | D ← D - 1 |
| `negq D` | D ← -D（取反） |
| `notq D` | D ← ~D（按位取反） |

### 5.3 二元操作

| 指令 | 效果 | 说明 |
|------|------|------|
| `addq S, D` | D ← D + S | |
| `subq S, D` | D ← D - S | |
| `imulq S, D` | D ← D × S | 有符号乘法（低64位） |
| `xorq S, D` | D ← D ^ S | 异或 |
| `orq S, D`  | D ← D \| S | 按位或 |
| `andq S, D` | D ← D & S | 按位与 |

> **注意**：`subq %rax, %rdx` 的语义是 `%rdx -= %rax`（AT&T 语法：源在左，目标在右）。

### 5.4 移位操作

| 指令 | 效果 | 说明 |
|------|------|------|
| `salq k, D` / `shlq k, D` | D ← D << k | 左移，等价 |
| `sarq k, D` | D ← D >>ₐ k | 算术右移（填符号位） |
| `shrq k, D` | D ← D >>ₗ k | 逻辑右移（填0） |

移位量 `k` 可以是立即数，也可以是 `%cl`（只用低几位：`movb $k, %cl`）。

### 5.5 特殊算术（128 位运算）

```asm
imulq  S    ; [%rdx:%rax] ← %rax × S（有符号 64×64→128 位）
mulq   S    ; [%rdx:%rax] ← %rax × S（无符号）
idivq  S    ; %rdx ← [%rdx:%rax] mod S，%rax ← [%rdx:%rax] / S（有符号）
divq   S    ; 无符号除法
cqto        ; %rdx:%rax ← SignExtend(%rax)（128位符号扩展，除法前用）
```

---

## 六、控制（Control）

### 6.1 条件码（Condition Codes）

CPU 维护一组单位条件码寄存器，由最近的算术/逻辑指令设置：

| 条件码 | 名称 | 含义 |
|--------|------|------|
| `CF` | 进位标志（Carry Flag） | 最近操作产生进位/借位（无符号溢出） |
| `ZF` | 零标志（Zero Flag） | 最近操作结果为0 |
| `SF` | 符号标志（Sign Flag） | 最近操作结果为负 |
| `OF` | 溢出标志（Overflow Flag） | 最近操作导致有符号溢出 |

**只设置条件码，不修改目标的指令**：

```asm
cmpq  S2, S1   ; 计算 S1 - S2，设置条件码（比较）
testq S2, S1   ; 计算 S1 & S2，设置条件码（按位测试）
```

常见用法：`testq %rax, %rax` 检查 `%rax` 是否为0（等价于 `cmpq $0, %rax` 但更快）。

### 6.2 SET 指令（读取条件码）

根据条件码组合设置单字节（0 或 1）：

| 指令 | 条件 | 有符号含义 | 无符号含义 |
|------|------|----------|---------|
| `sete / setz` | ZF | 相等 (==) | 相等 |
| `setne / setnz` | ~ZF | 不等 (!=) | 不等 |
| `sets` | SF | 负数 | — |
| `setg / setnle` | ~(SF^OF)&~ZF | 大于 (>) | — |
| `setge / setnl` | ~(SF^OF) | 大于等于 (>=) | — |
| `setl / setnge` | SF^OF | 小于 (<) | — |
| `setle / setng` | (SF^OF)\|ZF | 小于等于 (<=) | — |
| `seta / setnbe` | ~CF&~ZF | — | 大于（无符号） |
| `setb / setnae` | CF | — | 小于（无符号） |

```c
int gt(long x, long y) {
    return x > y;
}
```

```asm
gt:
    cmpq   %rsi, %rdi    ; x - y，设置条件码
    setg   %al           ; %al = (x > y) ? 1 : 0（有符号比较）
    movzbl %al, %eax     ; 零扩展到 32 位（同时清零 %rax 高32位）
    ret
```

### 6.3 跳转指令

| 指令 | 条件 | 说明 |
|------|------|------|
| `jmp Label` | 无条件 | 直接跳转 |
| `jmp *Operand` | 无条件 | 间接跳转（跳转表用） |
| `je / jz` | ZF | 相等时跳转 |
| `jne / jnz` | ~ZF | 不等时跳转 |
| `jg / jnle` | ~(SF^OF)&~ZF | 有符号大于 |
| `jl / jnge` | SF^OF | 有符号小于 |
| `ja` | ~CF&~ZF | 无符号大于 |
| `jb` | CF | 无符号小于 |

### 6.4 条件传送指令（Conditional Move）

```asm
cmovXX S, D    ; 若条件 XX 成立，则 D ← S，否则不操作
```

**为什么优于条件跳转**：
- 现代 CPU 使用**分支预测**（branch prediction）提前执行指令
- 预测失败代价高昂（约 15-20 周期的流水线冲刷）
- `cmov` 无条件计算两个分支的值，再选择，**避免分支**

```c
long absdiff(long x, long y) {
    return x > y ? x - y : y - x;
}
```

```asm
absdiff:
    movq   %rdi, %rax    ; %rax = x
    subq   %rsi, %rax    ; %rax = x - y
    movq   %rsi, %rdx    ; %rdx = y
    subq   %rdi, %rdx    ; %rdx = y - x
    cmpq   %rsi, %rdi    ; x - y，设置条件码
    cmovle %rdx, %rax    ; 若 x <= y，则 %rax = y - x
    ret
```

> **注意**：当两个分支有副作用（如指针解引用）时，不能用 `cmov`。

### 6.5 循环

#### do-while 循环（最直接映射）

```c
do {
    body;
} while (test);
```

```asm
loop:
    body
    testq  %rax, %rax   ; 测试条件
    jne    loop          ; 条件真则继续
```

#### while 循环（跳转到中间 / jump-to-middle）

```c
while (test) {
    body;
}
```

```asm
    jmp    test_label    ; 先跳到测试
loop:
    body
test_label:
    testq  %rax, %rax
    jne    loop
```

或用 **do-while 变换**（更常见于 `-O1` 及以上）：

```asm
    testq  %rax, %rax   ; 先测试，若初始不满足则跳过
    je     done
loop:
    body
    testq  %rax, %rax
    jne    loop
done:
```

#### for 循环

C 的 `for (init; test; update) { body; }` 等价于 `while` 变换，编译结果类似。

**计算阶乘示例**：

```c
long fact_for(long n) {
    long i, result = 1;
    for (i = 1; i <= n; i++)
        result *= i;
    return result;
}
```

```asm
fact_for:
    movl   $1, %eax      ; result = 1（同时清零高32位）
    movl   $1, %edx      ; i = 1
    jmp    .test
.loop:
    imulq  %rdx, %rax    ; result *= i
    addq   $1, %rdx      ; i++
.test:
    cmpq   %rdi, %rdx    ; i <= n ?
    jle    .loop
    ret
```

### 6.6 Switch 语句与跳转表

当 `case` 值密集时，编译器生成**跳转表（jump table）**，O(1) 时间选择分支：

```c
void switch_eg(long x, long n, long *dest) {
    long val = x;
    switch (n) {
        case 100: val *= 13;  break;
        case 102: val += 10;  /* fall through */
        case 103: val += 11;  break;
        case 104:
        case 106: val *= val; break;
        default:  val = 0;
    }
    *dest = val;
}
```

编译器生成：

```asm
    ; n 在 %rsi，跳转表基址在 .L4
    subq   $100, %rsi       ; n -= 100（使范围从0开始）
    cmpq   $6, %rsi         ; 超出范围？
    ja     .Ldefault         ; 无符号大于6 → default
    jmp    *.L4(,%rsi,8)    ; 间接跳转：jmp Mem[.L4 + 8×n]
.L4:
    .quad  .Lcase100        ; n=0 → case 100
    .quad  .Ldefault        ; n=1 → default
    .quad  .Lcase102        ; n=2 → case 102
    .quad  .Lcase103        ; n=3 → case 103
    .quad  .Lcase104        ; n=4 → case 104/106
    .quad  .Ldefault        ; n=5 → default
    .quad  .Lcase104        ; n=6 → case 106
```

> **关键设计**：多个 `case` 可指向同一目标（`case 104` 和 `106`），`default` 填充空缺位置。

---

## 七、过程（Procedures）

### 7.1 栈帧结构

调用时，每个函数在运行时栈上占据一个**栈帧（Stack Frame）**：

```
高地址
┌──────────────────────────────┐
│  调用者（Caller）的栈帧        │
│  ...                         │
│  参数 n（第7个及之后的参数）   │ ← 调用者负责压栈
│  参数 7                       │
│  返回地址（由 call 压入）      │ ← %rsp（call 之后）
├──────────────────────────────┤
│  保存的 %rbp（可选）           │
│  被调用者保存的寄存器           │
│  局部变量                     │
│  临时数据                     │
└──────────────────────────────┘ ← %rsp（当前帧底部）
低地址
```

### 7.2 调用约定（System V AMD64 ABI）

**参数传递**（前 6 个整数/指针参数）：

| 参数顺序 | 寄存器 |
|---------|--------|
| 第 1 个 | `%rdi` |
| 第 2 个 | `%rsi` |
| 第 3 个 | `%rdx` |
| 第 4 个 | `%rcx` |
| 第 5 个 | `%r8`  |
| 第 6 个 | `%r9`  |
| 第 7+ 个 | 通过栈传递（逆序压栈） |

**返回值**：整数/指针返回值放在 `%rax`（大值用 `%rax:%rdx`）。

**寄存器保存约定**：

| 类型 | 寄存器 | 说明 |
|------|--------|------|
| **调用者保存**（Caller-saved） | `%rax`, `%rcx`, `%rdx`, `%rsi`, `%rdi`, `%r8–%r11` | 被调用函数可随意修改，调用者若需要则自行保存 |
| **被调用者保存**（Callee-saved） | `%rbx`, `%rbp`, `%r12–%r15` | 被调用函数若要使用，必须先保存再使用，返回前恢复 |
| **特殊** | `%rsp` | 栈指针，调用前后必须恢复 |

### 7.3 call 与 ret 指令

```asm
call   Label   ; 1. %rsp -= 8; Mem[%rsp] = 下一条指令地址（返回地址）
               ; 2. %rip = Label
ret            ; 1. %rip = Mem[%rsp]（弹出返回地址）
               ; 2. %rsp += 8
```

### 7.4 过程调用示例

```c
long incr(long *p, long val) {
    long x = *p;
    long y = x + val;
    *p = y;
    return x;
}

long call_incr() {
    long v1 = 15213;
    long v2 = incr(&v1, 3000);
    return v1 + v2;
}
```

```asm
call_incr:
    subq   $16, %rsp        ; 分配栈帧（16字节对齐）
    movq   $15213, 8(%rsp)  ; v1 = 15213，存在栈上
    movl   $3000, %esi      ; 第2参数 val = 3000
    leaq   8(%rsp), %rdi    ; 第1参数 &v1（栈上地址）
    call   incr             ; 调用 incr
    addq   8(%rsp), %rax    ; v1 + incr的返回值
    addq   $16, %rsp        ; 释放栈帧
    ret
```

### 7.5 递归

递归通过栈帧自然实现，每次调用有独立的局部变量副本：

```c
long rfact(long n) {
    if (n <= 1) return 1;
    return n * rfact(n - 1);
}
```

```asm
rfact:
    pushq  %rbx              ; 保存 %rbx（被调用者保存）
    movq   %rdi, %rbx        ; %rbx = n（保存跨调用需要的值）
    movl   $1, %eax          ; 默认返回值 = 1
    cmpq   $1, %rdi          ; n <= 1?
    jle    .Lreturn
    leaq   -1(%rdi), %rdi    ; 参数 = n - 1
    call   rfact             ; 递归调用
    imulq  %rbx, %rax        ; n × rfact(n-1)
.Lreturn:
    popq   %rbx              ; 恢复 %rbx
    ret
```

**栈对齐要求**：调用 `call` 之前，`%rsp` 必须是 16 字节对齐的。

---

## 八、数组（Array Allocation and Access）

### 8.1 一维数组

声明 `T A[N]` 分配 `N × sizeof(T)` 字节连续空间，`A` 即首元素地址。

**访问公式**：`&A[i] = x_A + L × i`，其中 `L = sizeof(T)`。

```c
int E[5] = {1, 2, 3, 4, 5};  // 假设起始地址为 0x100

E[i]  等价于  *(E + i)
```

```asm
; int E[] 在 %rdi，i 在 %rsi
movl  (%rdi, %rsi, 4), %eax   ; %eax = E[i]（sizeof(int)=4）
```

### 8.2 多维数组（Row-Major 布局）

C 多维数组按**行优先（row-major）**存储：

```c
int A[5][3];   // 5行3列的整数数组
```

元素 `A[i][j]` 的地址：`x_A + 4 × (3i + j)`（`sizeof(int)=4`）

```asm
; A 在 %rdi，i 在 %rsi，j 在 %rdx
leaq  (%rsi, %rsi, 2), %rax   ; %rax = 3i
addl  %edx, %eax               ; %rax = 3i + j
movl  (%rdi, %rax, 4), %eax   ; %eax = A[i][j]
```

### 8.3 定长数组 vs 变长数组（VLA）

```c
/* 定长数组：编译时已知维度 */
#define N 16
int fixed[N][N];

/* 变长数组（VLA）：运行时确定维度，C99 引入 */
int vla[n][n];   // n 在运行时确定
```

VLA 在函数局部使用时分配在栈上，维度信息由额外寄存器或内存维护。

---

## 九、异构数据结构（Heterogeneous Data Structures）

### 9.1 结构体（Struct）

字段按声明顺序分配，编译器保证满足**对齐约束**（每个字段的起始地址是其大小的倍数）。

```c
struct rec {
    int i;      // 偏移 0，4字节
    int j;      // 偏移 4，4字节
    int a[2];   // 偏移 8，8字节
    int *p;     // 偏移 16，8字节（指针8字节）
};  // 总大小 24
```

```asm
; struct rec *r 在 %rdi，访问 r->i
movl  (%rdi), %eax         ; %eax = r->i（偏移0）
; 访问 r->a[1]
movl  12(%rdi), %eax       ; %eax = r->a[1]（偏移8 + 4×1）
; r->p = &r->a[r->i]
movl  (%rdi), %eax         ; %eax = r->i
cltq                        ; 符号扩展到64位
leaq  8(%rdi,%rax,4), %rax ; &r->a[r->i] = r + 8 + 4×i
movq  %rax, 16(%rdi)       ; r->p = ...
```

### 9.2 联合体（Union）

所有字段共用**同一块内存**，大小等于最大字段大小：

```c
union Data {
    int i;       // 4字节
    float f;     // 4字节
    double d;    // 8字节
};  // 总大小 8（由 double 决定）
```

**类型双关（Type Punning）**用途：将 `double` 的位模式视作 `unsigned long`：

```c
unsigned long double2bits(double d) {
    union { double d; unsigned long u; } temp;
    temp.d = d;
    return temp.u;
}
```

### 9.3 数据对齐（Data Alignment）

**对齐规则**：基本类型 `T` 的变量地址必须是 `sizeof(T)` 的倍数。

| 类型 | 大小 | 对齐要求 |
|------|------|---------|
| `char` | 1 | 1（无要求） |
| `short` | 2 | 2字节对齐 |
| `int`/`float` | 4 | 4字节对齐 |
| `long`/`double`/指针 | 8 | 8字节对齐 |

**结构体对齐示例**：

```c
struct S1 {
    char c;     // 偏移 0
    // 3字节填充 (padding)
    int i;      // 偏移 4
    char d;     // 偏移 8
    // 3字节填充
};  // 总大小 12（非最优）

struct S2 {    // 字段按大小降序排列（最优）
    int i;      // 偏移 0
    char c;     // 偏移 4
    char d;     // 偏移 5
    // 2字节填充
};  // 总大小 8
```

**结构体末尾填充**：结构体整体大小必须是其**最大字段对齐要求**的倍数（使数组中相邻元素也满足对齐）。

> **建议**：按字段大小从大到小声明，减少填充浪费。

---

## 十、控制与数据结合（Combining Control and Data）

### 10.1 理解指针

- `int *ip = &x`：`ip` 存储 `x` 的地址
- `*ip`：解引用，读写 `x` 的值  
- `*(ip + i)` ≡ `ip[i]`：数组指针算术，步长 = `sizeof(int) = 4`
- 函数指针：`int (*fp)(int, int)` — 指向接收两个 `int` 返回 `int` 的函数

### 10.2 缓冲区溢出（Buffer Overflow）

**问题根源**：C 不自动检查数组边界，向固定大小缓冲区写入超量数据会覆盖栈上的相邻内存。

```c
/* 不安全：gets() 不限制输入长度 */
void echo() {
    char buf[4];   // 栈上只有 4 字节
    gets(buf);     // 若输入超过3字节（含'\0'）则溢出！
}
```

```
栈帧布局（从低到高）：
┌──────────┐
│ buf[0-3] │ ← 4字节缓冲区
│ 保存%rbp │ ← 覆盖帧指针
│ 返回地址  │ ← 覆盖返回地址 → 攻击者控制 %rip
└──────────┘
```

**攻击原理**：输入超出缓冲区后，覆盖返回地址，使其指向**注入的恶意代码**（shellcode）。

### 10.3 安全防御机制

#### 栈金丝雀（Stack Canary / Stack Protector）

GCC `-fstack-protector`（现默认开启）：在缓冲区和返回地址之间插入随机**哨兵值（canary）**，函数返回前检查其完整性：

```asm
; 函数入口：设置金丝雀
movq  %fs:40, %rax       ; 从线程局部存储读取随机值
movq  %rax, -8(%rbp)     ; 存到栈帧中（缓冲区之上）
...
; 函数出口：验证金丝雀
movq  -8(%rbp), %rax
xorq  %fs:40, %rax        ; 与原值异或
jne   __stack_chk_fail    ; 不等则崩溃（已被篡改）
```

#### 地址空间布局随机化（ASLR）

每次程序运行时，栈、堆、库的**加载地址随机化**，攻击者无法预测跳转目标。

```bash
$ cat /proc/sys/kernel/randomize_va_space   # 2 = 完全随机化
```

#### 不可执行栈（NX / DEP）

操作系统将栈页标记为**不可执行（NX bit）**，即使攻击者注入了代码也无法执行。

**绕过**：**面向返回的编程（ROP, Return-Oriented Programming）**——攻击者不注入代码，而是链接已有代码片段（gadgets）的尾部（`ret` 指令前）来构造攻击。这是现代攻击的主要形式。

---

## 十一、浮点代码（Floating-Point Code）

x86-64 使用 **SSE/AVX 指令集**处理浮点，使用 `%xmm0–%xmm15` 寄存器（128/256位）。

### 基本规则

- `float` 参数通过 `%xmm0–%xmm7` 传递（最多 8 个）
- 返回值在 `%xmm0`
- 所有 `%xmm` 寄存器均为**调用者保存**

```c
double fadd(double x, double y) {
    return x + y;
}
```

```asm
fadd:
    addsd  %xmm1, %xmm0   ; %xmm0 = x + y（双精度标量加法）
    ret
```

**常用浮点指令**：

| 指令 | 操作 |
|------|------|
| `addsd / addss` | 双精度/单精度加法 |
| `subsd / subss` | 减法 |
| `mulsd / mulss` | 乘法 |
| `divsd / divss` | 除法 |
| `sqrtsd` | 平方根 |
| `ucomisd` | 无序比较（设置条件码） |
| `cvtsi2sd / cvtsi2ss` | 整数→浮点转换 |
| `cvtsd2si / cvtss2si` | 浮点→整数（截断）转换 |

---

## 十二、要点速览

1. **汇编后缀**：`b/w/l/q` 对应 1/2/4/8 字节，`movl` 写32位寄存器自动清零高32位。
2. **16个寄存器**：前6个参数用 `%rdi/%rsi/%rdx/%rcx/%r8/%r9`，返回值用 `%rax`。
3. **LEA 妙用**：`leaq (,%rdi,5), %rax` = 5×%rdi，无需乘法指令，编译器的常用技巧。
4. **条件码**：`cmp/test` 设置但不保存结果；`setXX` 读取，`jXX` 分支，`cmovXX` 条件传送。
5. **循环**：GCC 常将 `while/for` 转为 do-while 形式，减少一次跳转。
6. **调用约定**：被调用者必须保存 `%rbx/%rbp/%r12–%r15`；调用前 `%rsp` 16字节对齐。
7. **缓冲区溢出**：`gets/scanf` 无边界检查，覆盖返回地址 → 防御：栈金丝雀 + ASLR + NX。
8. **对齐**：结构体字段按大小降序排列可减少填充；末尾对齐到最大字段大小的倍数。
