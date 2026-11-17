# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$lintScript = Join-Path $repositoryRoot 'scripts\lint.ps1'
$outputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("QiQiLintFixture-" + [guid]::NewGuid().ToString('N'))
$fixture = @{ Lines = @(); ExitCode = 0; Issues = @() }

function cmake {
    $global:LASTEXITCODE = $fixture.ExitCode
    $fixture.Lines
}

function Invoke-ScriptAnalyzer {
    $fixture.Issues
}

function Assert-LintFixture {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Invoke-LintFixture {
    param([bool]$ExpectFailure)
    $failed = $false
    try {
        & $lintScript -BuildDirectory 'unused-fixture' -Configuration Release -OutputDirectory $outputDirectory
    } catch {
        $failed = $true
        if (-not $ExpectFailure) { throw }
    }
    Assert-LintFixture ($failed -eq $ExpectFailure) 'Unexpected fixture outcome.'
}

try {
    $fixture.Lines = @(
        "$repositoryRoot\build\x64\vcpkg_installed\include\src\external.h(1): warning C26400: external"
        "C:\external\header.h(2): warning C26401: external [$repositoryRoot\src\common\common.vcxproj]"
        "$repositoryRoot\src\common\test.cpp(3): warning C26490: reviewed exclusion"
    )
    Invoke-LintFixture $false
    Assert-LintFixture ((Get-Content -LiteralPath (Join-Path $outputDirectory 'cppcorecheck.log')).Count -eq 3) 'Full compiler output was not retained.'
    foreach ($path in @('src\common\test.cpp', 'tests\common\test.cpp', 'installer\cleanup.cpp')) {
        $fixture.Lines = @("1>$repositoryRoot\$path(4): warning C26400: project warning")
        Invoke-LintFixture $true
        Assert-LintFixture ((Get-Content -LiteralPath (Join-Path $outputDirectory 'project-warnings.log')) -match 'project warning') 'Project warning was not retained.'
    }
    $fixture.Lines = @('synthetic compiler failure')
    $fixture.ExitCode = 9
    Invoke-LintFixture $true
    Assert-LintFixture ((Get-Content -LiteralPath (Join-Path $outputDirectory 'cppcorecheck.log')) -eq 'synthetic compiler failure') 'Failed build output was not retained.'
    $fixture.ExitCode = 0
    $fixture.Lines = @()
    $fixture.Issues = @([pscustomobject]@{ RuleName = 'SyntheticAnalyzerFailure' })
    Invoke-LintFixture $true
    Assert-LintFixture ((Get-Content -LiteralPath (Join-Path $outputDirectory 'powershell-issues.log') -Raw) -match 'SyntheticAnalyzerFailure') 'Analyzer output was not retained.'
    Write-Output 'Lint output fixtures passed; no compiler or analyzer was invoked.'
} finally {
    if (Test-Path -LiteralPath $outputDirectory) {
        Remove-Item -LiteralPath $outputDirectory -Recurse -Force
    }
}
