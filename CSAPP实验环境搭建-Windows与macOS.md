# CSAPP 实验环境搭建（Docker + SSH）

Windows 与 macOS 使用同一套镜像和同一套容器内路径。差别只在宿主机怎么安装 Docker、怎么挂载目录、怎么启动。连进容器之后，Lab 命令完全相同。

| 项目 | 值 |
|------|----|
| 镜像 | `csapp-env`（仓库根目录 `Dockerfile`，Ubuntu 22.04 amd64） |
| 容器名 | `csapp` |
| 代码挂载 | 整个仓库 → 容器内 `/csapp` |
| SSH | 宿主机 `127.0.0.1:7777` → 容器 `22` |
| Proxy Lab | 宿主机 `15213` → 容器 `15213` |
| 登录 | 用户 `root`，默认密码 `csapp`（可用环境变量 `ROOT_PASSWORD` 覆盖） |

编辑代码用本机 VS Code，编译和调试通过 Remote-SSH 在容器里做。保存后的文件经卷挂载即时出现在两边。

---

## 一、为什么不用 macOS 虚拟机直接编译

在 Apple Silicon 的 Ubuntu/CentOS 虚拟机里安装 `gcc-multilib` 会失败，`make btest` 会报 `unrecognized command line option '-m32'`。CSAPP 的 Bomb、Data、Buffer 等实验依赖 x86-64 / i386 用户态程序。

正确做法：用 Docker 跑 **linux/amd64** 容器。Intel Mac 与 Apple Silicon 都加 `--platform linux/amd64`（Apple Silicon 由 Docker Desktop 做仿真）。

---

## 二、安装 Docker 与 VS Code

### Windows

