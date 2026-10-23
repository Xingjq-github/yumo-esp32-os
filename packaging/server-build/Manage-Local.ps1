param([ValidateSet('start','stop','status','open','build','test')][string]$Action='status')
$ErrorActionPreference='Stop'
if($Action -in @('start','stop','status','open')) {
    & (Join-Path $PSScriptRoot '..\pc\Manage-Local.ps1') -Action $Action
} elseif($Action -eq 'test') {
    & (Join-Path $PSScriptRoot '..\..\tooling\server\Build-Server.ps1') -TestOnly
} else {
    & (Join-Path $PSScriptRoot '..\..\tooling\server\Build-Server.ps1')
}
exit $LASTEXITCODE
