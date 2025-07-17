# Bomb Lab

## 简介

### 实验要求

**题目要求：** 运行 bomb 后在 6 个 phase 输入正确的内容，输入正确 bomb 程序才能继续运行，输入错误就会 bomb!  。如果是CMU的学生，每个人获得的炸弹是不一样的（高超的 反作弊技巧），每次爆炸会扣0.5分，扣满20分为止。不过我们作为自学党，没有这个限制，用的是自学用bomb，随便炸，根本不虚，不过为了达成良好的学习效果，还是尽可能减少爆炸次数。

实验文件
-  [bomb](../labs/bomb/bomb) 程序文件
-  [bomb.c](../labs/bomb/bomb.c) 包括题目要求和 bomb 实现的代码框架，无法编译。

### 阅读源码

```c
#include <stdio.h>
#include <stdlib.h>
#include "support.h"
#include "phases.h"

/* 
 * Note to self: Remember to erase this file so my victims will have no
 * idea what is going on, and so they will all blow up in a
 * spectaculary fiendish explosion. -- Dr. Evil 
 */

FILE *infile;

int main(int argc, char *argv[])
{
    char *input;

    /* Note to self: remember to port this bomb to Windows and put a 
     * fantastic GUI on it. */

    /* When run with no arguments, the bomb reads its input lines 
     * from standard input. */
    if (argc == 1) {  
	infile = stdin;
    } 

    /* When run with one argument <file>, the bomb reads from <file> 
     * until EOF, and then switches to standard input. Thus, as you 
     * defuse each phase, you can add its defusing string to <file> and
     * avoid having to retype it. */
    else if (argc == 2) {
	if (!(infile = fopen(argv[1], "r"))) {
	    printf("%s: Error: Couldn't open %s\n", argv[0], argv[1]);
	    exit(8);
	}
    }

    /* You can't call the bomb with more than 1 command line argument. */
    else {
	printf("Usage: %s [<input_file>]\n", argv[0]);
	exit(8);
    }

    /* Do all sorts of secret stuff that makes the bomb harder to defuse. */
    initialize_bomb();

    printf("Welcome to my fiendish little bomb. You have 6 phases with\n");
    printf("which to blow yourself up. Have a nice day!\n");

    /* Hmm...  Six phases must be more secure than one phase! */
    input = read_line();             /* Get input                   */
    phase_1(input);                  /* Run the phase               */
    phase_defused();                 /* Drat!  They figured it out!
				      * Let me know how they did it. */
    printf("Phase 1 defused. How about the next one?\n");

    /* The second phase is harder.  No one will ever figure out
     * how to defuse this... */
    input = read_line();
    phase_2(input);
    phase_defused();
    printf("That's number 2.  Keep going!\n");

    /* I guess this is too easy so far.  Some more complex code will
     * confuse people. */
    input = read_line();
    phase_3(input);
    phase_defused();
    printf("Halfway there!\n");

    /* Oh yeah?  Well, how good is your math?  Try on this saucy problem! */
    input = read_line();
    phase_4(input);
    phase_defused();
    printf("So you got that one.  Try this one.\n");
    
    /* Round and 'round in memory we go, where we stop, the bomb blows! */
    input = read_line();
    phase_5(input);
    phase_defused();
    printf("Good work!  On to the next...\n");

    /* This phase will never be used, since no one will get past the
     * earlier ones.  But just in case, make this one extra hard. */
    input = read_line();
    phase_6(input);
    phase_defused();

    /* Wow, they got it!  But isn't something... missing?  Perhaps
     * something they overlooked?  Mua ha ha ha ha! */
    
    return 0;
}
```

源码即为 `bomb.c` ，可以看到这个程序的主要逻辑大概是：每个阶段都是通过 `phase_x` 函数来实现的，而 `phase_x` 函数的参数是 `input`，也就是我们输入的字符串。所以，我们的目标就是**通过分析 `phase_x` 函数来找到正确的 `input`**。

根据题目的要求以及提示，**可以将`bomb`可执行文件反汇编，对汇编语言代码进行逆向分析**。

