param(
    [Parameter(Mandatory = $true)][string]$TestExecutable,
    [Parameter(Mandatory = $true)][string]$BaselineDll,
    [Parameter(Mandatory = $true)][string]$CurrentDll,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = 'Stop'
$TestExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
$BaselineDll = (Resolve-Path -LiteralPath $BaselineDll).Path
$CurrentDll = (Resolve-Path -LiteralPath $CurrentDll).Path
$baselineVersion = (Get-Item -LiteralPath $BaselineDll).VersionInfo.ProductVersion
$currentVersion = (Get-Item -LiteralPath $CurrentDll).VersionInfo.ProductVersion
if (-not $baselineVersion.EndsWith('.metrics-baseline')) { throw 'Not a benchmark baseline DLL' }
if ($baselineVersion -ne "$currentVersion.metrics-baseline") { throw 'DLL source versions differ' }
if (Test-Path -LiteralPath $Output) { throw 'Benchmark output already exists' }

function Read-Architecture([string]$File) {
    $stream = [IO.File]::OpenRead($File)
    $reader = $null
    try {
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Missing PE DOS header' }
        $stream.Position = 0x3C
        $offset = $reader.ReadUInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Missing PE signature' }
        switch ($reader.ReadUInt16()) {
            0x8664 { return 'x64' }
            0x014C { return 'x86' }
            default { throw 'Unsupported PE architecture' }
        }
    } finally {
        if ($null -ne $reader) { $reader.Dispose() }
        $stream.Dispose()
    }
}
$architecture = Read-Architecture $TestExecutable
if ((Read-Architecture $BaselineDll) -ne $architecture -or (Read-Architecture $CurrentDll) -ne $architecture) {
    throw 'EXE and DLL architectures differ'
}

function Measure-Variant([string]$Dll, [string]$Mode) {
    $lines = @(& $TestExecutable --focus-benchmark $Dll $Mode 2>&1)
    $exit = $LASTEXITCODE
    if ($exit -ne 0) { throw "Benchmark failed (exit=$exit): $($lines -join [Environment]::NewLine)" }
    $result = @($lines | Where-Object { "$_".StartsWith('[BENCH] ') })
    if ($result.Count -ne 1) { throw 'Missing or duplicate benchmark result' }
    $row = "$($result[0])".Substring(8) | ConvertFrom-Json
    $required = @('schema', 'metric', 'mode', 'samples', 'warmup', 'p50_ns', 'p95_ns', 'p99_ns', 'maximum_ns', 'events', 'incomplete')
    $names = @($row.PSObject.Properties.Name)
    if ($names.Count -ne $required.Count -or @($required | Where-Object { $_ -notin $names }).Count -ne 0) {
        throw 'Unexpected benchmark result fields'
    }
    foreach ($name in @('schema', 'samples', 'warmup', 'p50_ns', 'p95_ns', 'p99_ns', 'maximum_ns', 'events')) {
        $value = $row.$name
        if (($value -isnot [int] -and $value -isnot [long]) -or $value -lt 0) { throw "Invalid integer: $name" }
    }
    $expectedMode = if ($Mode -eq 'file') { 'private-etl' } else { 'off' }
    $expectedEvents = if ($Mode -eq 'file') { 33000 } else { 0 }
    if ($row.schema -ne 1 -or $row.metric -ne 'focus-gained' -or $row.mode -ne $expectedMode -or
        $row.samples -ne 10000 -or $row.warmup -ne 1000 -or $row.events -ne $expectedEvents -or
        $row.incomplete -isnot [bool] -or $row.incomplete -or
        $row.p50_ns -gt $row.p95_ns -or $row.p95_ns -gt $row.p99_ns -or $row.p99_ns -gt $row.maximum_ns) {
        throw 'Invalid benchmark result'
    }
    return $row
}

$groups = @()
for ($index = 1; $index -le 5; ++$index) {
    if ($index % 2 -eq 1) {
        $baseline = Measure-Variant $BaselineDll 'off'
        $off = Measure-Variant $CurrentDll 'off'
        $file = Measure-Variant $CurrentDll 'file'
    } else {
        $file = Measure-Variant $CurrentDll 'file'
        $off = Measure-Variant $CurrentDll 'off'
        $baseline = Measure-Variant $BaselineDll 'off'
    }
    $groups += [ordered]@{
        group = $index
        baseline = $baseline
        off = $off
        private_etl = $file
        off_delta_ns = $off.p95_ns - $baseline.p95_ns
        private_etl_delta_ns = $file.p95_ns - $baseline.p95_ns
        off_budget_ns = [Math]::Max($baseline.p95_ns * 0.01, 5000)
        enabled_reference_budget_ns = [Math]::Max($baseline.p95_ns * 0.05, 50000)
    }
    Write-Host "group=$index baseline=$($baseline.p95_ns) off=$($off.p95_ns) private-etl=$($file.p95_ns) ns"
}
$offDeltas = @($groups | ForEach-Object { $_.off_delta_ns } | Sort-Object)
$fileDeltas = @($groups | ForEach-Object { $_.private_etl_delta_ns } | Sort-Object)
$offMargins = @($groups | ForEach-Object { $_.off_delta_ns - $_.off_budget_ns } | Sort-Object)
$report = [ordered]@{
    schema = 1
    source_version = $currentVersion
    baseline_sha256 = (Get-FileHash -LiteralPath $BaselineDll -Algorithm SHA256).Hash
    current_sha256 = (Get-FileHash -LiteralPath $CurrentDll -Algorithm SHA256).Hash
    executable_sha256 = (Get-FileHash -LiteralPath $TestExecutable -Algorithm SHA256).Hash
    host = 'qiqi_tsf_ui_test; actual DLL; mock TSF thread manager'
    architecture = $architecture
    operations = 'unchanged focus gained; builtin dictionaries; UI and ordinary logs off'
    groups = $groups
    off_delta_median_ns = $offDeltas[2]
    private_etl_delta_median_ns = $fileDeltas[2]
    preliminary_focus_off_budget_passed = $offMargins[2] -le 0
    acceptance_complete = $false
    exclusions = @('handled key callback budget', 'normal realtime consumer', 'slow consumer', 'toggle', 'cross-process capture', 'real application hosts')
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Host "off median delta=$($offDeltas[2]) ns; private-etl median delta=$($fileDeltas[2]) ns"
