# Bomb Lab — 拆弹实战手册

> **目标**：通过逆向分析汇编代码，找到 6 个 phase 的正确输入，拆除炸弹。
> 核心能力：读懂 x86-64 汇编、用 GDB 动态调试、理解常见代码模式。

---

## 一、实验概述

### 1.1 任务说明

程序 `bomb` 在运行时依次要求输入 6 个 phase，每个 phase 输入正确才能继续，输入错误则 `BOOM`（炸弹爆炸）。

我们没有源代码（除了框架 `bomb.c`），唯一的武器是：
- **反汇编**：把二进制还原成汇编指令
- **GDB 调试**：运行时查看寄存器和内存
- **逻辑推理**：从汇编还原程序意图

### 1.2 文件说明

| 文件 | 说明 |
|------|------|
| `bomb` | 可执行程序（我们的分析对象） |
| `bomb.c` | 框架代码（只能看 main 结构，phase 实现未给出） |
| `result.txt` | 存放已解出答案，用 `./bomb result.txt` 跳过已解阶段 |

**bomb.c 框架结构（关键理解）：**
```c
input = read_line();
phase_1(input);          // 传入我们输入的字符串
phase_defused();         // 通关提示
// ... phase_2 到 phase_6 重复相同模式
```

每个 `phase_x(input)` 内部会验证输入，不匹配就调用 `explode_bomb()`。

### 1.3 工具快速参考

**第一步：反汇编**
```bash
objdump -d bomb > bomb.asm    # 生成汇编文件，用编辑器打开分析
```

**第二步：用 GDB 调试**
```bash
gdb bomb                       # 进入调试
(gdb) layout asm               # 显示汇编视图
(gdb) layout regs              # 显示寄存器视图
(gdb) file bomb                # 加载程序
(gdb) b phase_1                # 在 phase_1 入口打断点
(gdb) r result.txt             # 运行（从文件读取已知答案）
```

**常用 GDB 命令速查：**

| 命令 | 作用 |
|------|------|
| `b *0x400ee0` | 在地址 0x400ee0 打断点 |
| `r` / `c` | 运行 / 继续执行 |
| `ni` / `si` | 单步（不进入/进入函数） |
| `p $rax` | 打印寄存器值（十进制） |
| `p/x $rsp` | 打印寄存器值（十六进制） |
| `x/s 0x402400` | 把地址 0x402400 当作 C 字符串打印 |
| `x/d $rsp` | 打印 rsp 指向的值（十进制） |
| `x/gx $rsp` | 打印 rsp 指向的 8 字节（十六进制） |
| `x/6d $rsp` | 打印 rsp 起的 6 个 int（十进制） |
| `disas phase_2` | 反汇编 phase_2 函数 |
| `info registers` | 查看所有寄存器 |

---

## 二、核心知识：读汇编的基础

拆弹前，先掌握这些"汇编语言密码本"。

### 2.1 x86-64 寄存器功能速查

```
函数参数（从左到右）:  rdi  rsi  rdx  rcx  r8   r9
函数返回值:            rax
调用者保存（自己备份）: rax  rcx  rdx  rsi  rdi  r8   r9   r10  r11
被调用者保存（帮你保留）: rbx  rbp  r12  r13  r14  r15
栈指针:               rsp
```

> **关键推论**：
> - 看到 `callq <some_function>`，函数的第一个参数在 `%rdi`，第二个在 `%rsi`，以此类推。
> - 函数返回后，结果在 `%eax`（32位）或 `%rax`（64位）。

### 2.2 常见汇编模式识别

**模式一：字符串比较**
```asm
mov $0x402400, %esi          ; 第二个参数 = 某个地址（很可能是字符串）
callq strings_not_equal      ; 比较两个字符串
test %eax, %eax              ; 检查返回值
je  <通关>                   ; 相等则通关
callq explode_bomb           ; 不相等则爆炸
```
→ 策略：用 `x/s 0x402400` 打印那个地址里存的字符串

**模式二：读取多个整数**
```asm
callq read_six_numbers       ; 读取 6 个整数
```
→ 策略：知道调用后整数存在栈上（rsp 为基址），用 `x/6d $rsp` 查看

**模式三：条件判断**
```asm
cmp $0x7, %eax               ; 比较 eax 和 7
ja  explode_bomb             ; 如果 eax > 7（无符号）则爆炸
```
→ 注意：`ja`/`jb` 是无符号比较；`jg`/`jl` 是有符号比较

**模式四：间接跳转（switch 语句）**
```asm
jmpq *0x402470(,%rax,8)      ; 跳到 [0x402470 + rax*8] 存的地址
```
→ 策略：用 `x/gx 0x402470` 查看跳转表内容