### 工具列表

`objdump`-用于反汇编二进制对象文件

`VS Code`-用于查看反汇编后的结果与文本文件的编写，详见环境构建

`gdb`-用于运行时单步调试与查看运行时内存与寄存器信息

### gdb 指令

| 指令  | 全称  | 功能  |
|---|---|---|
|  gdb filename |   | 开始调试程序  |
|  r | run  | 开始执行程序，直到下一个断点或程序结束；run 1 2 3，开始运行病传入参数 1 2 3  |
|  q | quit  | 退出 GDB 调试器  |
|  ni | nexti  | 执行下一条指令，但不进入函数内部  |
|  si | stepi  | 执行当前指令，如果是函数调用则进入函数  |
|  b |  break | 在指定位置设置断点  |
|  d |  delete 1 | 删除断点1  |
|   |  clear sum | 删除sum函数入口的断点  |
|  c |  cont |  从当前位置继续执行程序，直到下一个断点或程序结束 |
|  p |  print | 打印变量的值  |
|  x |   | 打印内存中的值；</br>`x/w $rsp`	解析在rsp所指向位置的word；</br>`x/2w $rsp` 解析在rsp所指向位置的两个word；</br>`x/2wd $rsp` 解析在rsp所指向位置的word，以十进制形式输出	  |
|  j |  jump | 跳转到程序指定位置  |
|  disas |   | 反汇编当前函数或指定的代码区域  |
|  layout asm |   | 显示汇编代码视图  |
|  layout regs |   | 显示当前的寄存器状态和它们的值  |

关于 `p` 和 `x`，最重要的就是记得 `p` 命令用于打印表达式的值，而 `x` 命令则主要用于检查内存的内容。几个常用示例如下：

```bash
p $rax  # 打印寄存器 rax 的值
p $rsp  # 打印栈指针的值
p/x $rsp  # 打印栈指针的值，以十六进制显示
p/d $rsp  # 打印栈指针的值，以十进制显示

x/2x $rsp  # 以十六进制格式查看栈指针 %rsp 指向的内存位置 M[%rsp] 开始的两个单位。
x/2d $rsp # 以十进制格式查看栈指针 %rsp 指向的内存位置 M[%rsp] 开始的两个单位。
x/2c $rsp # 以字符格式查看栈指针 %rsp 指向的内存位置 M[%rsp] 开始的两个单位。
x/s $rsp # 把栈指针指向的内存位置 M[%rsp] 当作 C 风格字符串来查看。

x/b $rsp # 检查栈指针指向的内存位置 M[%rsp] 开始的 1 字节。
x/h $rsp # 检查栈指针指向的内存位置 M[%rsp] 开始的 2 字节（半字）。
x/w $rsp # 检查栈指针指向的内存位置 M[%rsp] 开始的 4 字节（字）。
x/g $rsp # 检查栈指针指向的内存位置 M[%rsp] 开始的 8 字节（双字）。

info registers  # 打印所有寄存器的值
info breakpoints  # 打印所有断点的信息

delete breakpoints 1  # 删除第一个断点，可以简写为 d 1
```

这些命令在 `/` 后面的后缀（如 `2x、2d、s、g、20c`）指定了查看内存的方式和数量。具体来说：

- 第一个数字（如 `2、20`）指定要查看的单位数量。

- 第二个字母（如 `x、d、s、g、c`）指定单位类型和显示格式，其中：
  - `c` / `d` / `x` 分别代表以字符、十进制、 十六进制格式显示内存内容。
  - `s` 代表以字符串格式显示内存内容。
  - `b` / `h` / `w` / `g` 分别代表以 1 / 2 / 4 / 8 字节为单位（`unit`）显示内存内容。当使用 `x/b`、`x/h`、`x/w`、`x/g` 时，`unit` 会记录对应改变，直到再次使用这些命令。

### x86-64 寄存器

![alt text](./pic/bomb/x86_64.png)

