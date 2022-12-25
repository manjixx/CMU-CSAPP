# Course Overview

## Course Theme: Abstraction is Good But don't forget realtiy

- 大多数CS/CE课程强调抽象
  - 抽象数据类型
  - 渐进分析(Asymptotic Analysis)

- These abstractions hava limits
  - Especially in the presence of bugs
  - Need to understand details of underlying implementations

- Useful outcomes from taking 213
  - Become more effective programmers
    - Able to find and eliminate bugs effciently
    - Able to understand and tune form program performance
  - Prepare for later "Systems" calsses in CS & ECE
  
## Five Realities

> **Great Reality 1:Ints are not Integers, Floats are not Reals**

- Examples 1: ${x^2 >= 0}$
  - Float's:yes
  - Int's: no!
    - 50000 * 50000
    - 300 *400* 500 * 600

- Example 2:Is ${(x + y) + z = x + (y + z)}$?
  - Unsigned & Signed int : yes
  - Float's: no
    - ${(1e20 + -1e20) + 3.14 == 3.14}$
    - ${1e20 + (-1e20 + 3.14) == 0}$

- Computer Arithmetic
  
  - 不产生随机值：算术运算具有重要的数学性质
  - 不能假设所有“通常”的数学性质
    - 整数运算满足“环”属性
      - 交换性、结合性、分配性
    - 浮点运算满足“排序”属性
      - 单调性，符号值
  
> **Great Reality 2: You've Got to Konw Assembly(汇编)**

- 很有可能永远不会用汇编写程序
- 了解汇编是机器级执行模型的关键（Understanding assembly is key to
  machine-­‐level execution model）
  - 存在错误时程序的行为
  - 调整程序性能
    - 了解编译器完成/未完成的操作
    - 了解程序效率低下的根源
  - 实现系统软件
    - 编译器以机器码作为目标
    - 操作系统必须管理进程状态
  - 创建/打击恶意软件
  - X86是汇编语言首选

> **Great Reality 3: Memory Matters-- Random Access Memory Is an unphysical Abstraction** (随机存取内存是一个非物理性的抽象概念)

- 内存并非无限的
  - 内存必须被分配和管理
  - 许多应用程序是内存主导的
- 内存引用错误极其有害
- 内存性能不均匀
  - 高速缓存和虚拟内存可以极大影响程序性能
  - 使程序适应内存系统的特性可以显着提高速度

- Memory Referencing Bug Example

```c
typedef struct{
  int a[2];
  double d;
} struct_t;

double fun(int i){
  volatile struct_t s;
  s.d = 3.14;
  s.a[i] = 107341824; /*Possibly out of bounds */
  return s.d;
}
/**
fun(0) -> 3.14
fun(1) -> 3.14
fun(2) -> 3.1399998664856
fun(3) ➙ 2.00000061035156
fun(4) ➙ 3.14
fun(6) ➙ Segmentation fault
 */
```

- Memory Referencing Errors
  - C和C++不提供任何内存保护
    - 越界的数组引用
    - 无效的指针值
    - 滥用malloc/free
  - 可能导致讨厌的bug
    - bug是否有任何影响取决于系统和编译器
    - 长远的行为
      - 损坏的对象在逻辑上与被访问的对象无关
      - bug的影响可能在它产生后很久才被发现
  - 我如何处理这个问题
    - 用Java、Ruby、Python、ML......编程
    - 了解可能发生的干扰
    - 使用或开发工具来检测引用错误（例如Valgrind）。

> **Great Reality 4: There’s more to performance than asymptotic complexity**（对性能的影响不仅仅是复杂度）

- Constant factors matter too
- 甚至精确的操作数也不能预测性能
  - Easily see 10:1 performance range depending on how code written
  - 必须在多个级别进行优化：算法、数据表示、过程和循环
- 必须了解系统以优化性能
  - 程序如何编译和执行
  - 如何衡量程序性能和识别漏洞
  - 如何在不破坏代码模块化和通用性的情况下提高性能

```c
/**2.0 GHz Intel Core i7 Haswell */

/**4.3ms */
void copyij(int src[2048][2048], int dst[2048][2048])
{
  int i,j;
  for (i = 0; i < 2048; i++)
    for (j = 0; j < 2048; j++)
      dst[i][j] = src[i][j];
} 

/**81.8ms */
void copyji(int src[2048][2048], int dst[2048][2048])
{
  int i,j;
  for (j = 0; j < 2048; j++)
    for (i = 0; i < 2048; i++)
      dst[i][j] = src[i][j];
} 
```

> **Great Reality 5 Computers do more than execute programs**

- They need get data in and out
- They Communicate with each other over networks
  - 存在网络时会出现许多系统级问题
  - 自主进程的并发操作
  - 应对不可靠的媒体
  - 跨平台兼容性
  - 复杂的性能问题

## How the coures fits into the CS/ECE curriculum

213: foundation of computer system,Underlying principles for hardware, software, and networking

> Cousre Perspecitve

大多数系统课程以构建者为中心

- 计算机体系结构:在 Verilog 中设计流水线处理器
- 操作系统:实现操作系统的样本部分
- 编译器:为简单语言编写编译器
- 网络:实现和模拟网络协议

## Academic integerity

read each chapter three times

课后习题检测对教材的理解程度

Tmin 取绝对值

Tmax 与Umax的关系
