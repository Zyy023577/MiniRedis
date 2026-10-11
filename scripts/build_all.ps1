$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    cmake -S . -B build -G "MinGW Makefiles"
    cmake --build build
    ctest --test-dir build --output-on-failure

    Push-Location (Join-Path $projectRoot "web")
    try {
        npm.cmd run build
    }
    finally {
        Pop-Location
    }
}
finally {
    Pop-Location
}
