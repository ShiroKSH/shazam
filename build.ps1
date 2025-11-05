$ErrorActionPreference = "Stop"

$Qt    = "C:\Qt\6.9.3\mingw_64"
$Tools = "C:\Qt\Tools"  

$mingwBin = Get-ChildItem -Path $Tools -Directory -Filter "mingw*_64" | Select-Object -First 1 | ForEach-Object { Join-Path $_.FullName "bin" }
if (-not (Test-Path $mingwBin)) { throw "MinGW не найден в $Tools" }
$env:Path = "$mingwBin;$env:Path"

Remove-Item -Recurse -Force ".\build",".\dist" -ErrorAction SilentlyContinue

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="$Qt" -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD/dist"
cmake --build build -j

& "$Qt\bin\windeployqt.exe" "$PWD\dist\Shazam.exe" --release
