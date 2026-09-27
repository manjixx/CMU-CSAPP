# GCC指令合集

大多数 64 位机器也可以运行为 32 位机器编译的程序，这是一种向后兼容。因此，举例来说，当程序 `prog.c` 用如下伪指令编译后。将程序称为 "32 位程序”或 "64 位程序”时，区别**在于该程序是如何编译**的，而不是其运行的机器类型。

```bash
linux> gcc -m32 prog.c # 该程序就可以在 32 位或 64 位机器上正确运行。
linux> gcc -m64 prog.c # 只能在 64 位机器上运行
```

```bash
linux> gcc -Og -o p p1.c p2.c

# - gcc:gcc编译器
# - Og:告诉编译器使用会生成符合原始 C 代码整体结构的机器代码的优化等级
# - o p：指定最终的可执行代码文件 p
```

在命令行上使用 "-s" 选项，就能看到 C 语言编译器产生的汇编代码：

```bash
linux> gcc -Og -S mstore.c # 产生一个汇编文件mstore.s
```

**产生Intel格式的汇编代码**:

```sh
linux> gcc -Og -S -masm=intel mstore.c
```

使用 "-c" 命令行选项， GCC 会编译并汇编该代码，这就会**产生目标代码文件** `mstore.o`：

```bash
linux> gee -Og -c mstore.c
```


**反汇编器：**

```bash
linux> objdump -d mstore.o
```

