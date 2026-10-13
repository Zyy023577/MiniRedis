# MiniRedis Studio

MiniRedis Studio 是一个使用 C++17 编写的轻量级键值存储服务，并配套提供 TCP 客户端、CLI 工具、HTTP 管理 API 和 React 管理界面。

项目重点放在可读的服务端代码、线程安全、TTL/LRU、AOF 持久化和本地可复现运行，不追求完整替代 Redis。

## Features

- String 类型键值操作：`SET`、`GET`、`DEL`、`EXISTS`、`KEYS`
- 过期控制：`EXPIRE`、`TTL`，后台线程自动清理过期 Key
- LRU 淘汰：限制最大 Key 数量，访问数据时更新访问顺序
- AOF 持久化：追加写入、重启恢复和 `AOF REWRITE`
- TCP Server：支持多个客户端和连续命令
- CLI Client：通过终端连接 TCP Server
- Admin API：提供 Key 管理、命令执行、统计和 AOF Rewrite
- Web Dashboard：搜索、编辑、删除 Key，查看 TTL 和运行指标
- C++ 并发测试客户端：测量实际吞吐量和平均命令延迟
- CMake 独立 Target、CTest 测试和 PowerShell 一键构建脚本

## Architecture

```text
                         ┌────────────────────────┐
                         │ React + TypeScript      │
                         │ Web Dashboard           │
                         └───────────┬────────────┘
                                     │ HTTP/JSON
                         ┌───────────▼────────────┐
                         │ C++ Admin API           │
                         │ REST + static hosting   │
                         └───────────┬────────────┘
                                     │
┌──────────────┐        ┌───────────▼────────────┐
│ CLI Client   │ TCP    │ MiniRedis Core          │
│ Benchmark    ├────────►│ unordered_map + mutex  │
└──────────────┘        │ TTL + LRU + metrics     │
                         └───────────┬────────────┘
                                     │
                         ┌───────────▼────────────┐
                         │ AOF Persistence         │
                         │ replay + atomic rewrite │
                         └────────────────────────┘
```

核心数据只由 C++ Core 管理，前端不会复制一套存储逻辑。

## Technology Stack

- C++17、STL、CMake
- Windows Winsock / Linux Socket API
- `std::shared_mutex`、`std::thread`、`std::condition_variable`
- 文件 I/O、AOF、临时文件和原子替换
- React、TypeScript、Vite
- 原生 HTTP/1.1 解析和 JSON 响应

## Requirements

### Windows

- Windows 11
- MinGW g++ 10.3.0 或兼容 C++17 编译器
- CMake 3.16+
- Node.js 和 npm

### WSL/Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake nodejs npm
```

Windows 环境优先使用 MinGW Makefiles；WSL 可用于 Linux 兼容构建和 Socket 验证。

## Quick Start

以下命令在项目根目录执行。

### Windows 一键构建和测试

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build_all.ps1
```

脚本会依次执行：

1. CMake 配置和 C++ 编译
2. CTest
3. React 生产构建

### 启动生产版服务

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run_server.ps1
```

默认监听：

- TCP Server：`127.0.0.1:6380`
- Admin API 和 Dashboard：`127.0.0.1:18080`

浏览器打开：

```text
http://127.0.0.1:18080/
```

### VS Code Tasks

按 `Ctrl+Shift+P`，选择 `Tasks: Run Task`，可以运行：

- `MiniRedis: build and test`
- `MiniRedis: run server`
- `MiniRedis Web: dev server`

任务配置位于 [.vscode/tasks.json](.vscode/tasks.json)。

## Development Mode

先启动 C++ 服务端，再启动 Vite：

```powershell
# 终端一：项目根目录
.\build\miniredis_server.exe 127.0.0.1 6380 data\miniredis.aof 18080 web\dist

# 终端二
Set-Location .\web
Copy-Item .env.example .env
npm.cmd install
npm.cmd run dev
```

开发模式打开：

```text
http://127.0.0.1:5173
```

如果电脑的 VPN 或代理影响本地访问，请确保 `127.0.0.1` 和 `localhost` 走直连。

## CLI Usage

```powershell
.\build\miniredis_cli.exe 127.0.0.1 6380
```

示例：

```text
SET name ayu
GET name
ayu
EXPIRE name 60
TTL name
(integer) 60
DEL name
(integer) 1
QUIT
OK
```

TCP 使用自定义的“每行一条命令、每行一个响应”文本协议，不是完整 Redis RESP 协议。

支持的核心命令：

| Command | Description |
| --- | --- |
| `SET key value` | 创建或更新字符串值 |
| `GET key` | 读取值 |
| `DEL key` | 删除 Key |
| `EXISTS key` | 判断 Key 是否存在 |
| `KEYS` | 获取当前全部 Key |
| `EXPIRE key seconds` | 设置过期时间 |
| `TTL key` | 查看剩余秒数 |
| `INFO` | 查看存储信息 |
| `AOF REWRITE` | 压缩当前 AOF |
| `QUIT` | 关闭 CLI 连接 |

## Configuration

服务端支持以下命令行参数：

```text
miniredis_server [ipv4-host] [tcp-port] [aof-path] [admin-port] [static-root]
```

默认值：

| Parameter | Default |
| --- | --- |
| IPv4 host | `127.0.0.1` |
| TCP port | `6380` |
| AOF path | `data/miniredis.aof` |
| Admin API port | `8080` |
| Static root | `web/dist` |

本机 8080 被占用时，可以使用：

```powershell
.\build\miniredis_server.exe `
  127.0.0.1 6380 data\miniredis.aof 18080 web\dist
```

