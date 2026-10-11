# MiniRedis Studio 验收清单

以下命令在 Windows PowerShell 中执行，默认工作目录为项目根目录。

## 自动构建与测试

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_all.ps1
```

脚本依次执行 CMake 配置、C++ 构建、CTest 和前端生产构建。

## 生产方式启动

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_server.ps1
```

浏览器打开 `http://127.0.0.1:18080/`。TCP CLI 使用另一个终端连接 `127.0.0.1:6380`。

## 手动验收

```powershell
curl.exe --noproxy "*" http://127.0.0.1:18080/api/stats
curl.exe --noproxy "*" -X POST http://127.0.0.1:18080/api/keys `
  -H "Content-Type: application/json" `
  -d '{"key":"demo","value":"ayu","ttl":60}'
curl.exe --noproxy "*" http://127.0.0.1:18080/api/keys/demo
```

CLI 验证：

```powershell
.\build\miniredis_cli.exe 127.0.0.1 6380
```

然后输入：

```text
SET name ayu
GET name
TTL name
QUIT
```

## 并发测试

```powershell
.\build\miniredis_benchmark.exe 127.0.0.1 6380 4 100
```

参数依次为 TCP 地址、端口、客户端数量和每个客户端的 `SET + GET` 轮数。输出只代表本次实际运行环境。

## VS Code 操作

按 `Ctrl+Shift+P`，选择 `Tasks: Run Task`，然后执行：

1. `MiniRedis: build and test`
2. `MiniRedis: run server`
3. `MiniRedis Web: dev server`（开发模式才需要）
