from card import table
def cards():
    out=[]
    def add(group,url,prefix,text,lang='shell'):out.extend(table(text,'工具',group,url,'命令或格式速查；先核验本机版本和目标路径',prefix,lang))
    add('Git','https://git-scm.com/docs','tool.git.',r'''
status ~~ Git查看变更 ~~ status展示工作区、暂存区与分支状态；diff查看尚未暂存和已暂存内容。 ~~ git status --short\ngit diff\ngit diff --cached ~~ diff默认不展示未跟踪文件内容，提交前同时核对status。
commit ~~ Git暂存与提交 ~~ add选择进入下一提交的内容，commit建立本地历史节点，push才发布远端。 ~~ git add -- src/File.java\ngit diff --cached\ngit commit -m "Fix validation" ~~ 只暂存本次范围，避免误提交凭据、产物与他人未完成修改。
branch ~~ Git分支与合并 ~~ switch切换分支，merge整合另一分支历史；冲突解决后核对最终代码。 ~~ git switch -c feature/example\ngit log --oneline --graph -10 ~~ 切分支前确认工作区状态；分支名不等于独立工作目录。
restore ~~ Git恢复与撤销 ~~ restore可恢复文件，revert生成反向提交用于保留历史的撤销。 ~~ git restore --staged -- src/File.java\ngit show --stat HEAD ~~ 取消暂存不删除工作区修改；restore工作区和reset --hard可能丢失未提交内容，操作前确认目标。
remote ~~ Git远端同步 ~~ fetch下载远端引用但不直接合并当前分支，先查看差异再选择merge或rebase。 ~~ git fetch origin\ngit log --oneline HEAD..origin/main ~~ rebase重写提交身份，共享分支需团队协商；不要盲目强制推送。
''')
    add('Maven','https://maven.apache.org/guides/','tool.maven.',r'''
lifecycle ~~ Maven生命周期 ~~ 常用阶段validate、compile、test、package、verify、install、deploy按顺序执行先前阶段。 ~~ mvn verify\nmvn package ~~ package生成包不等于部署运行；跳过测试会降低构建验证强度。
dependencies ~~ Maven依赖范围 ~~ compile、provided、runtime、test等scope影响编译和运行类路径，版本冲突遵循依赖调解规则。 ~~ mvn dependency:tree\nmvn help:effective-pom ~~ 最近路径胜出不等于版本最合适；用dependencyManagement统一约束。
reproducible ~~ Maven可复现构建 ~~ 固定插件和依赖版本，明确JDK与源码目标，减少环境差异。使用wrapper可统一Maven版本。 ~~ mvn -version\nmvn help:effective-settings ~~ 不在pom和日志中写凭据。源码目标与运行时JRE兼容都要核验。
''')
    add('Linux','https://www.gnu.org/software/coreutils/manual/coreutils.html','tool.linux.',r'''
files ~~ 文件路径与容量 ~~ pwd确认当前位置，ls查看文件，df看文件系统可用空间，du估计目录占用。 ~~ pwd\nls -lah\ndf -h\ndu -sh ./data ~~ 删除前确认绝对路径；df与du可能因打开但已删除的文件等原因不一致。
logs ~~ 检索与跟踪日志 ~~ tail看末尾，grep筛选文本，less交互阅读大文件，避免把整个日志读进内存。 ~~ tail -n 100 app.log\ngrep -n "ERROR" app.log\nless app.log ~~ 持续tail输出不代表服务健康；日志轮转后应确认跟踪方式和目标文件。
process ~~ 进程与资源 ~~ ps查看进程，top观察CPU和内存；优先正常停止，让程序关闭资源并保存状态。 ~~ ps -ef\ntop\nfree -h ~~ Linux可回收缓存不等于真正内存耗尽；强制kill可能中断文件写入。
permissions ~~ 权限与归属 ~~ 读写执行权限分别针对所有者、组和其他用户，目录执行权限表示可进入和遍历。 ~~ ls -ld ./data\nid\nchmod u+rw ./data/config.txt ~~ 不用chmod 777粗暴解决服务权限；确认服务实际UID和挂载目录归属。
''')
    add('HTTP与数据格式','https://www.rfc-editor.org/rfc/rfc9110','tool.http.',r'''
methods ~~ HTTP方法与幂等 ~~ GET用于读取，POST提交处理，PUT替换目标表示，DELETE删除目标。规范定义的幂等不等于所有实现都正确遵守。 ~~ GET /items/1 HTTP/1.1\nHost: example.com ~~ 方法名不替代鉴权和输入校验；不要用GET触发有副作用的删除。
status ~~ HTTP状态码 ~~ 2xx成功、3xx重定向、4xx请求相关错误、5xx服务侧错误。401需认证，403无权，404未找到。 ~~ HTTP/1.1 200 OK\nContent-Type: application/json\n\n{"ok":true} ~~ 网络超时不代表服务器未处理；重试副作用请求先使用幂等设计。
''','shell')
    add('HTTP与数据格式','https://www.rfc-editor.org/rfc/rfc8259','tool.json.',r'''
syntax ~~ JSON格式 ~~ JSON由对象、数组、字符串、数值、布尔和null组成，字符串及键使用双引号。 ~~ {"name":"Ada","enabled":true,"scores":[80,90],"note":null} ~~ 标准JSON没有注释和尾逗号；大整数跨语言传输要考虑数字精度，必要时使用字符串。
''','json')
    return out
