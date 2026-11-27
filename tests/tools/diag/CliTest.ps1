# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$DiagnosticPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-QiQiDiag([string]$Arguments) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $DiagnosticPath
    $start.Arguments = $Arguments
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
    $start.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdout
            Stderr = $stderr
        }
    } finally {
        $process.Dispose()
    }
}

Assert-True (Test-Path -LiteralPath $DiagnosticPath -PathType Leaf) "Missing QiQiIMEDiag.exe."
$version = (Get-Item -LiteralPath $DiagnosticPath).VersionInfo
Assert-True ($version.OriginalFilename -eq "QiQiIMEDiag.exe") "Missing OriginalFilename version resource."
Assert-True ($version.FileDescription.Contains("QiQi IME Diag")) "Missing diagnostic file-description resource."
Assert-True (-not [string]::IsNullOrWhiteSpace($version.FileVersion)) "Missing file version resource."
Assert-True (
    @(Get-ChildItem -LiteralPath (Split-Path -Parent $DiagnosticPath) -Filter "ftxui*.dll").Count -eq 0
) "FTXUI must be statically linked into QiQiIMEDiag.exe."

$chineseHelp = Invoke-QiQiDiag "--help --language zh-CN"
Assert-True ($chineseHelp.ExitCode -eq 0) "Chinese help failed."
Assert-True ($chineseHelp.Stdout.Contains("语言")) "Chinese help ignored --language."
$englishHelp = Invoke-QiQiDiag "--lang en-US --help"
Assert-True ($englishHelp.ExitCode -eq 0) "English help failed."
Assert-True ($englishHelp.Stdout.Contains("Override IME language")) "English help ignored --lang."
$autoHelp = Invoke-QiQiDiag "--lang auto --help"
Assert-True ($autoHelp.ExitCode -eq 0 -and
    ($autoHelp.Stdout -eq $chineseHelp.Stdout -or $autoHelp.Stdout -eq $englishHelp.Stdout)
) "Explicit auto did not resolve to a Windows UI language."

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("qiqi-diag-cli-" + [guid]::NewGuid().ToString("N"))
try {
    $declinedInput = Join-Path $root "declined.txt"
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    [System.IO.File]::WriteAllLines($declinedInput, @("n", "n", "n", "n"))
    $declinedOutput = Join-Path $root "declined.stdout.txt"
    $declinedError = Join-Path $root "declined.stderr.txt"
    $declinedDirectory = Join-Path $root "declined-report"
    $declined = Start-Process `
        -FilePath $DiagnosticPath `
        -ArgumentList @("--language", "en-US", "--log-directory", $declinedDirectory) `
        -RedirectStandardInput $declinedInput `
        -RedirectStandardOutput $declinedOutput `
        -RedirectStandardError $declinedError `
        -Wait -PassThru
    Assert-True ($declined.ExitCode -eq 0) "Declining stages failed."
    Assert-True (-not (Test-Path -LiteralPath $declinedDirectory)) "Declining every stage created a report directory."

    $approvedParent = Join-Path $root "approved"
    & $DiagnosticPath --yes --lang en-US --log-directory $approvedParent | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) "Approved collection failed."
    $run = @(Get-ChildItem -LiteralPath $approvedParent -Directory)
    Assert-True ($run.Count -eq 1) "Expected exactly one timestamped report directory."
    $files = @(Get-ChildItem -LiteralPath $run[0].FullName -File)
    Assert-True ($files.Count -eq 6) "Expected summary plus five stage/config files."
    foreach ($name in @("summary.json", "machine.json", "processes.json", "ime.json", "broker.json", "dcom.json")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $run[0].FullName $name) -PathType Leaf) "Missing $name."
    }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $run[0].FullName "report.json"))) "A monolithic report.json must not be produced."

    $summary = Get-Content -LiteralPath (Join-Path $run[0].FullName "summary.json") -Raw | ConvertFrom-Json
    Assert-True ($summary.schema -eq "qiqiime-diag") "Unexpected report schema."
    Assert-True ($summary.stages.Count -eq 4) "Unexpected stage count."
    $machine = Get-Content -LiteralPath (Join-Path $run[0].FullName "machine.json") -Raw | ConvertFrom-Json
    Assert-True ($machine.stage.display.monitors.Count -ge 1) "Display monitor data is missing."
    Assert-True ($machine.stage.fonts.chinese_font_faces.Count -ge 1) "Chinese font data is missing."
    $dcom = Get-Content -LiteralPath (Join-Path $run[0].FullName "dcom.json") -Raw | ConvertFrom-Json
    Assert-True ($null -ne $dcom.dcom.inspection) "DCOM configuration file is missing details."
    $broker = Get-Content -LiteralPath (Join-Path $run[0].FullName "broker.json") -Raw | ConvertFrom-Json
    Assert-True ($null -eq $broker.stage.PSObject.Properties["dcom"]) "DCOM details must not remain in broker.json."
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