1. 安装 [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/)，版本 ≥ 4.0，后端选 **WSL2**。
2. 安装 [VS Code](https://code.visualstudio.com/)，扩展安装 **Remote - SSH**。
3. 确认本机自带 OpenSSH 客户端（Windows 10 已内置）：

```powershell
docker --version
ssh -V
```

仓库路径固定为 `e:\CMU-CSAPP`，容器内对应 `/csapp`。

### macOS

1. 安装 [Docker Desktop for Mac](https://www.docker.com/products/docker-desktop/)。Apple Silicon 无需改成 ARM 镜像。
2. 安装 VS Code，扩展安装 **Remote - SSH**。
3. 把本仓库放到本机任意目录，下文记为 `$REPO`（例如 `/Users/你的用户名/CMU-CSAPP`）。

```bash
docker --version
ssh -V
```

---

## 三、构建镜像

镜像里已经包含 gcc、gdb、make、python3、valgrind、32 位兼容库，以及 **OpenSSH**。容器每次启动都会拉起 `sshd`，不需要再手动 `service ssh start`。

在仓库根目录执行。

### Windows

```powershell
cd e:\CMU-CSAPP
docker build -t csapp-env .
docker images | Select-String csapp-env
```

### macOS

```bash
cd "$REPO"
docker build --platform linux/amd64 -t csapp-env .
docker images | grep csapp-env
```

第一次大约 5–10 分钟。

---

## 四、创建并启动容器

容器在后台运行。退出 SSH 不会关掉容器。

### Windows

```powershell
cd e:\CMU-CSAPP
Set-ExecutionPolicy -Scope CurrentUser Bypass   # 只需一次
.\run.ps1
```

等价命令：

```powershell
docker run -d `
  --name csapp `
  --privileged `
  -v "e:/CMU-CSAPP:/csapp" `
  -p 127.0.0.1:7777:22 `
  -p 15213:15213 `
  csapp-env
```

已有同名容器时，脚本只会 `docker start csapp`，不会重建。

### macOS

```bash
cd "$REPO"
chmod +x run.sh
./run.sh
```

等价命令：

```bash
docker run -d \
  --platform linux/amd64 \
  --name csapp \
  --privileged \
  -v "$REPO:/csapp" \
  -p 127.0.0.1:7777:22 \
  -p 15213:15213 \
  csapp-env
```

`--privileged` 是为了 Attack Lab 里关闭 ASLR。SSH 只绑在 `127.0.0.1`，不暴露到局域网。

自定义 root 密码时，在 `docker run` 上增加 `-e ROOT_PASSWORD=你的密码`。不设置则密码是 `csapp`。改密码后如果容器已经创建过，需要删掉容器再按上面的命令重建（镜像不用重编）：

```bash
docker rm -f csapp
```

---

## 五、配置 SSH 并连上 VS Code

两边的 SSH 配置内容相同，只是文件位置不同。

| 系统 | 私钥目录 | SSH 配置文件 |
|------|----------|----------------|
| Windows | `C:\Users\<你>\.ssh\` | `C:\Users\<你>\.ssh\config` |
| macOS | `~/.ssh/` | `~/.ssh/config` |

### 5.1 生成密钥（还没有密钥时）

Windows PowerShell：

```powershell
ssh-keygen -t ed25519 -f $env:USERPROFILE\.ssh\id_ed25519
type $env:USERPROFILE\.ssh\id_ed25519.pub
```

macOS：

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519
cat ~/.ssh/id_ed25519.pub
```

一路回车即可。

### 5.2 写入容器

先保证容器已启动，再把公钥放进容器。密码登录不依赖这一步；配好后就可以免密。

Windows：

```powershell
docker start csapp
docker cp $env:USERPROFILE\.ssh\id_ed25519.pub csapp:/root/.ssh/authorized_keys
docker exec csapp chmod 600 /root/.ssh/authorized_keys
```

macOS：

```bash
docker start csapp
docker cp ~/.ssh/id_ed25519.pub csapp:/root/.ssh/authorized_keys
docker exec csapp chmod 600 /root/.ssh/authorized_keys
```

`docker rm` 之后容器是新的，需要再执行一次 `docker cp`。

### 5.3 SSH 配置

`config` 里追加：

```
Host csapp
    HostName 127.0.0.1
    Port 7777
    User root
    IdentityFile ~/.ssh/id_ed25519
```

Windows 上 `IdentityFile` 也可以写成 `C:\Users\<你>\.ssh\id_ed25519`。

命令行验证：

```bash
ssh csapp
```

第一次会询问主机指纹，输入 `yes`。未配密钥时密码是 `csapp`。登录后应直接位于 `/csapp`。

### 5.4 VS Code Remote-SSH

1. `Ctrl+Shift+P`（macOS 为 `Cmd+Shift+P`），运行 **Remote-SSH: Connect to Host...**。
2. 选择 `csapp`。
3. 打开文件夹 `/csapp`。

之后在 VS Code 终端里执行的就是容器里的 bash，可以多开终端：一个编译，一个跑 GDB。

日常开关机只需：

```bash
docker start csapp
```

然后在 VS Code 里重连 `csapp`。停止容器：

```bash
docker stop csapp
```

---

## 六、容器内目录与一次性初始化

```
/csapp/                       ← Windows: e:\CMU-CSAPP ；macOS: $REPO
├── labs/code/                ← 各 Lab 源码或可执行文件
├── labs/lab_notes/
├── lec-notes/
├── lec-materials/
├── Dockerfile
├── run.ps1                   ← Windows 启动
└── run.sh                    ← macOS 启动
```

Attack Lab 需要关闭 ASLR。首次登录后写入 `~/.bashrc`，以后每次 SSH 登录都会执行：

```bash
cat >> ~/.bashrc << 'EOF'
echo 0 > /proc/sys/kernel/randomize_va_space 2>/dev/null || true
alias ll='ls -alF --color=auto'
alias gdb='gdb -q'
alias objdump='objdump -M intel'
export EDITOR=vim
export LANG=en_US.UTF-8
EOF
source ~/.bashrc
cat /proc/sys/kernel/randomize_va_space   # 期望输出 0
```

---

## 七、各 Lab 运行指南

以下命令都在 SSH 会话（容器内）执行。

### 7.1 Bomb Lab

```bash
cd /csapp/labs/code/bomb
./bomb
gdb bomb
```

```text
(gdb) b phase_1
(gdb) b explode_bomb
(gdb) r
(gdb) disas
(gdb) x/s 0x地址
(gdb) x/d $rdi
(gdb) ni
(gdb) si
```

把已做出的答案放进 `answers.txt`，可避免重复输入：

```bash
./bomb answers.txt
```

### 7.2 Attack Lab

```bash
cd /csapp/labs/code/attacklab
echo 0 > /proc/sys/kernel/randomize_va_space
objdump -d ctarget > ctarget.asm
objdump -d rtarget > rtarget.asm
gdb ctarget
```

```text
(gdb) b getbuf
(gdb) r -q
(gdb) info frame
(gdb) p/x $rsp
```

```bash
./hex2raw < exploit.txt | ./ctarget -q
./hex2raw < exploit.txt | ./rtarget -q
```

### 7.3 Cache Lab

```bash
cd /csapp/labs/code/cachelab
make
./test-csim
./csim -v -s 4 -E 1 -b 4 -t traces/yi.trace
./csim-ref -v -s 4 -E 1 -b 4 -t traces/yi.trace

make && ./test-trans -M 32 -N 32
./test-trans -M 64 -N 64
./test-trans -M 61 -N 67
python3 driver.py
```

### 7.4 Data Lab

```bash
cd /csapp/labs/code/datalab
./dlc bits.c
make btest
./btest
./btest -f bitAnd
perl driver.pl
```

### 7.5 Shell Lab

```bash
cd /csapp/labs/code/shlab
make
./tshref
./tsh
make test01
make rtest01
```

`trace01`–`trace05` 是前台作业，`trace06`–`trace10` 是后台与进程组，`trace11`–`trace16` 是信号处理。

### 7.6 Malloc Lab

```bash
cd /csapp/labs/code/malloclab
make
./mdriver -v
./mdriver -f traces/short1-bal.rep -v
./mdriver -V -D
```

### 7.7 Proxy Lab

在一个 SSH 终端里：

```bash
cd /csapp/labs/code/proxylab
make
./proxy 15213
```

另开一个 SSH 终端：

```bash
curl -x http://localhost:15213 http://www.example.com/
./driver.sh
```

### 7.8 Arch Lab

```bash
cd /csapp/labs/code/archlab
tar xf sim.tar
cd sim && make clean && make
cd misc
# 编写 sum.ys 后：
../misc/yas sum.ys
../misc/yis sum.yo
```

### 7.9 Buffer Lab（32 位）

```bash
cd /csapp/labs/code/bufferlab
file bufbomb          # 应显示 ELF 32-bit
gdb bufbomb
```

```text
(gdb) set architecture i386
(gdb) b getbuf
(gdb) r -u userid
```

### 7.10 Performance Lab

```bash
cd /csapp/labs/code/perflab
make driver
./driver -g
```

---

## 八、常用工具

| GDB | 作用 |
|-----|------|
| `b function` / `b *0x地址` | 断点 |
| `r` / `c` / `ni` / `si` / `finish` | 运行、继续、单步 |
| `disas` | 反汇编 |
| `x/s 0x地址` / `x/d $rdi` | 看字符串 / 寄存器 |
| `info registers` / `info frame` | 寄存器 / 栈帧 |
| `layout asm` / `layout regs` | TUI |
| `q` | 退出 |

```bash
objdump -d -M intel binary > binary.asm
nm binary | grep " T "
strings binary
readelf -h binary
ldd binary
strace ./binary
```

---

## 九、故障排除

| 现象 | 处理 |
|------|------|
| `ssh: connect to host 127.0.0.1 port 7777: Connection refused` | 先 `docker start csapp`，再 `docker exec csapp pgrep sshd`。没有进程就 `docker rm -f csapp` 后按第四节重建（旧容器没有 SSH 入口） |
| 密码不对 | 默认 `csapp`。要改密码就删容器，用 `-e ROOT_PASSWORD=...` 重建 |
| Apple Silicon 上 `cannot execute binary file` 或 `-m32` 报错 | 构建和运行都加 `--platform linux/amd64` |
| Attack Lab 地址每次变化 | 容器内 `echo 0 > /proc/sys/kernel/randomize_va_space`，确认输出 `0` |
| Windows 卷挂载是空目录 | 使用正斜杠：`-v "e:/CMU-CSAPP:/csapp"` |
| `Permission denied`（公钥） | 容器内 `/root/.ssh` 为 `700`，`authorized_keys` 为 `600` |
| Proxy Lab 连不上 | 确认创建容器时有 `-p 15213:15213` |
| GDB 没有符号 | 编译加 `-g` |
| `make: flex: not found` | 镜像过旧。在仓库根目录重新 `docker build -t csapp-env .` 并重建容器 |

查看状态：

```bash
docker ps -a
docker port csapp
```

---

## 附录：Lab 与教材章节

| Lab | 章节 | 核心点 |
|-----|------|--------|
| Data Lab | Ch 2 | 位运算、补码、浮点 |
| Bomb / Attack / Buffer Lab | Ch 3 | 汇编、溢出、ROP |
| Arch Lab | Ch 4 | Y86-64、流水线 |
| Perf Lab | Ch 5 | 循环与缓存友好 |
| Cache Lab | Ch 6 | Cache 结构、分块 |
| Shell Lab | Ch 8 | 进程、信号、作业控制 |
| Malloc Lab | Ch 9 | 堆分配器 |
| Proxy Lab | Ch 11–12 | 套接字、并发服务器 |
