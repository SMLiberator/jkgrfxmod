# Size diagnostics for the Windows Release renderer.dll.
# Run after MSBuild; never modifies the DLL or ships in the standalone package.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$reportDir = Join-Path $root 'build-audit'
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$dll = Get-ChildItem -Path $root -Recurse -File -Filter renderer.dll |
    Where-Object { $_.FullName -match '[\\/](Release|release)[\\/]' } |
    Select-Object -First 1
if (-not $dll) { throw 'Release renderer.dll was not found' }
$map = Get-ChildItem -Path $root -Recurse -File -Filter renderer.map |
    Where-Object { $_.FullName -match '[\\/](Release|release)[\\/]' } |
    Select-Object -First 1

$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll.FullName).Hash
$info = @(
    "binary=renderer.dll"
    "bytes=$($dll.Length)"
    "kib=$([math]::Round($dll.Length / 1024.0, 2))"
    "sha256=$sha"
    "map_present=$([bool]$map)"
)
$info | Set-Content -Encoding utf8 (Join-Path $reportDir 'renderer_size.txt')
foreach ($mode in @('headers', 'imports', 'exports')) {
    $result = & dumpbin.exe "/$mode" $dll.FullName 2>&1
    if ($LASTEXITCODE -ne 0) { throw "dumpbin.exe /$mode failed" }
    $result | Out-File -FilePath (Join-Path $reportDir "renderer_$mode.txt") -Encoding utf8
}
if ($map) {
    Copy-Item -LiteralPath $map.FullName -Destination (Join-Path $reportDir 'renderer.map') -Force
}
else {
    Write-Warning 'renderer.map missing: check Release linker GenerateMapFile property.'
}
Write-Host "renderer.dll: $($dll.Length) bytes ($([math]::Round($dll.Length/1024.0,2)) KiB); map_present=$([bool]$map)"
if ($env:GITHUB_STEP_SUMMARY) {
    @(
        '### renderer.dll size audit'
        ''
        "| Item | Value |"
        "|---|---|"
        "| Release DLL | $($dll.Length) bytes ($([math]::Round($dll.Length/1024.0,2)) KiB) |"
        "| SHA256 | `$sha` |"
        "| Linker map produced | $([bool]$map) |"
        ''
        'PE section headers, import/export lists, and the linker map are saved in the build-audit artifact.'
    ) | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}
