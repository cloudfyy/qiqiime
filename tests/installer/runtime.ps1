$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "..\..\scripts\common.ps1")

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-MissingRuntime([hashtable]$Dlls, [bool]$Os64, [string]$ExpectedMessage) {
    $rejected = $false
    try {
        Test-QiQiRuntimeFiles -Dlls $Dlls -Is64BitOperatingSystem $Os64
    } catch {
        if ($_.Exception.Message -notlike $ExpectedMessage) { throw }
        $rejected = $true
    }
    Assert-True $rejected "Incomplete runtime must fail: $ExpectedMessage"
}

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("qiqi-runtime-" + [guid]::NewGuid().ToString("N"))
try {
    $x86Dir = Join-Path $fixture "x86"
    $dataDir = Join-Path $fixture "data"
    New-Item -ItemType Directory -Path $x86Dir, $dataDir -Force | Out-Null
    foreach ($binary in (@("qiqiime.dll") + $QiQiToolExeNames)) {
        [System.IO.File]::WriteAllText((Join-Path $x86Dir $binary), "")
    }
    foreach ($dataFile in $QiQiDataFileNames) {
        [System.IO.File]::WriteAllText((Join-Path $dataDir $dataFile), "")
    }

    $dlls = Resolve-QiQiDllPaths -BaseDir $fixture -Is64BitOperatingSystem $false
    Assert-True ($null -eq $dlls.X64 -and $dlls.X86 -eq (Join-Path $x86Dir "qiqiime.dll")) "32-bit Windows must resolve the standalone x86 layout."
    Test-QiQiRuntimeFiles -Dlls $dlls -Is64BitOperatingSystem $false
    Assert-MissingRuntime $dlls $true "Missing x64 DLL:*"

    $tool = Join-Path $x86Dir $QiQiToolExeNames[0]
    Remove-Item -LiteralPath $tool
    Assert-MissingRuntime $dlls $false "Missing required tool executable*"
    [System.IO.File]::WriteAllText($tool, "")
    $dataFile = Join-Path $dataDir $QiQiDataFileNames[0]
    Remove-Item -LiteralPath $dataFile
    Assert-MissingRuntime $dlls $false "Missing required data file*"
    [System.IO.File]::WriteAllText($dataFile, "")

    $x64Dir = Join-Path $fixture "x64"
    New-Item -ItemType Directory -Path $x64Dir -Force | Out-Null
    foreach ($binary in (@("qiqiime.dll") + $QiQiToolExeNames)) {
        [System.IO.File]::WriteAllText((Join-Path $x64Dir $binary), "")
    }
    $dlls = Resolve-QiQiDllPaths -BaseDir $fixture -Is64BitOperatingSystem $true
    Test-QiQiRuntimeFiles -Dlls $dlls -Is64BitOperatingSystem $true
    $native32 = Resolve-QiQiDllPaths -BaseDir $fixture -Is64BitOperatingSystem $false
    Assert-True ($null -eq $native32.X64) "32-bit Windows must ignore x64 files even in a dual-architecture bundle."
    Remove-Item -LiteralPath (Join-Path $x86Dir "qiqiime.dll")
    $dlls = Resolve-QiQiDllPaths -BaseDir $fixture -Is64BitOperatingSystem $false
    Assert-MissingRuntime $dlls $false "Missing x86 DLL:*"
    $dlls = Resolve-QiQiDllPaths -BaseDir $fixture -Is64BitOperatingSystem $true
    Assert-MissingRuntime $dlls $true "Missing x86 DLL:*"

    $cases = @(
        @{ Os64 = $false; Process64 = $false; Dll32 = $true; Directory = "System32" },
        @{ Os64 = $true; Process64 = $true; Dll32 = $true; Directory = "SysWOW64" },
        @{ Os64 = $true; Process64 = $false; Dll32 = $true; Directory = "SysWOW64" },
        @{ Os64 = $true; Process64 = $true; Dll32 = $false; Directory = "System32" },
        @{ Os64 = $true; Process64 = $false; Dll32 = $false; Directory = "Sysnative" }
    )
    foreach ($case in $cases) {
        $actual = Get-QiQiRegsvr32Path -Is32Bit:$case.Dll32 -Is64BitOperatingSystem $case.Os64 -Is64BitProcess $case.Process64
        $expected = Join-Path $env:SystemRoot "$($case.Directory)\regsvr32.exe"
        Assert-True ($actual -eq $expected) "Incorrect regsvr32 selection: $actual; expected $expected."
    }
    $rejected = $false
    try {
        Get-QiQiRegsvr32Path -Is64BitOperatingSystem $false -Is64BitProcess $false
    } catch {
        if ($_.Exception.Message -ne "Cannot register an x64 DLL on 32-bit Windows.") { throw }
        $rejected = $true
    }
    Assert-True $rejected "x64 registration must be rejected on 32-bit Windows."
    Write-Host "Native x86 / WOW64 runtime validation and regsvr32 selection passed."
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
