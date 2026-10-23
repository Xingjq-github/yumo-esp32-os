param([ValidateSet('start','stop','status','open')][string]$Action='status')
$ErrorActionPreference='Stop'
$vaultWorkspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$vaultPcRoot=Join-Path $vaultWorkspace 'runtime\server'
$configFile=Join-Path $PSScriptRoot 'local-config.json'
if(-not (Test-Path -LiteralPath $configFile)){$configFile=Join-Path $PSScriptRoot 'local-config.json.example'}
$vaultConfig=Get-Content -LiteralPath $configFile -Raw -Encoding UTF8 | ConvertFrom-Json
if($vaultConfig.java -eq 'java') {
    if($env:JAVA_HOME){$vaultConfig.java=Join-Path $env:JAVA_HOME 'bin\java.exe'}
    else{$vaultConfig.java=(Get-Command java.exe -ErrorAction Stop).Source}
} else {$vaultConfig.java=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $vaultConfig.java))}
$vaultUrl=[Uri]$vaultConfig.url
$vaultState=Join-Path $vaultPcRoot 'state\active.json'
function Read-VaultState {
    if (Test-Path -LiteralPath $vaultState) { return Get-Content -LiteralPath $vaultState -Raw -Encoding UTF8 | ConvertFrom-Json }
    return $null
}
function Owned-VaultProcess($state) {
    if ($null -eq $state) { return $null }
    $process=Get-Process -Id $state.pid -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $null }
    $recorded=[DateTimeOffset]::Parse($state.started).UtcDateTime
    if ([Math]::Abs(($process.StartTime.ToUniversalTime()-$recorded).TotalSeconds) -gt 2) { return $null }
    return $process
}
function Protect-VaultFolder([string]$path) {
    $resolved=[IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith($vaultPcRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Directory is outside runtime/server.' }
    New-Item -ItemType Directory -Path $resolved -Force | Out-Null
    # Load/write only the DACL, never owner or SACL (which can require admin privileges).
    $acl=[IO.Directory]::GetAccessControl($resolved,[Security.AccessControl.AccessControlSections]::Access)
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
    $system=New-Object Security.Principal.SecurityIdentifier('S-1-5-18')
    $rules=@($acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier]))
    $correct=$acl.AreAccessRulesProtected -and $rules.Count -eq 2
    foreach($principal in @($sid,$system)) {
        $found=@($rules | Where-Object {
            $_.IdentityReference -eq $principal -and $_.AccessControlType -eq 'Allow' -and
            $_.FileSystemRights -eq 'FullControl' -and -not $_.IsInherited -and
            $_.InheritanceFlags -eq 'ContainerInherit,ObjectInherit' -and $_.PropagationFlags -eq 'None'
        })
        $correct=$correct -and $found.Count -eq 1
    }
    if($correct){return}
    $acl.SetAccessRuleProtection($true,$false)
    foreach($rule in @($acl.GetAccessRules($true,$false,[Security.Principal.SecurityIdentifier]))){$acl.RemoveAccessRuleSpecific($rule)}
    foreach($principal in @($sid,$system)) {
        $rule=New-Object Security.AccessControl.FileSystemAccessRule($principal,'FullControl','ContainerInherit,ObjectInherit','None','Allow')
        $acl.AddAccessRule($rule)
    }
    [IO.Directory]::SetAccessControl($resolved,$acl)
}
if ($Action -eq 'open') { Start-Process -FilePath $vaultConfig.url; exit 0 }
$state=Read-VaultState
$owned=Owned-VaultProcess $state
if ($Action -eq 'status') {
    if ($null -eq $owned) { Write-Output 'Java vault is stopped.'; exit 0 }
    try { $health=Invoke-RestMethod -Uri ($vaultConfig.url+'/healthz') -TimeoutSec 5; Write-Output ('Java vault PID '+$owned.Id+' '+$health.status+' '+$vaultConfig.url) }
    catch { Write-Output ('Java vault PID '+$owned.Id+' is running, but health check failed.'); exit 1 }
    exit 0
}
if ($Action -eq 'stop') {
    if ($null -eq $owned) { Write-Output 'Java vault is already stopped.'; exit 0 }
    $client=New-Object Net.Sockets.TcpClient
    try {
        $connect=$client.ConnectAsync('127.0.0.1',[int]$state.port)
        if(-not $connect.Wait(3000)){throw 'Local control connection timed out.'}
        $stream=$client.GetStream();$stream.ReadTimeout=3000;$stream.WriteTimeout=3000
        $bytes=[Text.Encoding]::ASCII.GetBytes('STOP '+$state.token+"`n")
        $stream.Write($bytes,0,$bytes.Length);$stream.Flush()
        $reader=New-Object IO.StreamReader($stream)
        if($reader.ReadLine() -ne 'OK'){throw 'Local control request rejected.'}
    } finally { $client.Dispose() }
    if(-not $owned.WaitForExit(30000)){throw 'Graceful shutdown timed out; no process was killed.'}
    Write-Output 'Java vault stopped gracefully.'; exit 0
}
if ($null -ne $owned) { Write-Output ('Java vault already running: '+$vaultConfig.url); exit 0 }
if (-not (Test-Path -LiteralPath $vaultConfig.java)) { throw 'Configured Java runtime was not found.' }
if (Get-NetTCPConnection -State Listen -LocalPort $vaultUrl.Port -ErrorAction SilentlyContinue) { throw 'The configured port is already in use; no process was stopped.' }
foreach($folder in @('data','state','logs','tmp')){Protect-VaultFolder (Join-Path $vaultPcRoot $folder)}
$jar=Join-Path $vaultWorkspace 'artifacts\server\joyvault.jar'
if(-not (Test-Path -LiteralPath $jar)){throw 'The PC service jar was not found.'}
$values=@{
    VAULT_PUBLIC_URL=$vaultConfig.url; VAULT_BIND_IP=$vaultUrl.Host;
    VAULT_DATA_DIR=(Join-Path $vaultPcRoot 'data');
    VAULT_KEYS_FILE=(Join-Path $vaultWorkspace 'server\deployment\local-keys.json');
    VAULT_CONTROL_FILE=$vaultState;VAULT_LOG_FILE=(Join-Path $vaultPcRoot 'logs\vault.log')
}
$previous=@{}
foreach($name in $values.Keys){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$values[$name],'Process')}
try {
    $javaArgs=@('-Xms32m','-Xmx192m','-Xss512k','-XX:MaxMetaspaceSize=96m','-XX:ReservedCodeCacheSize=32m','-XX:MaxDirectMemorySize=16m','-XX:ActiveProcessorCount=2','-XX:+UseSerialGC','-XX:+ExitOnOutOfMemoryError','-Dfile.encoding=UTF-8',('"-Djava.io.tmpdir='+(Join-Path $vaultPcRoot 'tmp')+'"'),'-jar',('"'+$jar+'"'))
    $process=Start-Process -FilePath $vaultConfig.java -ArgumentList $javaArgs -WorkingDirectory $vaultPcRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $vaultPcRoot 'logs\bootstrap.stdout.log') -RedirectStandardError (Join-Path $vaultPcRoot 'logs\bootstrap.stderr.log') -PassThru
} finally { foreach($name in $previous.Keys){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')} }
$deadline=[DateTime]::UtcNow.AddSeconds(30)
do {
    if($process.HasExited){throw 'Service exited; inspect runtime/server/logs. No data was reset.'}
    try {$health=Invoke-RestMethod -Uri ($vaultConfig.url+'/healthz') -TimeoutSec 2;if($health.status -eq 'ok' -and (Test-Path -LiteralPath $vaultState)){Write-Output ('Java vault started: '+$vaultConfig.url);exit 0}}catch{}
    Start-Sleep -Milliseconds 200
} while ([DateTime]::UtcNow -lt $deadline)
throw 'Startup health check timed out; inspect logs and status before retrying.'
