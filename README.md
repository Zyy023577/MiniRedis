# MiniRedis Studio

基于 C++ 的轻量级键值存储服务器与可视化管理平台。项目按阶段实现，当前工作区已完成核心存储、TCP Server/CLI、TTL/LRU、AOF、Admin API、Dashboard 和工程化收尾。

## 当前状态

- 已完成：`SET`、`GET`、`DEL`、`EXISTS`、`KEYS`、`EXPIRE`、`TTL`、`INFO`、`QUIT` 的核心逻辑。
- 已完成：`std::unordered_map`、`std::shared_mutex`、键和值长度限制、过期键识别、核心单元测试和基础并发测试。
- 已完成：基于 Windows Winsock 的 TCP Server、CLI Client、逐行文本协议、多个客户端线程、异常输入和超长请求处理。
- 已完成：后台 TTL 清理线程、最大键数量、LRU 淘汰、GET/SET 访问顺序更新和安全停止清理线程。
- 已完成：AOF 写入、重启重放、Rewrite 临时文件和 Windows 原子替换。
- 已完成：本地 Admin HTTP API、统计接口和 React + TypeScript + Vite Dashboard。
- 已完成：C++ Admin API 托管 `web/dist` 静态文件，并加入静态路径穿越防护。
- 已完成：独立 C++ 并发测试客户端 `miniredis_benchmark`。

## 技术栈

C++17、STL、CMake、TCP Socket、线程与 RAII、文件 I/O、自定义 HTTP/1.1 API、React + TypeScript + Vite。

## 架构图

```text
React Dashboard
      │ HTTP JSON
      ▼
C++ Admin API ─────────────┐
      │                    │
      ▼                    ▼
MiniRedis Core        web/dist 静态文件
      │
      ├── unordered_map + shared_mutex
      ├── TTL 清理线程 + LRU
      └── AOF 持久化

TCP Clients ──► C++ TCP Server ──► MiniRedis Core
CLI Client  ──►
```

## 目录结构

```text
include/miniredis/   公共 C++ 头文件
src/core/            KeyValueStore、命令处理、指标
src/network/         Socket、TCP Server、TCP Client
src/persistence/     AOF 持久化
src/api/             Admin HTTP API 和静态文件托管
tests/               核心、TCP、AOF、Admin API 测试
benchmark/           benchmark 说明目录
web/src/             React 页面和 API 请求层
web/dist/            前端生产构建产物
data/                本地 AOF 数据
docs/                项目补充文档
```

## 第一阶段构建与测试

推荐使用 CMake：

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Windows 构建：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

WSL/Ubuntu：

```bash
sudo apt update
sudo apt install -y build-essential cmake
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Windows 优先使用 MinGW Makefiles；WSL 仅作为 Linux 兼容构建环境，端口和启动参数与 Windows 版本基本相同。

## VS Code 一键操作

项目提供了 Windows PowerShell 脚本和 `.vscode/tasks.json`：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_all.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_server.ps1
```

也可以在 VS Code 中按 `Ctrl+Shift+P`，选择 `Tasks: Run Task`，执行 `MiniRedis: build and test`、`MiniRedis: run server` 或 `MiniRedis Web: dev server`。

完整手动验收步骤见 [docs/verification.md](docs/verification.md)。

命令解析器和 TCP Server 使用自定义换行文本命令，例如 `SET name ayu`。这不是完整的 Redis RESP 协议。

## 第二阶段运行

启动服务端（默认只监听本机）：

```bat
build\miniredis_server.exe
```

启动 CLI：

```bat
build\miniredis_cli.exe
```

也可以指定 IPv4 地址和端口：

```bat
build\miniredis_server.exe 127.0.0.1 6380
build\miniredis_cli.exe 127.0.0.1 6380
```

TCP 协议是自定义的“每行一条命令、每行一个响应”的文本协议，不是完整 Redis RESP 协议。按 `Ctrl+C` 停止服务端。

