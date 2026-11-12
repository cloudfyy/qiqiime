# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "..\..\scripts\common.ps1")

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("qiqi-symbols-" + [guid]::NewGuid().ToString("N"))
try {
    $dlls = @{}
    foreach ($arch in @("x64", "x86")) {
        $dir = Join-Path $fixture "source\$arch"
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        $dlls[$arch] = Join-Path $dir "qiqiime.dll"
        foreach ($binary in (@("qiqiime.dll") + $QiQiToolExeNames)) {
            $pdb = Join-Path $dir ([System.IO.Path]::ChangeExtension($binary, ".pdb"))
            [System.IO.File]::WriteAllText($pdb, "$arch-$binary")
        }
    }
    $destination = Join-Path $fixture "symbols"
    Copy-QiQiSymbols -Dlls $dlls -Destination $destination
    $files = @(Get-ChildItem -LiteralPath $destination -Recurse -File)
    if ($files.Count -ne 8) { throw "Expected eight product PDB files." }
    foreach ($arch in @("x64", "x86")) {
        foreach ($binary in (@("qiqiime.dll") + $QiQiToolExeNames)) {
            $name = [System.IO.Path]::ChangeExtension($binary, ".pdb")
            $actual = [System.IO.File]::ReadAllText((Join-Path $destination "$arch\$name"))
            if ($actual -ne "$arch-$binary") { throw "Incorrect symbol copy: $arch $name" }
        }
    }
    $missing = Join-Path $fixture "source\x86\QiQiIMESettings.pdb"
    Remove-Item -LiteralPath $missing
    $rejected = $false
    try {
        Copy-QiQiSymbols -Dlls $dlls -Destination $destination
    } catch {
        if ($_.Exception.Message -ne "Missing x86 symbol file: $missing") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Missing PDB must fail even when an older destination copy exists." }
    $dlls.X64 = $null
    $rejected = $false
    try {
        Copy-QiQiSymbols -Dlls $dlls -Destination $destination
    } catch {
        if ($_.Exception.Message -ne "Missing x64 DLL path for symbol packaging.") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Missing architecture must fail." }
    Write-Host "Dual-architecture symbol copy and missing-artifact checks passed."
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
