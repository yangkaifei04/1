# 操作系统lab0.5实验报告
### 实验目的

实验0.5主要讲解最小可执行内核和启动流程。我们的内核主要在 Qemu 模拟器上运行，它可以模拟一台 64 位 RISC-V 计算机。为了让我们的内核能够正确对接到 Qemu 模拟器上，需要了解 Qemu 模拟器的启动流程，还需要一些程序内存布局和编译流程（特别是链接）相关知识。

本章你将学到：

1.使用 链接脚本 描述内存布局

2.进行 交叉编译 生成可执行文件，进而生成内核镜像

3.使用 OpenSBI 作为 bootloader 加载内核镜像，并使用 Qemu 进行模拟

4.使用 OpenSBI 提供的服务，在屏幕上格式化打印字符串用于以后调试

### 练习1: 使用GDB验证启动流程
为了熟悉使用qemu和gdb进行调试工作,使用gdb调试QEMU模拟的RISC-V计算机加电开始运行到执行应用程序的第一条指令（即跳转到0x80200000）这个阶段的执行过程，说明RISC-V硬件加电后的几条指令在哪里？完成了哪些功能？要求在报告中简要写出练习过程和回答。
#### 第一阶段 复位地址的指令

##### 开始调试
首先打开gdb进行调试。首先使用`make debug`指令挂起，`make gdb`指令开始调试。
<img src="photos/Pasted image 20240925002025.png">  
此时pc的值为0x1000，使用`x/10i &pc`指令查看即将执行的十条指令

<img src="photos/Pasted image 20240925002235.png">  

##### 指令解析
1. `0x1000: auipc t0,0x0`：将当前指令地址（PC）加上立即数0x0（即不变）存入寄存器`t0`。`auipc`（Add Upper Immediate to PC）指令用于设置全局地址。`AUIPC`（Add Upper Immediate to PC）是 RISC-V 指令集中的一个指令，用于将程序计数器（PC）的当前值与一个立即数相加，并将结果存储在指定的寄存器中。这个指令通常用于生成跳转目标地址。在 RISC-V 中，`AUIPC` 指令的格式如下：-`rd` 是目标寄存器，结果将被存储在这里。`imm` 是一个 20 位的立即数，它会被左移 12 位（因为 PC 是 4 字节对齐的，所以需要将立即数左移 12 位来生成正确的地址）后与 PC 的值相加。指令执行时，PC 的当前值（不包括最低的 12 位，因为这些位表示当前指令的字内偏移量）与立即数的左移版本相加，结果存储在 `rd` 寄存器中。
2. `0x1004: addi a2,t0,40`：将寄存器`t0`的值加上立即数40，结果存入寄存器`a2`。`addi`（Add Immediate）指令用于将寄存器与立即数相加。
3. `0x1008: csrr a0,mhartid`：从`mhartid`（Machine Hart ID）寄存器读取值存入`a0`。`csr`指令用于读取控制状态寄存器。Mhartid）是 RISC-V 架构中的一个控制和状态寄存器（Control and Status Register，CSR），用于存储当前硬件线程（Hart）的标识符。在多核处理器中，每个核心可能有一个或多个硬件线程，每个硬件线程都有一个唯一的 Hart ID。这个寄存器可以通过 `csrr` 指令来读取。

4. `0x100c: ld a1,32(t0)`：从`t0+32`的内存地址加载一个字（4字节）到寄存器`a1`。`ld`（Load）指令用于从内存加载数据。
    
5. `0x1010: ld t0,24(t0)`：从`t0+24`（0x1018）的内存地址加载一个字到寄存器`t0`。我们在这里用`x/10xw $pc`指令查看0X1018的值，为0x80000000。

<img src="photos/Pasted image 20240925014231.png">  
7. `0x1014: jr t0`：跳转到寄存器`t0`指向的地址执行。`jr`（Jump Register）指令用于跳转。此时查看t0的值，即为跳转地址。
<img src="photos/Pasted image 20240925011745.png">  

