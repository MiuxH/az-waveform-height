# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param([switch]$Release)
$ErrorActionPreference = 'Stop'
function Checked([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit $LASTEXITCODE)" }
}
Push-Location $PSScriptRoot
try {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw 'GitHub CLI fehlt. Einmal installieren: winget install --id GitHub.cli -e. Danach ein neues Terminal oeffnen.'
    }
    & gh auth status
    if ($LASTEXITCODE -ne 0) { Checked 'gh' @('auth','login','--hostname','github.com','--web','--git-protocol','https') }
    $login = (& gh api user --jq '.login').Trim()
    if ($LASTEXITCODE -ne 0 -or $login -ine 'MiuxH') { throw "Angemeldetes Konto ist '$login'; erwartet wird MiuxH. Es wurde nichts veroeffentlicht." }
    if (-not (Test-Path -LiteralPath '.git')) {
        Checked 'git' @('init','-b','main')
        $userId = (& gh api user --jq '.id').Trim()
        if ($LASTEXITCODE -ne 0) { throw 'GitHub-ID konnte nicht gelesen werden.' }
        Checked 'git' @('config','user.name','MiuxH')
        Checked 'git' @('config','user.email',"$userId+MiuxH@users.noreply.github.com")
        Checked 'git' @('add','.')
        Checked 'git' @('commit','-m','Prepare experimental Waveform Height community mod')
    }
    if ($Release) {
        $status = (& git status --porcelain) -join "`n"
        if ($status) { throw 'Bitte Aenderungen zuerst committen. Release abgebrochen.' }
        Checked 'git' @('tag','miuxh-wave-height-0.1.0')
        Checked 'git' @('push','origin','miuxh-wave-height-0.1.0')
        Write-Host 'Release-Build gestartet. Ergebnis unter GitHub > Actions pruefen.'
    } else {
        & gh repo view 'MiuxH/az-waveform-height' *> $null
        if ($LASTEXITCODE -eq 0) {
            throw 'MiuxH/az-waveform-height existiert bereits. Zum Schutz vorhandener Arbeit nicht automatisch ueberschrieben. README-DE.md beschreibt den manuellen Push.'
        }
        Checked 'gh' @('repo','create','MiuxH/az-waveform-height','--public','--source','.','--remote','origin','--push')
        Write-Host 'Quellcode und Testbuild-Workflow veroeffentlicht. Noch kein Release-Tag gesetzt.'
    }
} finally { Pop-Location }
