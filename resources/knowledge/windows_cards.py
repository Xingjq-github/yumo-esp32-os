"""Windows command cards. Shell dialects and mutation scope are explicit."""
from card import make

def cards():
    out=[]
    def cmd(slug,title,overview,code,pitfall,reference=None):
        out.append(make('tool.cmd.'+slug,title,'工具','CMD',overview,code,pitfall,
            'https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/'+(reference or slug),
            'Windows 10/11 CMD；示例在命令提示符中执行',aliases=['cmd',slug],language='shell'))
    def ps(slug,title,overview,code,pitfall,reference):
        out.append(make('tool.powershell.'+slug,title,'工具','PowerShell',overview,code,pitfall,
            'https://learn.microsoft.com/en-us/powershell/'+reference,
            'Windows PowerShell 5.1 / PowerShell 7；个别平台差异见说明',aliases=['powershell','pwsh',slug],language='shell'))

    cmd('help','CMD帮助与版本','help列出内建命令；命令后加/?查看参数。CMD和PowerShell不能直接混用语法。',r'''
ver
help
dir /?
robocopy /?
''','PowerShell中的dir、copy等名称通常是别名，不能照搬CMD参数。')
    cmd('cd','切换目录与盘符','cd /d可同时切换盘符和目录；含空格路径加双引号。',r'''
cd
cd /d ".\Work Folder"
cd ..
pushd ".\Work Folder"
popd
''','pushd保存目录，popd返回；目录需已存在。跨盘符切换时使用cd /d；相对路径则基于当前目录解析。')
    cmd('dir','列出与查找文件','/a显示隐藏等属性文件，/b简洁格式，/s递归，/o:n按名称排序。',r'''
dir /a /o:n
dir /b *.txt
dir /s /b ".\demo\*.log"
''','递归大目录输出可能很多；这是文件名匹配，不是文件内容检索。')
    cmd('mkdir','创建目录','mkdir或md创建目录，默认命令扩展支持多级目录。',r'''
mkdir ".\demo\logs"
mkdir ".\demo\output"
''','已存在同名文件时无法创建目录；先确认目标盘符和权限。','md')
    cmd('type','查看文件内容','type输出文本，more分页显示；type本身不会识别所有编码。',r'''
type ".\demo\config.txt"
more ".\demo\app.log"
type ".\demo\app.log" | more
''','乱码时核对文件编码与控制台代码页；二进制文件不要用type查看。')
    cmd('findstr','检索文本 findstr','/n显示行号，/i忽略大小写，/l按字面量，/c指定完整搜索串。',r'''
findstr /n /i /l /c:"error" ".\demo\app.log"
findstr /s /n /l /c:"TODO" ".\demo\*.java"
''','正则语法有限且有编码限制；复杂Unicode文本优先PowerShell的Select-String。')
    cmd('copy','复制文件与目录','copy适合文件；xcopy可递归目录，/i将目标视为目录。',r'''
copy /-y ".\demo\config.txt" ".\demo\config-copy.txt"
xcopy ".\demo\src" ".\demo\copy" /e /i /-y
''','/-y要求覆盖确认。批量目录复制可用robocopy；不要把目标放在源目录内部。')
    cmd('robocopy','稳健复制 robocopy','/e包括空目录，/r和/w限制失败重试；先用/l只列出计划。',r'''
robocopy ".\demo\src" ".\demo\copy" /e /r:2 /w:1 /l
robocopy ".\demo\src" ".\demo\copy" /e /r:2 /w:1
echo %ERRORLEVEL%
''','退出码0至7不代表复制失败，8及以上才表示至少一项失败。/mir会删除目标多余文件，此示例不使用。')
    cmd('move','移动与重命名','move可移动文件，ren在同目录改名。',r'''
move /-y ".\demo\old.txt" ".\demo\output\old.txt"
ren ".\demo\output\old.txt" "new.txt"
''','目标目录需存在；ren的第二个参数是新名称，不是新路径。')
    cmd('del','删除文件与空目录','del删除文件，rmdir删除目录。确认路径后再操作。',r'''
dir ".\demo\old.txt"
del /p ".\demo\old.txt"
rmdir ".\demo\empty"
''','del通常不进回收站；rmdir /s递归删除且危险。不要对未经确认的变量或根目录执行删除。')
    cmd('set','环境变量与变量展开','set设置当前CMD及后续子进程的环境；百分号包围变量名来读取。',r'''
set "APP_ENV=dev"
echo %APP_ENV%
set APP_ENV
set "APP_ENV="
''','变量最后一行被删除。setx影响后续新进程而非当前窗口；不要用它盲目改写PATH。','set_1')
    cmd('where','定位程序与退出码','where按当前目录和PATH查找可执行文件；退出码常用于判断上条命令结果。',r'''
where java
where python
where git
echo %ERRORLEVEL%
''','直接命令行中立即读取ERRORLEVEL；括号批处理块的百分号变量可能在整块执行前展开。')
    cmd('tasklist','进程查询与终止','tasklist查看进程，taskkill按PID请求终止，操作前确认目标。',r'''
tasklist
tasklist /fi "IMAGENAME eq java.exe"
tasklist /fi "PID eq 1234"
taskkill /pid 1234
''','1234必须替换为确认过的PID；/f强制结束可能丢失未保存数据，服务优先使用自身停止入口。')
    cmd('ipconfig','网络地址与连通性','ipconfig查看地址，ping测试ICMP连通性，tracert查看路径。',r'''
ipconfig /all
ping -n 4 192.168.1.1
tracert example.com
nslookup example.com
''','网关地址需替换；ICMP失败不等于HTTP服务不可访问，DNS查询成功也不代表服务可用。')
    cmd('netstat','端口与连接定位','netstat -ano展示监听和连接及其PID，再用tasklist定位进程。',r'''
netstat -ano
netstat -ano | findstr /l /c:":8080"
tasklist /fi "PID eq 1234"
''','文本筛选可能同时匹配本地或远端端口及较长端口号，需逐行核对；不要据此直接终止进程。')
    cmd('systeminfo','主机与用户信息','systeminfo输出系统版本等信息，hostname查看主机名，whoami查看当前用户。',r'''
hostname
whoami
systeminfo
''','systeminfo包含本机配置和补丁信息，分享输出前检查隐私；执行耗时可能较长。')
    cmd('fc','文件比较与校验','fc比较文本或二进制文件；certutil -hashfile计算摘要以校验文件一致性。',r'''
fc /n ".\demo\a.txt" ".\demo\b.txt"
fc /b ".\demo\a.bin" ".\demo\b.bin"
certutil -hashfile ".\demo\app.jar" SHA256
''','摘要相同用于一致性校验，不等于来源可信；fc有差异时会返回非零退出码。')
    cmd('cmd','重定向与条件执行','>覆盖输出，>>追加；2>&1合并错误输出；&&在前一命令成功后执行下一条。',r'''
dir ".\demo" > ".\demo\files.txt"
java -version > ".\demo\java.log" 2>&1
where java && java -version
where missing-tool || echo Tool not found
''','重定向会创建或覆盖目标文件；&&和||按命令退出码判断，不适用于robocopy的全部成功状态。')
    cmd('for','批处理循环与判断','下面为.cmd/.bat文件内容。批处理for变量使用双百分号，交互CMD改为单百分号。',r'''
@echo off
setlocal
if exist ".\demo\config.txt" echo Config exists
for %%F in (".\demo\*.txt") do echo %%~nxF
endlocal
''','不要将批处理中的%%F直接照搬到交互窗口；复杂变量依赖需要理解延迟展开。')
    cmd('chcp','控制台编码','chcp查看当前代码页，65001为UTF-8；切换代码页不等于转换磁盘文件。',r'''
chcp
chcp 65001
type ".\demo\utf8.txt"
''','旧程序可能不兼容UTF-8代码页。记录原代码页，需要时用chcp恢复；字体仍需支持对应字符。')

    ps('help','帮助与PowerShell版本','Get-Help查看语法和示例，Get-Command查找命令，PSVersionTable确认引擎版本。',r'''
$PSVersionTable.PSVersion
Get-Command Get-ChildItem
Get-Help Get-ChildItem -Examples
Get-Help Copy-Item -Full
''','本机帮助不完整时可用-Online打开官方资料；不要把CMD的/?视为统一参数。','module/microsoft.powershell.core/get-help')
    ps('location','目录定位与切换','Get-Location查看位置；Set-Location切换路径，Push/Pop保留返回位置。',r'''
Get-Location
Set-Location -LiteralPath '.\Work Folder'
Push-Location -LiteralPath '.\demo'
Pop-Location
''','-LiteralPath按原样处理方括号等字符，目录必须存在。','module/microsoft.powershell.management/set-location')
    ps('list','列出与筛选文件','Get-ChildItem返回对象，可按类型、名称、属性继续筛选。',r'''
Get-ChildItem -LiteralPath '.\demo' -Force
Get-ChildItem -LiteralPath '.\demo' -File -Recurse -Filter '*.log'
Get-ChildItem -LiteralPath '.\demo' -File | Select-Object Name,Length,LastWriteTime
''','-Recurse会遍历子目录；访问受限目录可能报错，不应把未读到的数据默认为不存在。','module/microsoft.powershell.management/get-childitem')
    ps('new','创建文件与目录','New-Item创建对象；Test-Path检查路径，创建前确认是否存在。',r'''
Test-Path -LiteralPath '.\demo'
New-Item -ItemType Directory -Path '.\demo\output'
New-Item -ItemType File -Path '.\demo\output\notes.txt'
''','路径是示例；不要随意加-Force覆盖已有对象。','module/microsoft.powershell.management/new-item')
    ps('copy-move','复制与移动文件','Copy-Item复制，Move-Item移动，Rename-Item改名；-WhatIf只展示计划。',r'''
Copy-Item -LiteralPath '.\demo\config.txt' -Destination '.\demo\copy.txt' -WhatIf
Copy-Item -LiteralPath '.\demo\config.txt' -Destination '.\demo\copy.txt' -Confirm
Move-Item -LiteralPath '.\demo\copy.txt' -Destination '.\demo\output' -Confirm
Rename-Item -LiteralPath '.\demo\output\copy.txt' -NewName 'new.txt' -Confirm
''','目标目录需存在；复制目录使用-Recurse，注意源目录与目标目录层级。','module/microsoft.powershell.management/copy-item')
    ps('remove','删除文件与目录','先列出目标，再用-WhatIf预演；-Confirm要求确认。',r'''
Get-Item -LiteralPath '.\demo\old.txt'
Remove-Item -LiteralPath '.\demo\old.txt' -WhatIf
Remove-Item -LiteralPath '.\demo\old.txt' -Confirm
''','通常不会进入回收站；-Recurse用于目录树，-Force不应替代目标核验。','module/microsoft.powershell.management/remove-item')
    ps('content','读取写入与跟踪日志','Get-Content读文本，-Raw返回一个字符串，-Tail/-Wait适合跟踪日志。',r'''
Get-Content -LiteralPath '.\demo\config.txt' -Raw -Encoding UTF8
Get-Content -LiteralPath '.\demo\app.log' -Tail 30 -Wait
# Stop following with Ctrl+C before continuing.
Set-Content -LiteralPath '.\demo\notes.txt' -Value 'hello' -Encoding UTF8
Add-Content -LiteralPath '.\demo\notes.txt' -Value 'world' -Encoding UTF8
''','Set-Content覆盖原内容。UTF8在5.1通常带BOM、7通常不带BOM；与外部程序交换文件要明确编码。','module/microsoft.powershell.management/get-content')
    ps('search','文本搜索 Select-String','-SimpleMatch按固定字符串匹配；否则Pattern按正则解释。',r'''
Select-String -LiteralPath '.\demo\app.log' -Pattern 'ERROR' -SimpleMatch
Get-ChildItem -LiteralPath '.\demo' -File -Recurse -Filter '*.java' |
    Select-String -Pattern 'TODO' -SimpleMatch
''','默认不区分大小写，严格区分用-CaseSensitive；结果包含匹配行和行号。','module/microsoft.powershell.utility/select-string')
    ps('pipeline','对象筛选排序与统计','PowerShell管道通常传递对象。Where筛选、Sort排序、Select投影，Measure汇总。',r'''
Get-ChildItem -LiteralPath '.\demo' -File |
    Where-Object { $_.Length -gt 1MB } |
    Sort-Object Length -Descending |
    Select-Object -First 10 Name,Length
Get-ChildItem -LiteralPath '.\demo' -File | Measure-Object Length -Sum
''','Format-Table只用于最终展示，不要在导出或对象处理之前插入格式化命令。','module/microsoft.powershell.core/about/about_pipelines')
    ps('variables','变量数组与字典','普通变量用$名称，数组用@()，哈希表用@{}；环境变量使用$env:。',r'''
$name = 'Ada'
$scores = @(80,90,100)
$user = @{ Name=$name; Enabled=$true }
$scores[0]
$user.Name
$env:APP_ENV = 'dev'
Remove-Item Env:\APP_ENV
''','不要重用HOME、PID等自动变量；示例环境变量仅影响当前进程及后续子进程。','module/microsoft.powershell.core/about/about_variables')
    ps('quoting','引号与调用外部程序','单引号不展开变量，双引号会展开；带空格的程序路径用&调用。',r'''
$name = 'Ada'
'Hello $name'
"Hello $name"
& (Join-Path $env:JAVA_HOME 'bin\java.exe') -version
$LASTEXITCODE
''','程序路径需替换。LASTEXITCODE是最近外部程序退出码，不是所有cmdlet的状态；避免Invoke-Expression执行拼接字符串。','module/microsoft.powershell.core/about/about_quoting_rules')
    ps('process','查看与停止进程','Get-Process查询进程对象，Stop-Process按核对过的进程ID终止。',r'''
Get-Process | Sort-Object CPU -Descending | Select-Object -First 10 Name,Id,CPU,WorkingSet64
Get-Process -Id 1234
Stop-Process -Id 1234 -WhatIf
Stop-Process -Id 1234 -Confirm
''','CPU为累计处理器秒数，不是实时CPU百分比；停止进程不保证应用完成保存，优先使用正常退出。','module/microsoft.powershell.management/get-process')
    ps('services','Windows服务管理','Get-Service查看状态，Start/Stop/Restart控制服务，设置启动方式是另一操作。',r'''
Get-Service -Name 'Spooler'
Stop-Service -Name 'Spooler' -WhatIf
Start-Service -Name 'Spooler' -WhatIf
Restart-Service -Name 'Spooler' -WhatIf
''','示例是Windows打印服务且只预演。真实变更通常需要管理员权限，并会影响相关业务。','module/microsoft.powershell.management/get-service')
    ps('network','测试网络与TCP端口','Windows的Test-NetConnection可验证目标TCP端口；Get-NetTCPConnection查看连接。',r'''
Test-NetConnection -ComputerName example.com -Port 443
Get-NetTCPConnection -LocalPort 8080 -ErrorAction SilentlyContinue |
    Select-Object LocalAddress,LocalPort,State,OwningProcess
Resolve-DnsName example.com
''','这些命令依赖Windows网络模块，并非跨平台PowerShell自带；端口连通不等于业务请求成功。','module/nettcpip/test-netconnection')
    ps('http','请求接口与下载文件','Invoke-RestMethod适合JSON等接口响应；Invoke-WebRequest可下载文件。',r'''
Invoke-RestMethod -Uri 'https://example.com/api/items' -Method Get -TimeoutSec 10
$body = @{ name='Ada' } | ConvertTo-Json
Invoke-RestMethod -Uri 'https://example.com/api/items' -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10
Invoke-WebRequest -UseBasicParsing -Uri 'https://example.com/file.txt' -OutFile '.\demo\file.txt' -TimeoutSec 10
''','example.com接口仅占位，需替换真实地址；POST可能修改数据。Windows 5.1使用-UseBasicParsing避免旧网页解析器依赖。','module/microsoft.powershell.utility/invoke-restmethod')
    ps('json-csv','JSON与CSV转换','ConvertFrom-Json解析JSON，ConvertTo-Json生成JSON，Import/Export-Csv处理表格对象。',r'''
$item = '{"name":"Ada","score":90}' | ConvertFrom-Json
$item.name
$item | ConvertTo-Json -Depth 10
@($item) | Export-Csv -LiteralPath '.\demo\items.csv' -NoTypeInformation -Encoding UTF8
Import-Csv -LiteralPath '.\demo\items.csv' -Encoding UTF8
''','ConvertTo-Json默认深度较小，嵌套数据显式指定Depth；CSV导入字段通常是字符串，数值需按需要转换。','module/microsoft.powershell.utility/convertto-json')
    ps('archive','压缩与解压ZIP','Compress-Archive创建ZIP，Expand-Archive解压；在独立目录操作。',r'''
Compress-Archive -LiteralPath '.\demo\report.txt' -DestinationPath '.\demo\report.zip'
Expand-Archive -LiteralPath '.\demo\report.zip' -DestinationPath '.\demo\unpack'
''','模块对隐藏文件及大文件存在限制，不作为完整系统备份工具；不要对未知ZIP使用-Force覆盖已有文件。','module/microsoft.powershell.archive/compress-archive')
    ps('hash','文件摘要与差异比较','Get-FileHash计算文件摘要；Compare-Object比较两组输入对象。',r'''
Get-FileHash -LiteralPath '.\demo\app.jar' -Algorithm SHA256
Compare-Object (Get-Content -LiteralPath '.\demo\a.txt') (Get-Content -LiteralPath '.\demo\b.txt')
''','Compare-Object不等同于逐行位置diff，重复和顺序的解释要按需求核对；哈希不提供来源认证。','module/microsoft.powershell.utility/get-filehash')
    ps('control','条件循环与异常处理','if判断、foreach遍历，try/catch处理终止性错误；-ErrorAction Stop将常见非终止错误升级。',r'''
foreach ($file in Get-ChildItem -LiteralPath '.\demo' -File) {
    if ($file.Length -gt 1MB) { $file.Name }
}
try {
    Get-Content -LiteralPath '.\demo\missing.txt' -ErrorAction Stop
} catch {
    Write-Warning $_.Exception.Message
}
''','原生命令失败通常还需检查LASTEXITCODE，不能假定所有非零退出码都自动进入catch。','module/microsoft.powershell.core/about/about_try_catch_finally')
    ps('scripts','脚本参数与执行策略','下面是保存为demo.ps1的脚本内容；param必须位于脚本有效语句开头。',r'''
param([string]$Name='Ada')
"Hello $Name"
# From a separate PowerShell prompt:
# .\demo.ps1 -Name Bob
# Get-ExecutionPolicy -List
''','先审查脚本再运行。执行策略不是安全边界，不要为运行陌生脚本全局关闭策略；组策略可能优先。','module/microsoft.powershell.core/about/about_scripts')
    return out
