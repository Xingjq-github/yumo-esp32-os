param([switch]$Online,[switch]$Offline,[switch]$TestOnly)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$server=Join-Path $workspace 'server'
$syncConfig=Join-Path $server 'vault-sync-key.properties'
if(-not (Test-Path -LiteralPath $syncConfig)){throw 'Run python tooling/init_config.py first.'}
$syncLines=@(Get-Content -LiteralPath $syncConfig -Encoding UTF8 | ForEach-Object {$_.Trim()} | Where-Object {$_ -and -not $_.StartsWith('#')})
if($syncLines.Count -ne 1 -or $syncLines[0] -cnotmatch '^sync\.key\.hex[ \t]*=[ \t]*[0-9a-fA-F]{64}$') {
    throw 'Use one sync.key.hex entry with exactly 64 hexadecimal characters.'
}
$maven=(Get-Command mvn.cmd -ErrorAction Stop).Source
$mavenArgs=@('-B','--no-transfer-progress','-s',(Join-Path $PSScriptRoot 'maven-settings.xml'),'-gs',(Join-Path $PSScriptRoot 'maven-global.xml'))
if($Offline){$mavenArgs+='--offline'}
if($env:YUMO_MAVEN_REPOSITORY){$mavenArgs+=('-Dmaven.repo.local='+$env:YUMO_MAVEN_REPOSITORY)}
$mavenArgs+=if($TestOnly){'test'}else{'package'}
Push-Location $server
try { & $maven @mavenArgs; if($LASTEXITCODE -ne 0){throw "Maven failed: $LASTEXITCODE"} } finally { Pop-Location }
