# 📝  Linking 

```css
源代码 → 预处理 → 编译 → 汇编 → .o 文件
      → 链接器解析符号 + 重定位 → 可执行 ELF
      → execve 加载 segments → 启动动态链接器
      → 动态装载共享库 → 动态符号绑定（GOT/PLT）
      → 程序运行 main()
      → （可选）dlopen 动态加载更多库
```

## 1. 课程主题

* **Linking（链接）**
* **案例研究：库函数插桩（Library Interpositioning）**

---

# 2. Linking 基础理解

## 2.1 Linking 是什么？

Linking 是将多个**目标文件（.o）**组合成单个程序的过程。可以发生在三个时间点：

* **编译时（Compile-time）**
* **加载时（Load-time）**
* **运行时（Run-time）**

掌握 Linking 有助于避免各种难定位的错误，并提升系统级理解能力。

---

# 3. 示例程序与静态链接

## 3.1 示例 C 程序

main.c 调用 sum.c：

```c
int sum(int *a, int n);

int array[2] = {1, 2};

int main() {
    int val = sum(array, 2);
    return val;
}
```

sum.c：

```c
int sum(...) { ... }
```

## 3.2 静态链接流程

编译器驱动程序执行：

```
gcc -Og -o prog main.c sum.c
```

静态链接器（ld）将 main.o 和 sum.o 合并为 **可执行文件 prog**。

---

# 4. ELF：Executable and Linkable Format

ELF 是 Linux 统一的二进制格式，适用于：

* **可重定位文件**（.o）
* **可执行文件**（a.out）
* **共享库文件**（.so）

## 4.1 ELF 文件主要结构

* **ELF header**：字长、机器类型、文件类型等
* **Segment Header Table**（可执行文件需要）
* **Section Header Table**（目标文件需要）
* **关键 Sections：**

  * `.text`：代码
  * `.rodata`：只读数据（如跳转表）
  * `.data`：已初始化全局变量
  * `.bss`：未初始化全局变量（节头存在但不占空间）
  * `.symtab`：符号表
  * `.rel.text`：文本重定位信息
  * `.rel.data`：数据段重定位信息
  * `.debug`：调试信息

---

# 5. 符号解析（Symbol Resolution）

## 5.1 符号类型

* **全局符号**

  * 当前模块定义，可被其他模块引用
* **外部符号**

  * 当前模块引用，但在其他模块定义
* **局部符号**

  * static 修饰的局部变量、局部静态变量

## 5.2 处理重复符号定义：强符号 vs 弱符号

* **强符号（Strong）**：函数、已初始化全局变量
* **弱符号（Weak）**：未初始化全局变量

规则：

1. **不允许多个强符号同名**
2. **强符号优先于弱符号**
3. 多个弱符号 → 任意一个被选中

---

# 6. 重定位（Relocation）

目的：将所有引用转换为最终的虚拟地址。

重定位发生在链接阶段，用于：

* 填充全局变量地址
* 填充函数调用跳转目标

示例（main.o 的重定位项）：

```
a: R_X86_64_32 array          ; &array 地址
f: R_X86_64_PC32 sum-0x4      ; 调用 sum()
```

最终运行时地址如：

```
%edi = 0x601018  ; array 的实际地址
callq sum
```

---

# 7. 加载可执行文件

加载器（Loader，execve）：

* 将 .text / .data 等 segments 映射进内存
* 映射共享库区域
* 创建堆、栈

最终进程内存包括：

* Code segment（.text、.rodata）
* Data segment（.data、.bss）
* Heap（malloc）
* Stack

---

# 8. Static Library（静态库）

## 8.1 静态库结构

一个 `.a` 文件是多个 `.o` 文件通过 `ar` 打包而成：

```
libvector.a = { addvec.o , multvec.o }
```

示例使用流程：

```
gcc main2.c libvector.a
```

常见库：

* libc.a（1496 个 .o）
* libm.a（444 个 .o）

## 8.2 链接器解析静态库的算法

1. 按命令行顺序扫描 .o 和 .a
2. 维护当前未解析符号列表
3. 遇到新目标文件时尝试解析未解决符号
4. 扫描结束仍未解决 → 链接错误

注意：**静态库需要放在命令行的后面**。

---

# 9. Shared Library（共享库，动态链接库）

共享库 = `.so` 文件，可在：

* **加载时**动态加载
* **运行时**动态加载（dlopen）

优势：

* 减小可执行文件大小
* 多进程共享代码段
* 库升级无需重新链接应用

示例创建共享库：

```
gcc -shared -o libvector.so addvec.c multvec.c
```

---

# 10. Runtime Dynamic Linking（运行时动态链接）

使用 `dlopen` / `dlsym`：

```c
handle = dlopen("./libvector.so", RTLD_LAZY);
addvec = dlsym(handle, "addvec");
addvec(x, y, z, 2);
dlclose(handle);
```

常用于：

* 插件系统
* 高性能服务器
* 软件动态扩展

---

# 11. Library Interpositioning（库函数插桩）

插桩：拦截对任意函数的调用，并替换为自定义版本。

三种方式：

## 11.1 编译时插桩

使用宏替换对 malloc/free 的调用。

## 11.2 链接时插桩

使用 linker wrapper 技术：

* malloc → `__wrap_malloc`
* 原 malloc → `__real_malloc`

## 11.3 运行时插桩（最强）

环境变量：

```
LD_PRELOAD=mymalloc.so ./a.out
```

示例输出：

```
malloc(32) = 0xe60010
free(0xe60010)
```

用途：

* 调试 / Profiler
* 安全监控
* 内存泄漏检测

---

# 12. 全局变量与注意事项

全局变量若定义重复可能造成覆盖：

* 弱符号可能与强符号合并
* 不同编译器可能结构体布局不同 → 更危险

建议：

* 尽量避免全局变量
* 必要时使用 `static`
* 使用 `extern` 引用外部变量

---

# 13. 总结

* Linking 是构建程序的核心步骤，涉及符号解析+重定位。
* ELF 提供统一的目标、可执行、共享库格式。
* 静态库整个复制进可执行文件。
* 共享库通过动态链接减少空间浪费。
* Library interpositioning 提供强大调试能力。
* 理解 Linking 能有效避免运行时错误。