服务端默认使用 `data/miniredis.aof`，也可以指定 AOF 路径：

```bat
build\miniredis_server.exe 127.0.0.1 6380 data\demo.aof
```

如果 8080 已被其他程序占用，可以指定第四个参数修改 Admin API 端口：

```bat
build\miniredis_server.exe 127.0.0.1 6380 data\demo.aof 18080
```

第五个参数可以指定前端静态文件目录。默认是 `web/dist`：

```bat
build\miniredis_server.exe 127.0.0.1 6380 data\demo.aof 18080 web\dist
```

AOF 命令示例：

```text
SET name ayu
SET name updated
EXPIRE name 60
AOF REWRITE
```

服务端重新启动后会从 AOF 文件重放数据。Rewrite 使用临时文件，并在成功写入后进行原子替换。

## Admin API 与 Dashboard

服务端默认同时启动：

- TCP：`127.0.0.1:6380`
- Admin API：`127.0.0.1:8080`

如果 8080 已被其他程序占用，可以把第四个参数改为 18080：

```powershell
.\build\miniredis_server.exe 127.0.0.1 6380 data\demo.aof 18080
```

启动前端开发服务器：

```powershell
Set-Location .\web
$env:VITE_PROXY_TARGET = "http://127.0.0.1:18080"
npm.cmd run dev
```

也可以复制 `web/.env.example` 为 `web/.env`，再执行 `npm.cmd run dev`。

浏览器打开 `http://127.0.0.1:5173`。如果 Admin API 使用默认 8080，可以省略 `VITE_PROXY_TARGET`。

前端生产构建：

```powershell
npm.cmd install
npm.cmd run build
```

当前生产构建实际通过，生成 `web/dist`。开发阶段可以使用 Vite；部署验证时由 C++ Admin API 直接托管静态文件。

生产构建后，直接启动 C++ 服务端：

```powershell
Set-Location .\web
npm.cmd run build
Set-Location ..
.\build\miniredis_server.exe 127.0.0.1 6380 data\miniredis.aof 18080 web\dist
```

浏览器打开 `http://127.0.0.1:18080/`。其中 `/api/*` 仍然由 Admin API 处理，其他 GET 路径从 `web/dist` 读取。

### Admin API

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/keys` | 获取全部 Key 和 TTL |
| GET | `/api/keys/:key` | 获取 Key、Value 和 TTL |
| POST | `/api/keys` | JSON `{ "key": "name", "value": "ayu", "ttl": 60 }` |
| PUT | `/api/keys/:key` | JSON `{ "value": "updated", "ttl": 60 }` |
| DELETE | `/api/keys/:key` | 删除 Key |
| POST | `/api/command` | JSON `{ "command": "GET name" }` |
| GET | `/api/stats` | Key 数量、客户端、命令数、命中率、内存估算 |
| POST | `/api/aof/rewrite` | 执行 AOF Rewrite |

API 默认只监听本机，并返回 CORS 响应供本地 Vite 开发服务器调用。前端没有复制存储逻辑，所有数据来自 C++ API。

第二阶段实际测试环境：Windows PowerShell、g++ 10.3.0、CMake 4.4.4、MinGW Make。实际测试结果：

```text
1/2 Test #1: miniredis_core_tests ............. Passed
2/2 Test #2: miniredis_tcp_tests .............. Passed
100% tests passed out of 2
Total Test time (real) = 1.66 sec
```

第三阶段实际测试覆盖后台 TTL 清理、LRU 淘汰、并发读写和 TCP 回归。当前实际结果：

```text
1/2 Test #1: miniredis_core_tests ............. Passed
2/2 Test #2: miniredis_tcp_tests .............. Passed
100% tests passed out of 2
Total Test time (real) = 2.89 sec
```

第四阶段实际测试结果：

```text
1/3 Test #1: miniredis_core_tests ............. Passed
2/3 Test #2: miniredis_tcp_tests .............. Passed
3/3 Test #3: miniredis_aof_tests .............. Passed
100% tests passed out of 3
Total Test time (real) = 2.97 sec
```

第五阶段实际验证：

```text
1/4 Test #1: miniredis_core_tests ............. Passed
2/4 Test #2: miniredis_tcp_tests .............. Passed
3/4 Test #3: miniredis_aof_tests .............. Passed
4/4 Test #4: miniredis_admin_api_tests ........ Passed
100% tests passed out of 4
Total Test time (real) = 3.44 sec
Vite production build: passed, 28 modules transformed
```

第六阶段工程化验证：

先启动 TCP 服务端，再执行：

```powershell
.\build\miniredis_benchmark.exe 127.0.0.1 6380 4 100
```

参数依次是：服务端地址、TCP 端口、客户端数量、每个客户端的 `SET + GET` 轮数。

```text
miniredis_benchmark，Windows 11 本机，g++ 10.3.0，CMake 4.4.4，MinGW Make
测试命令：每个客户端执行 50 轮 SET + GET

