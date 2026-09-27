FROM ubuntu:22.04

# ── 基础环境 ──────────────────────────────────────────────────────────
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    g++ \
    gcc-multilib \
    g++-multilib \
    make \
    cmake \
    gdb \
    gdb-multiarch \
    valgrind \
    strace \
    ltrace \
    binutils \
    elfutils \
    nasm \
    && true

RUN apt-get install -y \
    python3 \
    python3-pip \
    perl \
    flex \
    bison \
    libssl-dev \
    openssl \
    curl \
    wget \
    net-tools \
    netcat-openbsd \
    vim \
    nano \
    less \
    xxd \
    man-db \
    manpages-dev \
    openssh-server \
    && true

# ── 32位兼容库（Buffer Lab / 部分 Arch Lab）────────────────────────────
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y \
    libc6:i386 \
    libncurses5:i386 \
    libstdc++6:i386 \
    && true

# ── 清理缓存 ───────────────────────────────────────────────────────────
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# ── 工作目录 ───────────────────────────────────────────────────────────
WORKDIR /csapp

# ── GDB 配置 ───────────────────────────────────────────────────────────
RUN echo 'set pagination off'           >> /root/.gdbinit && \
    echo 'set confirm off'              >> /root/.gdbinit && \
    echo 'set disassembly-flavor intel' >> /root/.gdbinit

# ── Locale ─────────────────────────────────────────────────────────────
RUN apt-get update && apt-get install -y locales && \
    locale-gen en_US.UTF-8 && \
    apt-get clean && rm -rf /var/lib/apt/lists/*
ENV LANG=en_US.UTF-8

# ── SSH：容器每次启动时拉起 sshd，供 VS Code Remote-SSH 连接 ───────────
RUN mkdir -p /var/run/sshd /root/.ssh && \
    chmod 700 /root/.ssh && \
    ssh-keygen -A && \
    printf '\nPermitRootLogin yes\nPasswordAuthentication yes\nPubkeyAuthentication yes\n' >> /etc/ssh/sshd_config && \
    printf '%s\n' \
      '#!/bin/bash' \
      'set -e' \
      'mkdir -p /var/run/sshd' \
      'echo "root:${ROOT_PASSWORD:-csapp}" | chpasswd' \
      '/usr/sbin/sshd' \
      'exec "$@"' \
      > /usr/local/bin/docker-entrypoint.sh && \
    chmod +x /usr/local/bin/docker-entrypoint.sh

EXPOSE 22
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["sleep", "infinity"]
