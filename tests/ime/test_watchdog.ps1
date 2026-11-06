param(
    [Parameter(Mandatory = $true)][string]$TestExecutable,
    [Parameter(Mandatory = $true)][string]$Debugger,
    [ValidateSet('full', 'focused', 'probe')][string]$Mode = 'full'
)

$ErrorActionPreference = 'Stop'
$test = $null

function Invoke-BoundedDebugger {
    param([string[]]$Arguments, [string]$OutputPrefix, [int]$Milliseconds)
    $debugProcess = $null
    try {
        $debugProcess = Start-Process -FilePath $Debugger -ArgumentList $Arguments -PassThru `
            -NoNewWindow -RedirectStandardOutput "$OutputPrefix.stdout.txt" -RedirectStandardError "$OutputPrefix.stderr.txt"
        $null = $debugProcess.Handle
        if (-not $debugProcess.WaitForExit($Milliseconds)) {
            throw "cdb exceeded its diagnostic deadline; output: $OutputPrefix"
        }
        if ($null -eq $debugProcess.ExitCode -or $debugProcess.ExitCode -ne 0) {
            throw "cdb failed with exit code $($debugProcess.ExitCode); output: $OutputPrefix"
        }
    } finally {
        if ($null -ne $debugProcess) {
            if (-not $debugProcess.HasExited) { Stop-Process -Id $debugProcess.Id -Force }
            $debugProcess.Dispose()
        }
    }
}

try {
    $TestExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
    $Debugger = (Resolve-Path -LiteralPath $Debugger).Path
    $pdb = [IO.Path]::ChangeExtension($TestExecutable, '.pdb')
    if (-not (Test-Path -LiteralPath $pdb)) { throw "Matching test PDB is required: $pdb" }
    $directory = Join-Path (Get-Location).Path 'test-logs'
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $prefix = Join-Path $directory ("watchdog-{0}-{1}" -f $Mode, [guid]::NewGuid().ToString('N'))
    $arguments = switch ($Mode) {
        'focused' { '--composition-lifecycle-tests' }
        'probe' { '--test-hang-diagnostics-probe' }
        default { $null }
    }
    $startOptions = @{
        FilePath = $TestExecutable
        WorkingDirectory = (Get-Location).Path
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = "$prefix.stdout.txt"
        RedirectStandardError = "$prefix.stderr.txt"
    }
    if ($arguments) { $startOptions.ArgumentList = $arguments }
    $startedAt = [DateTime]::UtcNow
    $test = Start-Process @startOptions
    $null = $test.Handle
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastCompletion = 0.0
    $completed = 0
    $captured = $false
    $timedOut = $false
    $dump = "$prefix.dmp"
    $symbols = "$prefix-symbols"
    $trace = $null
    Write-Output "[WATCHDOG] pid=$($test.Id), inactivity=60s, execution-limit=90s, logs=$prefix"

    while (-not $test.HasExited) {
        if ($null -eq $trace) {
            $trace = Get-ChildItem -LiteralPath $directory -Filter "*-$($test.Id)-*.log" |
                Where-Object { $_.CreationTimeUtc -ge $startedAt } | Select-Object -First 1
        }
        if ($null -ne $trace) {
            $count = @(Select-String -LiteralPath $trace.FullName -Pattern '^\[CASE-END\]').Count
            if ($count -gt $completed) {
                $completed = $count
                $lastCompletion = $timer.Elapsed.TotalSeconds
            }
        }
        if ($timer.Elapsed.TotalSeconds -ge 90) {
            $timedOut = $true
            Write-Output "[HANG] Execution limit reached at elapsed=$($timer.Elapsed.TotalSeconds); stopping owned test pid=$($test.Id)"
            Stop-Process -Id $test.Id -Force
            break
        }
        if (-not $captured -and $timer.Elapsed.TotalSeconds - $lastCompletion -ge 60 -and -not $test.HasExited) {
            $captured = $true
            Write-Output "[HANG] No test completion for 60s at elapsed=$($timer.Elapsed.TotalSeconds); collecting $dump"
            [IO.Directory]::CreateDirectory($symbols) | Out-Null
            Copy-Item -LiteralPath $TestExecutable, $pdb -Destination $symbols
            $commands = "$prefix.capture.commands.txt"
            [IO.File]::WriteAllLines($commands, @(".dump /ma `"$dump`"", 'qd'))
            $remaining = [Math]::Max(1, [Math]::Min(15000, [int]((90 - $timer.Elapsed.TotalSeconds) * 1000)))
            Invoke-BoundedDebugger -Arguments @('-p', "$($test.Id)", '-cf', "`"$commands`"") `
                -OutputPrefix "$prefix.capture" -Milliseconds $remaining
            if (-not (Test-Path -LiteralPath $dump) -or (Get-Item -LiteralPath $dump).Length -eq 0) {
                throw "cdb did not produce a nonempty dump; output: $prefix.capture"
            }
        }
        Start-Sleep -Milliseconds 100
    }
    $test.WaitForExit()
    $exitCode = $test.ExitCode
    if ($null -eq $exitCode) { throw "Unable to read test exit code for pid=$($test.Id)" }
    Get-Content -LiteralPath "$prefix.stdout.txt"
    Get-Content -LiteralPath "$prefix.stderr.txt"
    if ($captured) {
        $commands = "$prefix.analysis.commands.txt"
        [IO.File]::WriteAllLines($commands, @('.reload /f', '!analyze -hang', '~* kv', 'lmv m qiqi_tsf_ui_test', 'q'))
        Invoke-BoundedDebugger -Arguments @('-z', "`"$dump`"", '-y', "`"$symbols`"", '-lines', '-cf', "`"$commands`"") `
            -OutputPrefix "$prefix.analysis" -Milliseconds 30000
        if (-not (Select-String -LiteralPath "$prefix.analysis.stdout.txt" -Pattern 'qiqi_tsf_ui_test.*\(private pdb symbols\)' -Quiet)) {
            throw "Dump analysis did not load matching private test symbols; output: $prefix.analysis.stdout.txt"
        }
        if (Select-String -LiteralPath "$prefix.analysis.stdout.txt" -Pattern 'Symbol Loading Error Summary' -Quiet) {
            Write-Output "[ANALYSIS-WARNING] Some module symbols are missing; automatic hang classification may be incomplete. See $prefix.analysis.stdout.txt"
        }
        Write-Output "[DUMP] Retained dump=$dump, symbols=$symbols, analysis=$prefix.analysis.stdout.txt"
    }
    if ($timedOut) { exit 1 }
    exit $exitCode
} finally {
    if ($null -ne $test) {
        if (-not $test.HasExited) { Stop-Process -Id $test.Id -Force }
        $test.Dispose()
    }
}