<img src="photos/Pasted image 20240925010502.png">  


#### 0x80000000处的指令

<img src="photos/Pasted image 20240925012028.png">  
首先在跳转之后，我们查看即将执行的十条指令，在0x802000000处设置断点并执行到断点.

查资料得到：
典型的RISC-V启动流程如下，以Loader为spl、Bootloader为U-Boot、OS为Linux为例。

1. 系统POR之后，从ROM开始启动。将SPL加载到SRAM中。跳转到SPL运行。
2. SPL进行DDR初始化，并将OpenSBI和U-Boot加载到DDR中。跳转到OpenSBI中运行。
3. OpenSBI从DDR中开始执行，进行相关设置。跳转到U-Boot中执行。
4. U-Boot加载Linux到DDR中，进行解析、解压等操作。最后跳转到Linux中运行。
5. 最后处于运行态的仅有OpenSBI和Linux，Linux通过sbi指令和OpenSBI进行交互。

这个阶段

<img src="photos/Pasted image 20240925012516.png">  
通过设置断点，我们发现启动函数似乎是kern/init/entry.s,打开这个文件进行查看。

```
#include <mmu.h>
#include <memlayout.h>
    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    la sp, bootstacktop
    tail kern_init
.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
```

1. `la sp, bootstacktop`  
    使用 `la`（Load Address）指令将 `bootstacktop` 的地址加载到堆栈指针 `sp` 中。`bootstacktop` 是堆栈顶部的地址。
    
2. `tail kern_init`  
    使用 `tail` 指令调用 `kern_init` 函数。在 RISC-V 汇编中，`tail` 调用是一种优化的调用方式，它跳转到另一个函数并在那里返回，而不是通过返回地址返回。这在函数的最后调用另一个函数时很有用，因为它可以减少堆栈的使用。

我们再来查看kern_init函数。

```
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
   while (1)
        ;
}
```

这个函数的作用是在操作系统启动时初始化未初始化的全局变量，打印一条消息到控制台，然后执行一个无限循环。在实际的操作系统开发中，内核初始化函数会执行更多的任务，比如设置内存管理单元、初始化中断和异常处理程序、启动用户空间进程等。







### 重要知识点

##### 寄存器

| 寄存器     | ABI 名称 | 描述         |
| ------- | ------ | ---------- |
| x0      | zero   | 零值         |
| x1      | ra     | 返回地址       |
| x2      | sp     | 堆栈指针       |
| x3      | gp     | 全局指针       |
| x4      | tp     | 线程指针       |
| x5      | t0     | 临时/备用链接寄存器 |
| x6-x7   | t1-t2  | 临时寄存器      |
| x8      | s0/fp  | 保存寄存器/帧指针  |
| x9      | s1/gp  | 保存寄存器/全局指针 |
| x10-x11 | a0-a1  | 函数参数/返回值   |
| x12-x17 | a2-a7  | 函数参数       |
| x18-x27 | s2-s11 | 保存寄存器      |
| x28-x31 | t3-t6  | 临时寄存器      |

RISC-V 架构提供32个通用寄存器x0-x31，其中x0 有些特殊，x0 寄存器被设置为硬件连线的常数0，读恒为0，写无效，这个寄存器在一些地方很有作用，因为程序运行中常数0的使用频率非常高，所以专门用一个寄存器来存放常数0，并没有浪费寄存器数量，并且使得编译器工作更加简便，这一点也是RISC-V架构优雅性的体现，比如后面讲到的伪指令。


#####  makefile

makefile带来的好处就是——“自动化编译”，一旦写好，只需要一个make命令，整个工程完全自动编译，极大的提高了软件开发的效率。
本实验的makefile还依赖tools/function.mk。只要makefile写得够好，所有的这一切，只用一个make命令就可以完成，make命令会自动智能地根据当前的文件修改的情况来确定哪些文件需要重编译，从而自己编译所需要的文件和链接目标程序。

