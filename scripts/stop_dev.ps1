<#
=====================================================================
  停止开发环境 —— 按端口找到服务进程并结束
=====================================================================
配合 start_dev.ps1 使用, 直接双击 stop_dev.bat 即可。

为什么"按端口找"而不是记录 PID:
    服务是跑在独立窗口里的, 窗口一关(或者电脑重启) PID 就失效了。
    端口才是"事实": 谁占着 1883 / 8000 / 5173, 谁就是目标。

安全考虑:
    只结束 python / node / cmd 这类开发用的进程。万一某个端口被
    别的软件(数据库、IIS、其他 Node 项目)占了, 脚本会提示并跳过,
    绝不误杀 —— 你现在同时开着别的项目也不会被牵连。
=====================================================================
#>
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$targets = [ordered]@{
    1883 = 'MQTT Broker'
    8000 = '后端 Backend '
    5173 = '前端 Frontend'
}

$killable = @('python', 'python3', 'node', 'cmd')
$stopped  = 0

Write-Host ''
Write-Host '  正在停止开发环境服务 ...' -ForegroundColor Cyan

foreach ($port in $targets.Keys) {
    $name  = $targets[$port]
    $conns = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue

    if (-not $conns) {
        Write-Host ("  [--] {0}  端口 {1,-5} 本来就没在跑" -f $name, $port) -ForegroundColor DarkGray
        continue
    }

    foreach ($c in $conns) {
        $proc = Get-Process -Id $c.OwningProcess -ErrorAction SilentlyContinue
        if (-not $proc) { continue }

        if ($killable -notcontains $proc.ProcessName) {
            Write-Host ("  [!!] 端口 {0} 被 {1} (PID {2}) 占用, 不是本项目的服务, 已跳过" -f $port, $proc.ProcessName, $proc.Id) -ForegroundColor Yellow
            continue
        }

        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        $stopped++
        Write-Host ("  [OK] {0}  已停止 (端口 {1}, {2}, PID {3})" -f $name, $port, $proc.ProcessName, $proc.Id) -ForegroundColor Green
    }
}

Write-Host ''
if ($stopped -gt 0) {
    Write-Host "  共停止 $stopped 个进程。" -ForegroundColor Green
}
else {
    Write-Host '  没有需要停止的进程。' -ForegroundColor DarkGray
}
Write-Host '  (那几个服务窗口可能还停在命令提示符上, 直接关掉就好)' -ForegroundColor DarkGray
Write-Host ''