更多指令可以参考：
- [gdb 指令](http://csapp.cs.cmu.edu/2e/docs/gdbnotes-x86-64.pdf)           
- [马天猫的CS学习之旅](https://zhuanlan.zhihu.com/deeplearningcat) 
  
## 实验解析

### 反编译

首先让我们来反编译一下整个 `bomb` 这个二进制程序：

```bash
objdump -d bomb > bomb.asm
```

运行 bomb 程序，并使用 `layout asm`、`layout regs` 开启视图，方便分析。

```bash
gdb run
layout asm
layout regs
```


### phase_1

```asm
0000000000400ee0 <phase_1>:
  400ee0:	48 83 ec 08          	sub    $0x8,%rsp
  400ee4:	be 00 24 40 00       	mov    $0x402400,%esi
  400ee9:	e8 4a 04 00 00       	callq  401338 <strings_not_equal>
  400eee:	85 c0                	test   %eax,%eax
  400ef0:	74 05                	je     400ef7 <phase_1+0x17>
  400ef2:	e8 43 05 00 00       	callq  40143a <explode_bomb>
  400ef7:	48 83 c4 08          	add    $0x8,%rsp
  400efb:	c3                   	retq   
```

注意此处需要结合 x86-64 寄存器，明确每个寄存器的作用

- 第2行，为函数分配栈帧（`%rsp`为栈指针）
- 第3行，设置函数`strings_not_equal`传入参数,（`%esi`为栈指针）函数第二个参数
- 第4行，调用函数 `strings_not_equal`，从字面意思理解，猜想如果传入字符串不同，则返回0
- 第5、6行，函数`strings_not_equal`的返回值储存在`%eax`中（`%eax`表示函数返回值），判断其是否为0，若为0，则跳至第8行，函数返回，炸弹拆除成功；若不为0，则跳至第7行
- 第7行，调用`explode_bomb`函数，从字面意思理解，炸弹爆炸了。
- 第8行，清理栈空间

调试步骤:

```bash
gdb run 
file bomb
b *0x400ee4
r
x/s 0x402400  # 正确答案
quit
```

重新启动

```bash
gdb
layout regs
layout asm
file bomb
b phase_2
r
# 输入正确答案
Phase 1 defused. How about the next one?
c
```

### phase_2

```asm
0000000000400efc <phase_2>:
  400efc:	55                   	push   %rbp         ; 被调用者保存
  400efd:	53                   	push   %rbx         ；被调用者保存
  400efe:	48 83 ec 28          	sub    $0x28,%rsp   ; 分配40字节栈空间
  400f02:	48 89 e6             	mov    %rsp,%rsi    ; rsi = rsp (栈顶地址作为参数)
  400f05:	e8 52 05 00 00       	callq  40145c <read_six_numbers>    ; 调用read_six_numbers，读取6个数到栈上
  400f0a:	83 3c 24 01          	cmpl   $0x1,(%rsp)              ; 比较栈顶第一个数是否等于1
  400f0e:	74 20                	je     400f30 <phase_2+0x34>    ; 如果等于1，跳转到400f30
  400f10:	e8 25 05 00 00       	callq  40143a <explode_bomb>    ; 否则爆炸
  400f15:	eb 19                	jmp    400f30 <phase_2+0x34>    ; 跳转（冗余，因为爆炸后不会继续）
  ; 循环部分
  400f17:	8b 43 fc             	mov    -0x4(%rbx),%eax          ; 取前一个数（rbx-4）到eax
  400f1a:	01 c0                	add    %eax,%eax                ; eax = eax * 2
  400f1c:	39 03                	cmp    %eax,(%rbx)              ; 比较当前数(rbx指向)是否等于前一个数的两倍        
  400f1e:	74 05                	je     400f25 <phase_2+0x29>    ; 相等则跳转
  400f20:	e8 15 05 00 00       	callq  40143a <explode_bomb>    ; 否则爆炸
  400f25:	48 83 c3 04          	add    $0x4,%rbx                ; rbx + 4 指向下一个数
  400f29:	48 39 eb             	cmp    %rbp,%rbx                ; 比较rbx是否达到rbp（结束位置）
  400f2c:	75 e9                	jne    400f17 <phase_2+0x1b>    ; 未达到则继续循环
  400f2e:	eb 0c                	jmp    400f3c <phase_2+0x40>    ; 达到则跳出循环，函数结束
  ; 初始化循环指针
  400f30:	48 8d 5c 24 04       	lea    0x4(%rsp),%rbx           ; rbx指向第二个数（地址为rsp+4）
  400f35:	48 8d 6c 24 18       	lea    0x18(%rsp),%rbp          ; rbp指向结束位置（rsp+24，即第六个数之后）
  400f3a:	eb db                	jmp    400f17 <phase_2+0x1b>    ; 跳转到循环开始
  ; 清理栈并返回
  400f3c:	48 83 c4 28          	add    $0x28,%rsp
  400f40:	5b                   	pop    %rbx
  400f41:	5d                   	pop    %rbp
  400f42:	c3                   	retq
```

接下来将逐步分析代码

#### step 1

```asm
  400efc:	55                   	push   %rbp
  400efd:	53                   	push   %rbx
  400efe:	48 83 ec 28          	sub    $0x28,%rsp
```

- 第 2、3 行：将**被调用者保存寄存器的值**入栈
- 第 4 行：分配栈帧

#### step 2

```asm
  400f02:	48 89 e6             	mov    %rsp,%rsi
  400f05:	e8 52 05 00 00       	callq  40145c <read_six_numbers>
```
- 第 5 行：将栈顶指针`%rsp`传递给`%rsi`
- 第 6 行：将`%rsi`作为参数调用`read_six_number`
- 从字面意思理解，本题是要我们输入`6`个数字。这里`mov %rsp,%rsi`的目的是保存`caller`中栈顶的位置，方便在`read_six_numbers`中进行改值。我们不妨反汇编`read_six_numbers`

此时栈的情况为

![alt text](./pic/bomb/stack_1.png)

#### step3 反汇编 read_six_number

因为此时栈中的值从`read_six_number`中获取，所以不妨进入 `read_six_number`看一下其汇编实现

```asm
(gdb) disassemble read_six_numbers
Dump of assembler code for function read_six_numbers:
   0x000000000040145c <+0>:     sub    $0x18,%rsp       ; 分配 24 字节栈空间
   0x0000000000401460 <+4>:     mov    %rsi,%rdx        ; rdx = rsi (第 1 个整数地址)
   0x0000000000401463 <+7>:     lea    0x4(%rsi),%rcx   ; rcx = rsi + 4 （第 2 个整数地址）
   0x0000000000401467 <+11>:    lea    0x14(%rsi),%rax  ; rax = rsi + 20（第 6 个整数地址）
   0x000000000040146b <+15>:    mov    %rax,0x8(%rsp)   ; rsp + 8 = 第 6 个整数地址
   0x0000000000401470 <+20>:    lea    0x10(%rsi),%rax  ; rax = rsi + 16（第 5 个整数地址）
   0x0000000000401474 <+24>:    mov    %rax,(%rsp)      ; rsp = 第 5 个整数地址
   0x0000000000401478 <+28>:    lea    0xc(%rsi),%r9    ；r9 = rsi + 12 （第 4 个整数地址）
   0x000000000040147c <+32>:    lea    0x8(%rsi),%r8    ；r8 = rsi + 8  （第 3 个整数地址）
   0x0000000000401480 <+36>:    mov    $0x4025c3,%esi   ；esi = 格式字符串地址（"%d %d %d %d %d %d"）
   0x0000000000401485 <+41>:    mov    $0x0,%eax        ; eax = 0（清除返回值）
   0x000000000040148a <+46>:    callq  0x400bf0 <__isoc99_sscanf@plt> 调用 sscanf
   0x000000000040148f <+51>:    cmp    $0x5,%eax        ； 检查返回值（成功解析的整数数量）
   0x0000000000401492 <+54>:    jg     0x401499 <read_six_numbers+61> ；若 >5（即6个）则跳转
   0x0000000000401494 <+56>:    callq  0x40143a <explode_bomb>        ; 否则引爆炸弹
   0x0000000000401499 <+61>:    add    $0x18,%rsp       ; 恢复栈指针
   0x000000000040149d <+65>:    retq                    ; 返回
End of assembler dump. 
```

在`read_six_numbers`中，它使用了传入的`rsi`（即`phase_2`的`rsp`）作为数组基址，然后计算出6个地址：

**在这个函数中，要做到传6个参数，用来存储6个输入的数字**。

`sscanf`这个函数，有`8`个参数:
- 第一个是`input`
- 第二个是输入的模式串
- 之后`6`个是读取`6`个的值存的地址，因为最多只能传6个寄存器参数，所以最后两个把内存地址存在栈中 。六个地址分别存在`%rdx, %rcx, %r8, %r9, (%rsp), 8(%rsp)`处，同时也在`0x0(%rsi), 0x4(%rsi), 0x8(%rsi), 0xc(%rsi), 0x10(%rsi), 0x14(%rsi)`处。

由于`phase_2`函数中的栈指针`rsp`与这个函数中的`rsi`相等，所以把所有参数存在`rsi`之前的位置的目的是在返回`phase_2`函数后，能够**直接利用`phase_2`函数的栈指针来连续地访问这6个数字**

**在 `read_six_numbers` 中**：
| 表达式 | 内容 | 物理指向 |
|--------|------|----------|
| `%rsi` | 地址1 | `phase_2.rsp + 0` |
| `%rsi+4` | 地址2 | `phase_2.rsp + 4` |
| `%rsi+8` | 地址3 | `phase_2.rsp + 8` |
| `%rsi+0xC` | 地址4 | `phase_2.rsp + 12` |
| `(%rsp)` | 地址5 | `phase_2.rsp + 16` |
| `8(%rsp)` | 地址6 | `phase_2.rsp + 20` |

返回phase_2函数后，利用栈顶指针调用就是：`%rsp` `%rsp+0x4` `%rsp+0x8` `%rsp+0xc` `%rsp+0x10` `%rsp+0x14`

总结：两个函数不共用同一个栈帧（各自的rsp不同），但是read_six_numbers将数据写入了phase_2的栈帧（通过phase_2传递过来的rsp值，即rsi）。

#### step4 返回 phase_2 

```asm
0000000000400efc <phase_2>:
  400efc:	55                   	push   %rbp         ; 被调用者保存
  400efd:	53                   	push   %rbx         ；被调用者保存
  400efe:	48 83 ec 28          	sub    $0x28,%rsp   ; 分配40字节栈空间
  400f02:	48 89 e6             	mov    %rsp,%rsi    ; rsi = rsp (栈顶地址作为参数)
  400f05:	e8 52 05 00 00       	callq  40145c <read_six_numbers>    ; 调用read_six_numbers，读取6个数到栈上
  400f0a:	83 3c 24 01          	cmpl   $0x1,(%rsp)              ; 比较栈顶第一个数是否等于1
  400f0e:	74 20                	je     400f30 <phase_2+0x34>    ; 如果等于1，跳转到400f30
  400f10:	e8 25 05 00 00       	callq  40143a <explode_bomb>    ; 否则爆炸
  400f15:	eb 19                	jmp    400f30 <phase_2+0x34>    ; 跳转（冗余，因为爆炸后不会继续）
  ; 循环部分
  400f17:	8b 43 fc             	mov    -0x4(%rbx),%eax          ; 取前一个数（rbx-4）到eax
  400f1a:	01 c0                	add    %eax,%eax                ; eax = eax * 2
  400f1c:	39 03                	cmp    %eax,(%rbx)              ; 比较当前数(rbx指向)是否等于前一个数的两倍        
  400f1e:	74 05                	je     400f25 <phase_2+0x29>    ; 相等则跳转
  400f20:	e8 15 05 00 00       	callq  40143a <explode_bomb>    ; 否则爆炸
  400f25:	48 83 c3 04          	add    $0x4,%rbx                ; rbx + 4 指向下一个数
  400f29:	48 39 eb             	cmp    %rbp,%rbx                ; 比较rbx是否达到rbp（结束位置）
  400f2c:	75 e9                	jne    400f17 <phase_2+0x1b>    ; 未达到则继续循环
  400f2e:	eb 0c                	jmp    400f3c <phase_2+0x40>    ; 达到则跳出循环，函数结束
  ; 初始化循环指针
  400f30:	48 8d 5c 24 04       	lea    0x4(%rsp),%rbx           ; rbx指向第二个数（地址为rsp+4）
  400f35:	48 8d 6c 24 18       	lea    0x18(%rsp),%rbp          ; rbp指向结束位置（rsp+24，即第六个数之后）
  400f3a:	eb db                	jmp    400f17 <phase_2+0x1b>    ; 跳转到循环开始
  ; 清理栈并返回
  400f3c:	48 83 c4 28          	add    $0x28,%rsp
  400f40:	5b                   	pop    %rbx
  400f41:	5d                   	pop    %rbp
  400f42:	c3                   	retq
```

- 第7，8，9行，比较(%rsp)与1是否相等，不相等则引爆。可知第一个数为 1
- 查看初始化循环指针部分（20行）。
  - 第20行：第2个数存在`0x4(%rsp)`中，设为`num_2`，则初始化后`(%rbx)=num_2`；
  - 第21行：将`rbp`指向结束位置（`rsp+24`，即第六个数之后）
- 进入循环
  - 第11、12行：取前一个数（`rbx-4`）到`eax`，将`eax`翻倍
  - 第 13 行：**比较当前数(`rbx`指向)是否等于前一个数的两倍**
  - 第 14、15 行：相等则继续循环，不相等则爆炸
  - 剩余：继续取下一个值，并和结束位置`rbp`进行比较，未达到则继续循环



### phase_3

#### 源代码

```asm
0000000000400f43 <phase_3>:
  ; ===== 函数初始化 =====
  400f43:	48 83 ec 18          	sub    $0x18,%rsp           ; 分配 24字节栈空间
  400f47:	48 8d 4c 24 0c       	lea    0xc(%rsp),%rcx       ; rcx = rsp+0xc（第二个整数的存储地址）
  400f4c:	48 8d 54 24 08       	lea    0x8(%rsp),%rdx       ; rdx = rsp+0x8（第一个整数的存储地址）
  400f51:	be cf 25 40 00       	mov    $0x4025cf,%esi       ; esi = 格式字符串地址（通过 GDB 可查为 "%d %d"）
  400f56:	b8 00 00 00 00       	mov    $0x0,%eax            ; eax = 0（清空返回值）
  400f5b:	e8 90 fc ff ff       	callq  400bf0 <__isoc99_sscanf@plt> ; 调用 sscanf 读取输入
  ; ===== 输入验证 =====
  400f60:	83 f8 01             	cmp    $0x1,%eax                    ; 比较 sscanf 返回值（成功读取的参数数量）
  400f63:	7f 05                	jg     400f6a <phase_3+0x27>        ; 若返回值 > 1（即成功读取两个整数），跳转
  400f65:	e8 d0 04 00 00       	callq  40143a <explode_bomb>        ; 否则引爆炸弹（输入不足两个整数）
  ; ===== 分支索引检查 =====
  ; 索引 0 分支
  400f6a:	83 7c 24 08 07       	cmpl   $0x7,0x8(%rsp)         ; 比较第一个整数和 7
  400f6f:	77 3c                	ja     400fad <phase_3+0x6a>  ; 若第一个整数 > 7（无符号），跳转至炸弹
  400f71:	8b 44 24 08          	mov    0x8(%rsp),%eax         ; eax = 第一个整数（分支索引）
  400f75:	ff 24 c5 70 24 40 00 	jmpq   *0x402470(,%rax,8)     ; 跳转到 [0x402470 + rax*8] 地址处
  ; ===== 分支表（根据索引跳转至此）=====
  ; 索引 0 分支
  400f7c:	b8 cf 00 00 00       	mov    $0xcf,%eax             ; eax = 0xCF (207)
  400f81:	eb 3b                	jmp    400fbe <phase_3+0x7b>  ; 跳转到公共比较代码
  ; 索引 2 分支
  400f83:	b8 c3 02 00 00       	mov    $0x2c3,%eax            ; eax = 0x2C3 (707)
  400f88:	eb 34                	jmp    400fbe <phase_3+0x7b>  
  ; 索引 3 分支
  400f8a:	b8 00 01 00 00       	mov    $0x100,%eax            ; eax = 0x100 (256)
  400f8f:	eb 2d                	jmp    400fbe <phase_3+0x7b>  
  ; 索引 4 分支
  400f91:	b8 85 01 00 00       	mov    $0x185,%eax            ; eax = 0x185 (389)
  400f96:	eb 26                	jmp    400fbe <phase_3+0x7b>  
  ; 索引 5 分支
  400f98:	b8 ce 00 00 00       	mov    $0xce,%eax             ; eax = 0xCE (206)
  400f9d:	eb 1f                	jmp    400fbe <phase_3+0x7b> 
  ; 索引 6 分支
  400f9f:	b8 aa 02 00 00       	mov    $0x2aa,%eax            ; eax = 0x2AA (682)
  400fa4:	eb 18                	jmp    400fbe <phase_3+0x7b>  
  ; 索引 7 分支
  400fa6:	b8 47 01 00 00       	mov    $0x147,%eax            ; eax = 0x147 (327)
  400fab:	eb 11                	jmp    400fbe <phase_3+0x7b>  
  ; 非法索引处理
  400fad:	e8 88 04 00 00       	callq  40143a <explode_bomb>  ; 引爆炸弹（索引 > 7）
  400fb2:	b8 00 00 00 00       	mov    $0x0,%eax              ；eax = 0
  400fb7:	eb 05                	jmp    400fbe <phase_3+0x7b>  ；无条件跳转值 400fbe
  ; ===== 索引 1 分支（特殊位置）=====
  400fb9:	b8 37 01 00 00       	mov    $0x137,%eax            ; eax = 0x137 (311)
  ; ===== 公共比较代码 =====
  400fbe:	3b 44 24 0c          	cmp    0xc(%rsp),%eax         ; 比较 eax 和第二个输入整数
  400fc2:	74 05                	je     400fc9 <phase_3+0x86>  ; 若相等，跳转至安全退出
  400fc4:	e8 71 04 00 00       	callq  40143a <explode_bomb>  ; 否则引爆炸弹
  ; ===== 安全退出 =====
  400fc9:	48 83 c4 18          	add    $0x18,%rsp             ; 恢复栈指针
  400fcd:	c3                   	retq                          ; 函数返回
```

## 问答

Q1: 汇编中如何区分`$0x...` 是立即数还是地址

A: 所有 $0x... 都是立即数，这是 AT&T 汇编语法规则。语法上都是立即数，但通过寄存器用途、地址范围、调试器可推断其语义（数值/地址）。

Q2：`__isoc99_sscanf` 函数详解

1. 函数功能： `__isoc99_sscanf` 是 C 标准库函数 sscanf 的特定实现，用于从字符串中格式化读取输入，按照指定格式解析输入字符串，将结果存储到指定变量；返回值为成功解析的参数数量（整数）
  ```c
  int sscanf(const char *str, const char *format, ...);
  ```
2. 参数传递机制（System V AMD64 ABI）/ 寄存器使用规则：
   
| 参数顺序 | 寄存器  | 当前值                     |
|----------|---------|----------------------------|
| 参数1    | `rdi`   | 输入字符串地址（外部传入）|
| 参数2    | `rsi`   | `0x4025cf`（格式字符串）  |
| 参数3    | `rdx`   | `rsp+8`（第一个整数地址） |
| 参数4    | `rcx`   | `rsp+0xc`（第二个整数地址）|