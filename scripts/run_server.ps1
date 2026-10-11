param(
    [int]$TcpPort = 6380,
    [int]$AdminPort = 18080,
    [string]$AofPath = "data\miniredis.aof"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$serverPath = Join-Path $projectRoot "build\miniredis_server.exe"
$staticRoot = Join-Path $projectRoot "web\dist"

if (-not (Test-Path -LiteralPath $serverPath)) {
    throw "找不到 $serverPath，请先运行 scripts\build_all.ps1"
}
if (-not (Test-Path -LiteralPath (Join-Path $staticRoot "index.html"))) {
    throw "找不到前端构建产物，请先运行 scripts\build_all.ps1"
}

Push-Location $projectRoot
try {
    & $serverPath "127.0.0.1" $TcpPort $AofPath $AdminPort $staticRoot
}
finally {
    Pop-Location
}