**模式五：栈保护金丝雀**
```asm
mov %fs:0x28, %rax           ; 读取金丝雀值
mov %rax, 0x18(%rsp)         ; 存入栈中
...
xor %fs:0x28, %rax           ; 退出时验证（忽视这部分）
```
→ 这是缓冲区溢出检测，不影响 phase 的逻辑，**看到直接跳过**。

### 2.3 工作流程模板

```
每个 phase 的分析步骤：

1. 在 bomb.asm 中找到 phase_x 函数
2. 识别：输入了几个值？（看 sscanf 格式串）
3. 识别：什么条件会让程序跳到 explode_bomb？
4. 找到通关条件（避开所有 explode_bomb 的路径）
5. 用 GDB 验证猜测（打断点，查看内存/寄存器）
```

---

## 三、各阶段拆弹

### Phase 1：字符串匹配

**汇编代码：**
```asm
0000000000400ee0 <phase_1>:
  400ee0: 48 83 ec 08    sub  $0x8,%rsp
  400ee4: be 00 24 40 00 mov  $0x402400,%esi    ; 设置第二个参数 = 某地址
  400ee9: e8 4a 04 00 00 callq 401338 <strings_not_equal>
  400eee: 85 c0          test %eax,%eax
  400ef0: 74 05          je   400ef7            ; 若返回值=0（相等）则通关
  400ef2: e8 43 05 00 00 callq 40143a <explode_bomb>
  400ef7: 48 83 c4 08    add  $0x8,%rsp
  400efb: c3             retq
```

**逻辑还原：**
```
如果 strings_not_equal(我们的输入, 0x402400处的字符串) == 0
    → 通关（两字符串相等）
否则 → 爆炸
```

**关键问题**：`0x402400` 里存的是什么字符串？

**调试步骤：**
```bash
gdb bomb
(gdb) b *0x400ee4           # 在设置参数处打断点
(gdb) r                     # 随便输入一行触发
(gdb) x/s 0x402400          # 打印那个地址的字符串 → 这就是答案！
```

**答案**：`Border relations with Canada have never been better.`

---

### Phase 2：等比数列

**汇编逻辑分析：**

```asm
400f02: mov %rsp,%rsi
400f05: callq 40145c <read_six_numbers>   ; 读取 6 个整数到栈
400f0a: cmpl $0x1,(%rsp)                  ; 第一个数必须 == 1
400f0e: je   400f30                       ; 等于则跳到循环初始化
400f10: callq explode_bomb                ; 不等则爆炸

; 循环初始化：rbx = &numbers[1], rbp = &numbers[6]（哨兵）
400f30: lea 0x4(%rsp),%rbx               ; rbx → 第二个数
400f35: lea 0x18(%rsp),%rbp              ; rbp → 越界位置

; 循环体
400f17: mov -0x4(%rbx),%eax             ; eax = 前一个数
400f1a: add %eax,%eax                   ; eax = eax * 2
400f1c: cmp %eax,(%rbx)                 ; 当前数 == 前一个数 * 2 ?
400f1e: je  400f25                      ; 是则继续
400f20: callq explode_bomb              ; 否则爆炸
400f25: add $0x4,%rbx                   ; 移向下一个数
400f29: cmp %rbp,%rbx                   ; 检查是否到末尾
400f2c: jne 400f17                      ; 未到则循环
```

**逻辑还原：**
```
numbers[0] == 1
for i in 1..5:
    if numbers[i] != numbers[i-1] * 2:
        爆炸
```

**答案**：`1 2 4 8 16 32`

---

### Phase 3：switch 跳转表

**理解 sscanf 读入：**
```asm
400f47: lea 0xc(%rsp),%rcx    ; 第二个整数地址 → rsp+0xc
400f4c: lea 0x8(%rsp),%rdx    ; 第一个整数地址 → rsp+0x8
400f51: mov $0x4025cf,%esi    ; 格式字符串
400f5b: callq sscanf
```

用 `x/s 0x4025cf` 查看格式串 → `"%d %d"`，即输入两个整数。

**间接跳转（跳转表）：**
```asm
400f6a: cmpl $0x7, 0x8(%rsp)    ; 第一个数必须 <= 7
400f6f: ja   explode_bomb        ; > 7 则爆炸
400f71: mov  0x8(%rsp),%eax      ; eax = 第一个数（索引）
400f75: jmpq *0x402470(,%rax,8)  ; 跳到 [0x402470 + 索引*8] 存的地址
```

**查看跳转表（GDB）：**
```bash
(gdb) x/8gx 0x402470    # 查看 8 个跳转目标地址
```

**每个索引对应的第二个数：**

| 第一个数（索引） | 第二个数 |
|---|---|
| 0 | 207 (0xcf) |
| 1 | 311 (0x137) |
| 2 | 707 (0x2c3) |
| 3 | 256 (0x100) |
| 4 | 389 (0x185) |
| 5 | 206 (0xce) |
| 6 | 682 (0x2aa) |
| 7 | 327 (0x147) |