## Admin API

Admin API 默认只监听本机，并返回 CORS 响应供 Vite 开发服务器调用。

| Method | Endpoint | Description |
| --- | --- | --- |
| `GET` | `/api/keys` | 获取全部 Key 和 TTL |
| `GET` | `/api/keys/:key` | 获取 Key、Value 和 TTL |
| `POST` | `/api/keys` | 创建 Key |
| `PUT` | `/api/keys/:key` | 更新 Value 或 TTL |
| `DELETE` | `/api/keys/:key` | 删除 Key |
| `POST` | `/api/command` | 执行一条 MiniRedis 命令 |
| `GET` | `/api/stats` | 获取运行统计 |
| `POST` | `/api/aof/rewrite` | 执行 AOF Rewrite |

创建 Key：

```powershell
curl.exe --noproxy "*" -X POST http://127.0.0.1:18080/api/keys `
  -H "Content-Type: application/json" `
  -d '{"key":"demo","value":"ayu","ttl":60}'
```

查询统计：

```powershell
curl.exe --noproxy "*" http://127.0.0.1:18080/api/stats
```

## Persistence and Data Safety

- `SET`、`DEL`、`EXPIRE` 会追加到 AOF。
- 服务启动时按顺序重放 AOF。
- AOF Rewrite 根据当前有效数据生成临时文件。
- 临时文件写入成功后，再使用原子替换更新原文件。
- AOF 恢复期间不会重复追加恢复命令。

## Testing

运行 C++ 测试：

```powershell
ctest --test-dir build --output-on-failure
```

运行前端生产构建：

```powershell
Set-Location .\web
npm.cmd run build
```

当前已验证的 C++ 测试包括：

- 核心命令、参数错误和长度限制
- TTL 识别、后台清理和 LRU 淘汰
- 多线程读写
- 多客户端 TCP 连接
- 非法命令和超长请求
- AOF 写入、关闭、重启恢复和 Rewrite
- Admin API CRUD、命令、统计和静态文件托管

最近一次实际结果：

```text
4/4 C++ tests passed
Total Test time (real) = 3.44 sec
Vite production build: passed, 28 modules transformed
```

## Benchmark

先启动 TCP Server，再执行：

```powershell
.\build\miniredis_benchmark.exe 127.0.0.1 6380 4 100
```

参数依次为：TCP 地址、端口、客户端数量、每个客户端的 `SET + GET` 轮数。

Windows 11 本机一次实际运行记录：

| Clients | Successful commands | Failed commands | Throughput | Average latency |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 100 | 0 | 25740.688 commands/s | 28.960 μs |
| 2 | 200 | 0 | 52699.534 commands/s | 27.235 μs |
| 4 | 400 | 0 | 93578.196 commands/s | 30.890 μs |
| 8 | 800 | 0 | 136899.567 commands/s | 40.786 μs |

测试环境：Windows 11、g++ 10.3.0、CMake 4.4.4、MinGW Make；每个客户端执行 50 轮 `SET + GET`。这些数据只代表一次本机短请求测试，不是通用性能承诺。

## Project Structure

```text
MiniRedis/
├── CMakeLists.txt
├── README.md
├── include/miniredis/       公共 C++ 头文件
├── src/core/                存储、命令处理、指标
├── src/network/             Socket、TCP Server、TCP Client
├── src/persistence/         AOF 持久化
├── src/api/                 Admin API 和静态文件托管
├── src/server_main.cpp      服务端入口
├── src/cli_main.cpp         CLI 入口
├── src/benchmark_main.cpp   并发测试客户端
├── tests/                   CTest 测试
├── scripts/                 PowerShell 构建和启动脚本
├── web/                     React + TypeScript + Vite
├── docs/                    验收文档
└── data/                    本地 AOF 数据目录
```

## Design Notes

- `std::unordered_map` 提供平均 O(1) Key 查找。
- `std::shared_mutex` 允许多个读操作并发执行，并保护写操作。
- `std::list` 加哈希表迭代器实现 LRU 顺序维护。
- 后台清理线程通过条件变量周期性检查过期 Key，并在析构时安全停止。
- Socket、文件、线程都通过 RAII 或明确的生命周期管理。
- Windows 使用 Winsock，Linux 使用 POSIX Socket API；公共接口由 `Socket` 封装。

## Known Limitations

- 只支持 string 类型，不支持列表、集合、哈希等 Redis 数据结构。
- TCP 是自定义文本协议，不兼容 Redis RESP。
- AOF 是按行保存的文本格式，Key 或 Value 中的换行符不适合 Rewrite。
- Admin API 使用项目内的轻量 HTTP 和有限 JSON 字段解析，不是通用 HTTP/JSON 框架。
- 默认只监听 `127.0.0.1`，没有认证、TLS 和远程访问控制。
- Benchmark 只覆盖本机简单 `SET + GET` 工作负载，不等同于完整压测工具。

## Roadmap

- 引入成熟 HTTP/JSON 库并完善请求解析
- 增加认证、限流和权限控制
- 支持 RESP 或更完整的客户端协议
- 增加更多数据结构、批量命令和配置文件
- 增加跨平台 CI、长时间稳定性测试和更系统的压测场景

## Screenshots

```text
docs/screenshots/dashboard.png   Dashboard 首页
docs/screenshots/cli.png         CLI 命令交互
docs/screenshots/tests.png       CTest 和 benchmark 输出
```

截图位置保留为占位，后续可替换为真实运行截图。

## License

License 尚未单独声明，当前仓库主要用于学习、实验和本地开发。
