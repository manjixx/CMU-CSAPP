#!/bin/bash
# macOS / Linux 下启动 CSAPP 容器，之后用 SSH 连入
# 用法：chmod +x run.sh && ./run.sh

set -euo pipefail

CONTAINER="csapp"
IMAGE="csapp-env"
REPO="$(cd "$(dirname "$0")" && pwd)"

if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
    echo ">>> 启动已有容器 [$CONTAINER] ..."
    docker start "$CONTAINER" >/dev/null
else
    echo ">>> 创建新容器 [$CONTAINER] ..."
    docker run -d \
        --platform linux/amd64 \
        --name "$CONTAINER" \
        --privileged \
        -v "${REPO}:/csapp" \
        -p "127.0.0.1:7777:22" \
        -p "15213:15213" \
        "$IMAGE"
fi

echo ">>> SSH: ssh -p 7777 root@127.0.0.1    密码默认 csapp"
echo ">>> 或在 VS Code Remote-SSH 中连接 Host csapp"
