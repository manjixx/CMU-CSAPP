# 实验环境配置

[CSAPP](https://blog.csdn.net/weixin_44071773/article/details/120785642)

## 方案一：MACOS + 虚拟机 + Ubuntu20.04失败

### 一、系统信息

- OS：MAC OS 13.0.1
- 虚拟机：VMWare Fusion 专业版 e.x.p (19431034)
- 虚拟机操作系统：centos 8.4

### 二、安装虚拟机与Linux操作环境

### 三、配置gcc环境

```bash
sudo apt-get install build-essential
```

```bash
ubuntu@ubuntu:~$ sudo apt-get install gcc-multilib
Reading package lists... Done
Building dependency tree       
Reading state information... Done
Package gcc-multilib is not available, but is referred to by another package.
This may mean that the package is missing, has been obsoleted, or
is only available from another source

E: Package 'gcc-multilib' has no installation candidate
```

```bash
gcc-multilib-arm-linux-gnueabihf
```

```bash
sudo apt-get install gdb
```

### 四、验证

```bash
root@user:/labs/code# cd datalab/
root@user:/labs/code/datalab# make btest
gcc -O1 -Wall -m32 -lm -o btest bits.c btest.c decl.c tests.c
gcc: error: unrecognized command line option ‘-m32’
make: *** [Makefile:11: btest] Error 1
```

## 方案二：Docker + VsCode

### Step1 安装软件

- 安装Docker：[下载地址](https://docs.docker.com/desktop)
- 安装VsCode：[下载地址](https://code.visualstudio.com/download)

### Step2 Docker 配置实验环境

- 从仓库拉去系统镜像：ubuntu 20.04

```bash
docker pull ubuntu:20.04 # 自动拉取arm架构的ubuntu，不可以使用
docker pull ubuntu:latest@sha256:965fbcae990b0467ed5657caceaec165018ef44a4d2d46c7cdea80a9dff0d1ea   # 其中@sha256之后的可以在dockerhub 相应版本中寻找，这里为amd64
```

- 创建容器
  - 从该ubuntu镜像创建容器，命名为csapp_env，且挂载csapp共享文件夹(对应容器根目录下的csapp文件夹)，之前下载的lab资源通过该文件夹和容器共享，
  - 命令中的/Users/xxxx/Desktop/csapp为共享文件夹的本地目录绝对路径。执行完创建容器的命令后会自动进入容器并打开容器的bash，后面的命令都是在容器中的bash执行的。
  - -p 7777:22：设置端口映射，将容器tcp22端口转发到7777（后续将通过该端口对容器进行访问），与已占用端口不冲突的情况下可以自由设置，自由设置时查看宿主机容器映射指令`docker port CONTAINER [PRIVATE_PORT[/PROTO]]`
  - -v /Users/xxxx/Desktop/csapp:/csapp：将服务器目录与容器目录进行共享，方便本地与服务器（创建的Linux系统）之间的文件共享
  - ubuntu:lasted...：拉取的镜像名称。

```bash
docker container run --platform linux/amd64 -it -p 7777:22 -v /Users/iiixv/Documents/CSAPP:csapp --name=csapp_env 6b7dfa7e8fdb /bin/bash     #6b7dfa7e8fdb 镜像id

```

- 更新apt软件源

```bash
apt-get update
```

- 安装sudo

```bash
apt-get install sudo
```

- 安装c/c++编译环境

```bash
sudo apt-get install build-essential    # 安装编译环境
apt install gcc-multilib  # 补充gcc的完整环境(gcc-multilib)
sudo apt-get install gdb    # 安装gdb
```

- 安装vim

```bash
sudo apt-get install vim
```

### Step3 安装并配置SSH服务

- 安装openssh

```bash
sudo apt-get install -y openssh-server
```

- ssh配置

```bash
vim /etc/ssh/sshd_config
```

- 如果使用密钥登录，则进行如下修改，去掉以下选项的#注释

```bash
Port 22                     #开启22端口
PermitRootLogin yes         #允许root用户使用ssh登录
PubkeyAuthentication yes    #启用公钥私钥配对认证方式
AuthorizedKeysFile          .ssh/authorized_keys .ssh/authorized_keys2      #公钥文件路径
```

- 同时再mac端侧 进入`/Users/iiixv/.ssh/id_rsa.pub`，复制文件内容

- 在docker容器内，将上述内容粘贴至 `.ssh/authorized_keys`中

```bash
# 没有上述文件
touch .ssh/authorized_keys
vi .ssh/authorized_keys
```

- 重启ssh服务

```bash
service ssh restart
```

### Step4 SSH服务器自启动配置

尽管容器内安装了ssh服务，但每次关闭容器重启后，ssh将恢复停止状态，每次进入容器时需要重新启动ssh服务：service ssh restart，否则远程连接将失败，因此我们在这里利用脚本来实现ssh服务的自动启动：

```bash
vim /root/startup_run.sh
# 写入
#!/bin/bash

LOGTIME=$(date "+%Y-%m-%d %H:%M:%S")
echo "[$LOGTIME] startup run..." >>/root/startup_run.log
service ssh start >>/root/startup_run.log
#service mysql start >>/root/startup_run.log
```

- 添加文件权限

```bash
chmod +x /root/startup_run.sh
```

- 打开启动文件：

```bash
vim /root/.bashrc
```

- 把脚本命令添加到文件末尾：

```bash
# startup run
if [ -f /root/startup_run.sh ]; then
      /root/startup_run.sh
fi
```

- 最后，生效.bashrc：

```bash
source ~/.bashrc
```

### Step5 VSCode连接Linux

- 安装插件remote ssh

![](https://img-blog.csdnimg.cn/e5aea43885b24384b2da186f9c2e168e.png?x-oss-process=image/watermark,type_ZHJvaWRzYW5zZmFsbGJhY2s,shadow_50,text_Q1NETiBARm9vbGlzaDQz,size_20,color_FFFFFF,t_70,g_se,x_16)

- 打开查找栏（ctr+shift+p），输入remote-ssh，选择open Configuration file

![](https://img-blog.csdnimg.cn/c35a5f68e91843eabd5b786de0b717aa.png?x-oss-process=image/watermark,type_ZHJvaWRzYW5zZmFsbGJhY2s,shadow_50,text_Q1NETiBARm9vbGlzaDQz,size_20,color_FFFFFF,t_70,g_se,x_16)

- 填写配置文件

![](https://img-blog.csdnimg.cn/9bb4f340b8a947848b372f9b464e5d97.png?x-oss-process=image/watermark,type_ZHJvaWRzYW5zZmFsbGJhY2s,shadow_50,text_Q1NETiBARm9vbGlzaDQz,size_20,color_FFFFFF,t_70,g_se,x_16)

- 打开远程资源管理器，选择刚才配置好的host进行SSH连接

![](https://img-blog.csdnimg.cn/8bc85f8d164d42c1ab2ac80decd2c6c0.png?x-oss-process=image/watermark,type_ZHJvaWRzYW5zZmFsbGJhY2s,shadow_50,text_Q1NETiBARm9vbGlzaDQz,size_19,color_FFFFFF,t_70,g_se,x_16)

- 输入root账户密码

![](https://img-blog.csdnimg.cn/42760dab7c044ec78d87eb677d2c430d.png?x-oss-process=image/watermark,type_ZHJvaWRzYW5zZmFsbGJhY2s,shadow_50,text_Q1NETiBARm9vbGlzaDQz,size_20,color_FFFFFF,t_70,g_se,x_16)

- 找到csapp文件夹，进行实验

![](https://img-blog.csdnimg.cn/545538db241d4d8d968c4e7954ed12c1.png?x-oss-process=image/watermark,type_ZHJvaWRzYW5zZmFsbGJhY2s,shadow_50,text_Q1NETiBARm9vbGlzaDQz,size_20,color_FFFFFF,t_70,g_se,x_16)

至此实验所需环境搭建完毕。

### Step6 启动容器

```bash
# 启动容器
docker start csapp_env
# 进入容器系统bash
docker exec -it csapp_env bash
```

"LogPath": "/var/lib/docker/containers/bdf8caad4cf791d611047664d1c3679fb90c755b6459d55f7e38597bbb810497/bdf8caad4cf791d611047664d1c3679fb90c755b6459d55f7e38597bbb810497-json.log",
