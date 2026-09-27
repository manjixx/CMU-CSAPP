# run.ps1 — Windows 下启动 CSAPP 容器，之后用 SSH 连入
# 用法：在 PowerShell 中执行 .\run.ps1

$CONTAINER = "csapp"
$IMAGE     = "csapp-env"
$VOLUME    = "e:/CMU-CSAPP:/csapp"

$exists = docker ps -a --format "{{.Names}}" | Select-String "^${CONTAINER}$"

if ($exists) {
    Write-Host ">>> 启动已有容器 [$CONTAINER] ..." -ForegroundColor Green
    docker start $CONTAINER | Out-Null
} else {
    Write-Host ">>> 创建新容器 [$CONTAINER] ..." -ForegroundColor Cyan
    docker run -d `
        --name       $CONTAINER `
        --privileged `
        -v           $VOLUME `
        -p           "127.0.0.1:7777:22" `
        -p           "15213:15213" `
        $IMAGE
}

Write-Host ">>> SSH: ssh -p 7777 root@127.0.0.1    密码默认 csapp" -ForegroundColor Yellow
Write-Host ">>> 或在 VS Code Remote-SSH 中连接 Host csapp" -ForegroundColor Yellow