clients=1: successful_commands=100, failed_commands=0,
throughput=25740.688 commands/s, average_latency=28.960 us
clients=2: successful_commands=200, failed_commands=0,
throughput=52699.534 commands/s, average_latency=27.235 us
clients=4: successful_commands=400, failed_commands=0,
throughput=93578.196 commands/s, average_latency=30.890 us
clients=8: successful_commands=800, failed_commands=0,
throughput=136899.567 commands/s, average_latency=40.786 us
```

以上是一次 Windows 11 本机、短请求、空载本地环境下的实测记录，不代表通用性能，也没有作为服务端性能承诺。

### 第一阶段实际验证结果

早期第一阶段验证结果：

```text
PASS: MiniRedis core tests (4 suites)
```

这四组测试覆盖基础命令、长度限制与 TTL、命令解析错误处理，以及 4 个线程并发写读。尚未将该结果扩展为 TCP 或 Web 功能的性能结论。

## 已知限制

- TCP 协议是自定义换行文本协议，不兼容 Redis RESP。
- AOF 当前按文本行保存，Key 或 Value 中的换行符不适合 Rewrite。
- Admin API 使用项目内的轻量 HTTP 解析和有限 JSON 字段解析，不是完整通用 HTTP/JSON 框架。
- 当前只支持 string 类型，未实现列表、集合、哈希等 Redis 数据结构。
- benchmark 只覆盖本机 TCP 的简单 SET + GET 工作负载，不等同于完整压测工具。
- 当前默认只监听 `127.0.0.1`，没有认证和远程访问控制。

## 项目难点

- 使用互斥量、条件变量和 RAII 管理多线程访问、后台 TTL 清理线程以及 Socket 生命周期。
- LRU 淘汰需要同时维护哈希表和访问顺序，且 GET、SET、过期删除都可能改变状态。
- AOF Rewrite 需要避免恢复时重复写入，并使用临时文件和原子替换降低文件损坏风险。
- Windows Winsock 与 Linux Socket API 存在差异，项目通过统一 `Socket` 封装兼容两种环境。
- Dashboard 只负责展示和发送请求，实际数据始终由 C++ Core 管理，避免前后端出现两套存储逻辑。

## 项目截图

后续可在此处补充以下真实运行截图：

```text
docs/screenshots/dashboard.png       Dashboard 首页
docs/screenshots/cli.png             CLI 命令交互
docs/screenshots/tests.png            CTest 和 benchmark 输出
```

## 后续优化方向

1. 引入成熟 HTTP/JSON 库并完善请求解析。
2. 增加认证、限流和更细粒度的权限控制。
3. 支持 RESP 或更完整的客户端协议。
4. 增加更丰富的数据结构、批量命令和配置文件。
5. 增加跨平台 CI、长时间稳定性测试和更系统的压测场景。

测试结果只填写实际运行结果，不虚构性能数据。