**答案（任选其一）**：`0 207` 或 `1 311` 或 `3 256` 等

---

### Phase 4：递归二分查找

**phase_4 整体逻辑：**
```asm
; 读取两个整数
401029: cmp $0x2,%eax      ; 必须读到 2 个整数
40102e: cmpl $0xe,0x8(%rsp) ; 第一个数必须 <= 14 (0xe)

; 调用 func4(input[0], 0, 14)
40103a: mov $0xe,%edx      ; 第三参数 = 14
40103f: mov $0x0,%esi      ; 第二参数 = 0
401044: mov 0x8(%rsp),%edi ; 第一参数 = 输入的第一个数
401048: callq func4

40104d: test %eax,%eax     ; func4 返回值必须 == 0
40104f: jne explode_bomb

401051: cmpl $0x0,0xc(%rsp) ; 第二个数必须 == 0
401056: je  通关
```

**func4 的 C 语言等价：**
```c
int func4(int x, int lo, int hi) {
    int mid = lo + (hi - lo) / 2;   // 区间中点
    if (x < mid) 
        return 2 * func4(x, lo, mid - 1);   // 左半区间，返回值 * 2
    else if (x > mid) 
        return 2 * func4(x, mid + 1, hi) + 1;  // 右半区间，返回值 * 2 + 1
    else 
        return 0;   // 找到了！返回 0
}
```

**目标**：找到使 `func4(x, 0, 14)` 返回 0 的 x 值。

返回 0 只有一个路径：每次递归都命中 mid（走 `else` 分支）。

初始调用 `func4(x, 0, 14)`：
- 第一层：mid = `0 + (14-0)/2 = 7`，命中 → x=7，直接返回 0
- 若走左边：`func4(x, 0, 6)` → mid = 3，命中 → x=3，返回 `2*0=0` ✓
- 再往左：`func4(x, 0, 2)` → mid = 1，命中 → x=1，返回 `2*0=0` ✓
- 再往左：`func4(x, 0, 0)` → mid = 0，命中 → x=0，返回 `2*0=0` ✓

**答案（任选其一）**：`7 0` 或 `3 0` 或 `1 0` 或 `0 0`

---

### Phase 5：字符映射

**逻辑分析：**
```asm
; 检查长度 == 6
40107a: callq string_length
40107f: cmp $0x6,%eax
401082: je  进入循环
401084: callq explode_bomb

; 循环体（处理每个字符）
40108b: movzbl (%rbx,%rax,1),%ecx  ; 取第 rax 个字符
401096: and $0xf,%edx              ; 取低 4 位（0~15）
401099: movzbl 0x4024b0(%rdx),%edx ; 用低4位作索引，从查找表取字符
4010a0: mov %dl,0x10(%rsp,%rax,1)  ; 存入新字符串

; 比较结果字符串
4010b3: mov $0x40245e,%esi         ; 目标字符串地址
4010bd: callq strings_not_equal
```

**查看关键数据（GDB）：**
```bash
(gdb) x/s 0x4024b0    # 查找表
# → "maduiersnfotvbylSo you think you can stop the bomb..."
# 索引:  0123456789...  
#  m=0, a=1, d=2, u=3, i=4, e=5, r=6, s=7, n=8, f=9, o=10, t=11, v=12, b=13, y=14, l=15

(gdb) x/s 0x40245e    # 目标字符串
# → "flyers"
```

**逆推过程：**

目标字符串 `flyers` 在查找表中的索引：
```
f → 索引 9    (0x4024b0[9] = 'f')
l → 索引 15   (0x4024b0[15] = 'l')
y → 索引 14   (0x4024b0[14] = 'y')
e → 索引 5    (0x4024b0[5] = 'e')
r → 索引 6    (0x4024b0[6] = 'r')
s → 索引 7    (0x4024b0[7] = 's')
```

所以输入字符串每个字符的低 4 位必须分别是 `9, 15, 14, 5, 6, 7`。

ASCII 字符低 4 位查找：
- 低4位为 9 → 如 `9`(0x39), `I`(0x49), `Y`(0x59), `i`(0x69), `y`(0x79)
- 低4位为 15 → 如 `?`(0x3F), `O`(0x4F), `o`(0x6F)
- ... （任选满足低4位的字符即可）

**答案（之一）**：`ionefg`（i=0x69低4位9, o=0x6F低4位15, n=0x6E低4位14, e=0x65低4位5, f=0x66低4位6, g=0x67低4位7）

---

### Phase 6：链表排序

这是最复杂的 phase，分五步理解。

**第一步：读入并验证**
```asm
callq read_six_numbers    ; 读入 6 个整数
; 约束：每个数在 1~6 之间，且互不相同（1到6的排列）
```

**第二步：数字映射**
```asm
; 将每个数 a[i] 替换为 7 - a[i]
; 即：1↔6, 2↔5, 3↔4（互换）
```

**第三步：构建链表指针数组**

程序内存中有一个固定链表：
```
node1(0x6032d0) → node2(0x6032e0) → node3(0x6032f0)
    → node4(0x603300) → node5(0x603310) → node6(0x603320)
```

用 GDB 查看每个节点的值：
```bash
(gdb) x/d 0x6032d0    # → 332   (node1)
(gdb) x/d 0x6032e0    # → 168   (node2)
(gdb) x/d 0x6032f0    # → 924   (node3)
(gdb) x/d 0x603300    # → 691   (node4)
(gdb) x/d 0x603310    # → 477   (node5)
(gdb) x/d 0x603320    # → 443   (node6)
```

按照映射后的数组，把对应的节点指针存入新数组。
（映射后数组中 1 对应 node1，2 对应 node2，…）

**第四步：按新顺序重建链表**

把存入新数组的节点指针依次串成链表。

**第五步：验证链表值降序**
```asm
; 遍历新链表，要求每个节点的值 >= 下一个节点的值
; 否则爆炸
```

**逆推答案：**

节点值从大到小排序：
```
node3(924) > node4(691) > node5(477) > node6(443) > node1(332) > node2(168)
```
对应节点编号序列：`3 4 5 6 1 2`

这是**映射后**数组的排列（映射后数字 i 放在第 i 个节点的位置）。

映射前（7 - 映射后）：
```
7-3=4, 7-4=3, 7-5=2, 7-6=1, 7-1=6, 7-2=5
```

**答案**：`4 3 2 1 6 5`

---

### Phase 7（彩蛋）：隐藏关卡

**触发条件分析：**

`phase_defused` 函数在完成 6 个 phase 后执行。分析它的汇编：

```asm
; 检查是否已完成 6 个 phase（全局变量 num_input_strings == 6）
cmpl $0x6, 0x202181(%rip)   ; 若 != 6 则跳过隐藏关卡

; 重新解析第 4 个 phase 的输入（格式为 "%d %d %s"）
mov $0x402619, %esi          ; 格式串 → 用 x/s 0x402619 查看
mov $0x603870, %edi          ; phase_4 的输入字符串地址

; 如果解析出了第三个字段（字符串），且它等于 "DrEvil"
; 则调用 secret_phase()
```

**触发方法**：在 phase_4 的输入末尾加上 ` DrEvil`：
```
0 0 DrEvil
```

**secret_phase 分析：**
```asm
0000000000401242 <secret_phase>:
; 读取一个整数，要求 <= 1001
; 调用 fun7(n) 要求返回 2
```

`fun7` 是对一棵二叉搜索树的遍历，树根在 `0x6030f0`。使用 GDB 遍历树，找到使 `fun7` 返回 2 的输入值。

**调试提示：**
```bash
(gdb) x/3gx 0x6030f0    # 查看根节点：[value, left_child_addr, right_child_addr]
```

**答案**：`22`（具体值取决于二叉树结构，可用 GDB 验证）

---

## 四、完整答案汇总

将以下内容保存到 `result.txt`：
```
Border relations with Canada have never been better.
1 2 4 8 16 32
0 207
7 0
ionefg
4 3 2 1 6 5
```

运行验证：
```bash
./bomb result.txt
```

如要触发隐藏关卡，把 phase_4 的答案改为：
```
0 0 DrEvil
```

---

## 五、总结：拆弹方法论

| Phase | 知识点 | 关键技巧 |
|-------|--------|---------|
| Phase 1 | 字符串比较 | `x/s` 打印目标字符串 |
| Phase 2 | 循环/等比数列 | 识别循环结构，找初始条件 |
| Phase 3 | switch/跳转表 | `x/gx` 查看跳转表，逐一测试 |
| Phase 4 | 递归函数逆推 | 还原 C 代码，找返回 0 的路径 |
| Phase 5 | 字符映射/查表 | 打印查找表，逆推字符低 4 位 |
| Phase 6 | 链表排序 | 分步分析，GDB 查节点值 |

**核心思路**：
1. 从 `explode_bomb` 往上反推，找到所有"爆炸条件"
2. 构造"永远不爆炸"的输入路径
3. 遇到无法静态分析的地方，用 GDB 动态查看

**GDB 调试心法**：
- 不确定地址存的内容 → `x/s` 或 `x/d` 打印
- 不确定寄存器的值 → `p $rax`
- 不确定栈的内容 → `x/6d $rsp`
- 遇到函数调用不明白 → `si` 进入，`disas` 看汇编
