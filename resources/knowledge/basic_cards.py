"""Practical baseline operations; examples target disposable demo resources."""
from card import table, JLS, API, JVM, MYSQL

def cards():
    out=[]
    def add(category,group,url,prefix,text,lang='shell',version='常用操作；执行前核验本机版本、权限和目标'):
        out.extend(table(text,category,group,url,version,prefix,lang))
    add('工具','Linux','https://www.gnu.org/software/coreutils/manual/coreutils.html','tool.linux.',r'''
navigation ~~ 目录切换 cd / pwd / ls ~~ 相对路径从当前目录开始，绝对路径从根目录开始。cd是Shell内建命令。 ~~ pwd\nls -lah\ncd /var/log\ncd ..\ncd - ~~ 含空格的路径用引号；cd -返回前一个工作目录。
create ~~ 创建目录与文件 ~~ mkdir -p建立多级目录；touch创建空文件或更新已有文件时间。 ~~ mkdir -p ./demo/logs\ntouch ./demo/logs/app.log\nprintf '%s\\n' 'hello' > ./demo/readme.txt ~~ 重定向 > 会截断已有文件；追加用 >>，先确认路径。
copy-move ~~ 复制与移动 cp / mv ~~ cp复制文件，cp -a保留目录结构和常用属性；mv移动或重命名。 ~~ cp -i ./demo/readme.txt ./demo/copy.txt\ncp -a ./demo ./demo-copy\nmv -i ./demo/copy.txt ./demo/notes.txt ~~ -i覆盖前确认。跨文件系统的mv可能是复制后删除，不是原子重命名。
remove ~~ 删除 rm / rmdir ~~ rmdir仅删除空目录，rm删除文件；目录递归删除需要-r。 ~~ rm -i ./demo/notes.txt\nrmdir ./empty-demo ~~ rm通常不进回收站。不要把未经核验的变量拼入递归删除命令。
read ~~ 查看 cat / head / tail ~~ 小文件用cat；head和tail只输出指定行数，大文件可用less交互查看。 ~~ cat ./demo/readme.txt\nhead -n 20 app.log\ntail -n 50 app.log\ntail -F app.log ~~ tail -F按名称跟踪并重试，适合常见日志轮转；Ctrl+C停止。
sort-count ~~ 排序去重与统计 ~~ sort默认按文本排序，-n按数值；uniq只合并相邻相同行。 ~~ sort names.txt | uniq -c\nsort -n scores.txt\nwc -l names.txt\ncut -d, -f1 data.csv ~~ cut不理解CSV引号和字段内逗号，复杂CSV要用专门解析器。
links ~~ 符号链接 ~~ ln -s创建指向目标路径的链接；相对目标以链接所在目录为基准。 ~~ ln -s ./demo/readme.txt ./readme-link\nreadlink ./readme-link ~~ 删除链接本身与删除目标不同；复制时是否跟随链接受选项影响。
owner ~~ 所有者与权限 chmod / chown ~~ 数字权限每位由读4、写2、执行1相加，目录需执行权限才能遍历。 ~~ chmod 640 ./config.txt\nchmod 750 ./scripts\nchown app:app ./config.txt ~~ chown通常需要特权；不对未知目录递归改权，服务账号需能访问父目录。
env ~~ 环境变量与命令位置 ~~ export让后续子进程继承变量；command -v查当前Shell如何解析命令。 ~~ export APP_ENV=dev\nprintf '%s\\n' "$APP_ENV"\ncommand -v java\nprintenv PATH ~~ 环境变量仅影响当前进程及后续子进程，不会反向修改父Shell；不要打印密钥。
redirect ~~ 管道与输出重定向 ~~ 管道传递标准输出；2>重定向标准错误，2>&1合并到当前标准输出目标。 ~~ command > output.log 2> error.log\ncommand >> combined.log 2>&1\nprintf '%s\\n' one two | wc -l ~~ 重定向按从左到右处理；管道退出码默认通常来自最后一项，Bash可启用pipefail。
disk ~~ 容量与磁盘设备 ~~ df查看文件系统容量，du估算目录占用，lsblk查看块设备关系。 ~~ df -hT\ndu -h --max-depth=1 ./data\nlsblk -f ~~ 示例du选项面向GNU。不要将df可用空间直接等同于可写配额。
help ~~ 命令帮助 ~~ 不确定命令时先看帮助或手册，确认参数针对的程序版本。 ~~ ls --help\nman cp\nhelp cd ~~ help用于Bash内建命令；精简系统可能没有man或完整GNU选项。
''')
    add('工具','Linux','https://www.gnu.org/software/findutils/manual/html_mono/find.html','tool.linux.',r'''
find ~~ 查找文件 find ~~ find按目录树搜索；文件名通配符用引号，避免先被Shell展开。 ~~ find ./logs -type f -name '*.log'\nfind ./logs -type f -mtime +7 -print ~~ 先只输出匹配列表再考虑删除；-mtime以完整24小时段计算，不等于自然日。
''')
    add('工具','Linux','https://www.gnu.org/software/grep/manual/grep.html','tool.linux.',r'''
grep ~~ 文本检索 grep ~~ -n显示行号，-i忽略大小写，-F按固定字符串匹配，-r递归目录。 ~~ grep -nF 'ERROR' app.log\ngrep -rn --include='*.java' 'TODO' ./src\ngrep -E 'WARN|ERROR' app.log ~~ 返回码1通常表示没有匹配，2通常表示错误；不要把所有非零都当同一种故障。
''')
    add('工具','Linux','https://www.gnu.org/software/tar/manual/tar.html','tool.linux.',r'''
archive ~~ 压缩归档 tar ~~ -c创建、-t列出、-x解包，-z使用gzip；先查看归档内容再解包。 ~~ tar -czf demo.tar.gz ./demo\ntar -tzf demo.tar.gz\nmkdir -p ./unpack\ntar -xzf demo.tar.gz -C ./unpack ~~ 在独立目录解压不可信文件；注意绝对路径、链接及覆盖现有文件。
''')
    add('工具','Linux','https://www.freedesktop.org/software/systemd/man/latest/systemctl.html','tool.linux.',r'''
service ~~ 服务管理 systemctl ~~ start立即启动，stop停止，restart重启，enable设置开机启动；两种生命周期不同。 ~~ systemctl status app.service\nsudo systemctl start app.service\nsudo systemctl stop app.service\nsudo systemctl enable app.service ~~ 仅适用于systemd且存在对应unit的主机；容器中通常没有systemd。
journal ~~ 服务日志 journalctl ~~ 按服务、时间范围和条数筛选日志；-f持续跟随。 ~~ journalctl -u app.service -n 100 --no-pager\njournalctl -u app.service --since '1 hour ago'\njournalctl -u app.service -f ~~ 日志可见范围受权限影响，持久保存和保留时间由主机配置决定。
''')
    add('工具','Linux','https://www.kernel.org/doc/man-pages/','tool.linux.',r'''
signals ~~ 停止进程与信号 ~~ TERM请求正常终止，应用可做清理；KILL不能捕获，仅在无法正常结束时考虑。 ~~ ps -ef\nkill -TERM 1234\nkill -0 1234 ~~ 1234为示例PID，必须替换为核对过的目标；kill -0只检查存在与权限。
network ~~ 地址路由与端口 ~~ ip查看地址和路由，ss查看套接字；-l只看监听。 ~~ ip addr show\nip route show\nss -lntp\nss -lun ~~ ip与ss来自iproute2；普通用户可能看不到其他用户进程名，监听不代表外网能访问。
''')
    add('工具','Linux','https://curl.se/docs/manpage.html','tool.linux.',r'''
curl ~~ HTTP请求 curl ~~ -I请求响应头，-f对HTTP错误返回失败，--max-time限制请求总时长。 ~~ curl -I --max-time 10 https://example.com\ncurl -fS --max-time 10 https://example.com/api\ncurl -fS -H 'Content-Type: application/json' -d '{"name":"Ada"}' https://example.com/api ~~ example.com只是占位地址；不要用-k跳过证书验证处理正常服务连接问题。
''')
    add('工具','Linux','https://www.openssh.com/manual.html','tool.linux.',r'''
ssh ~~ 远程登录与复制 ~~ ssh连接远程终端，scp复制文件；端口参数分别为小写-p和大写-P。 ~~ ssh -p 22 user@host\nscp -P 22 ./app.jar user@host:/srv/app/ ~~ 首次连接核对主机指纹；不要将密码直接放命令行或脚本。
''')
    add('工具','Maven','https://maven.apache.org/guides/introduction/introduction-to-the-lifecycle.html','tool.maven.',r'''
start ~~ 项目位置与版本 ~~ 在含pom.xml的目录运行Maven，-f指定其他POM，-v核对实际使用的JDK。 ~~ mvn -v\nmvn -f ./app/pom.xml validate\nmvn -f ./app/pom.xml compile ~~ JAVA_HOME与PATH可能选择不同JDK；先确认mvn -v的Java home。
clean ~~ 清理与重新打包 ~~ clean属于独立生命周期，通常删除target；package执行到打包阶段。 ~~ mvn clean package ~~ clean通常不会删除本地仓库；会移除target内自行存放的文件。
test ~~ 测试与指定测试 ~~ test运行配置给Surefire的测试，-Dtest筛选测试类或方法。 ~~ mvn test\nmvn '-Dtest=OrderTest' test\nmvn '-Dtest=OrderTest#createsOrder' test ~~ 示例方法筛选由Surefire和测试框架支持；集成测试通常通过Failsafe的verify流程。
skip ~~ 跳过测试的区别 ~~ skipTests通常跳过运行但仍编译测试；maven.test.skip通常同时跳过测试编译与执行。 ~~ mvn package -DskipTests\nmvn package -Dmaven.test.skip=true ~~ 插件配置可能改变行为；跳过测试只用于明确场景，交付仍应运行verify。
install ~~ 安装本地与发布远端 ~~ install写入本地仓库，deploy发布到配置的远端制品仓库，都不等于启动服务。 ~~ mvn install\nmvn deploy ~~ deploy需distributionManagement和匹配的settings凭据，可能产生不可覆盖的发布版本。
wrapper ~~ 使用项目 Maven Wrapper ~~ Wrapper按项目配置选定Maven版本，Linux与Windows使用不同脚本。 ~~ ./mvnw verify\n# Windows CMD:\nmvnw.cmd verify ~~ 首次可能下载发行包；提交并审查Wrapper配置，不盲目运行陌生脚本。
profiles ~~ 切换与查看 Profile ~~ -P激活命名Profile；effective-pom展示合并后的有效配置。 ~~ mvn help:active-profiles\nmvn -Pdev verify\nmvn -Pprod help:effective-pom ~~ Maven Profile影响构建，和Spring运行时Profile不是一回事。
modules ~~ 多模块定向构建 ~~ -pl选择模块，-am同时构建其依赖模块；-rf从指定模块恢复反应堆。 ~~ mvn -pl app -am verify\nmvn -rf :app verify ~~ -rf不会自动补齐之前模块产物；缺依赖时从根目录使用-pl和-am。
settings ~~ 指定配置和本地仓库 ~~ -s指定用户settings，maven.repo.local改变本次本地仓库位置。 ~~ mvn -s ./settings.xml verify\nmvn -Dmaven.repo.local=./.m2-repository verify ~~ settings可能有凭据；不要提交到公开仓库，也不要输出完整凭据日志。
offline ~~ 离线与更新快照 ~~ -o禁止联网解析；-U检查缺失release和更新snapshot，离线前需准备依赖与插件。 ~~ mvn dependency:go-offline\nmvn -o verify\nmvn -U verify ~~ go-offline受Profile和插件动态依赖影响，不保证所有后续任务完全离线。
debug ~~ 定位构建错误 ~~ -e输出异常栈，-X输出详细调试信息；先找首个失败模块与根异常。 ~~ mvn -e verify\nmvn -X verify\nmvn dependency:tree -Dverbose ~~ 调试日志可能暴露路径、环境或配置，分享前清理敏感信息。
''')
    add('工具','Maven','https://maven.apache.org/plugins/maven-dependency-plugin/','tool.maven.',r'''
dependency-filter ~~ 筛选依赖冲突 ~~ 按坐标筛选依赖树，检查直接和传递依赖；排除应定位到引入它的依赖节点。 ~~ mvn dependency:tree -Dincludes=org.slf4j:slf4j-api\nmvn dependency:analyze ~~ analyze依赖字节码分析，反射或SPI使用可能被误判未使用；不要直接批量删除。
''')
    add('工具','Git','https://git-scm.com/docs','tool.git.',r'''
init-clone ~~ 初始化与克隆仓库 ~~ init建立本地仓库，clone获取已有仓库及默认检出分支。 ~~ git init demo\ngit clone https://example.com/team/repo.git\ngit remote -v ~~ 示例远端需替换。克隆不等于有提交或推送权限。
history ~~ 日志与定位提交 ~~ log查看历史，show查看具体提交，blame追踪行最近修改来源。 ~~ git log --oneline -10\ngit show HEAD\ngit blame -- src/File.java ~~ blame是最近改动线索，不代表问题责任或最初设计来源。
stash ~~ 临时保存工作区 ~~ stash临时保存改动，-u包含未跟踪文件；apply恢复但保留暂存记录。 ~~ git stash push -u -m 'work in progress'\ngit stash list\ngit stash apply ~~ 被忽略文件默认不包含；恢复可能冲突，核验后再手动drop。
''')
    add('MySQL','入门与命令','https://dev.mysql.com/doc/refman/8.4/en/mysql.html','mysql.',r'''
connect ~~ 连接与退出 mysql ~~ 在操作系统终端启动mysql客户端，-p交互输入密码，避免密码进入命令历史。 ~~ mysql -h 127.0.0.1 -P 3306 -u demo -p\n# In the mysql client:\n# USE demo;\n# quit ~~ -P大写为端口，-p小写为密码。远程地址、权限及TLS需按服务配置。
''')
    add('MySQL','入门与命令',MYSQL,'mysql.',r'''
browse ~~ 查看数据库和表 ~~ 先列库，再选择库、列表示意结构，明确后再读写。 ~~ SHOW DATABASES;\nUSE demo;\nSHOW TABLES;\nDESCRIBE users;\nSHOW CREATE TABLE users; ~~ 只能看到当前账号有权访问的对象；示例users表结构见创建示例表。
query-basic ~~ 查询命令 SELECT 入门 ~~ 完整常用顺序为SELECT字段、FROM表、WHERE条件、ORDER BY排序、LIMIT限制。 ~~ SELECT id,name,score FROM users\nWHERE score>=60 ORDER BY score DESC,id LIMIT 10;\nSELECT COUNT(*) AS total FROM users; ~~ 先明确列和条件；LIMIT若无ORDER BY不能保证每次返回相同记录。
crud-demo ~~ 增删改查最小流程 ~~ 以下针对已建好的users表，在事务内演示并回滚，避免留下示例记录。 ~~ START TRANSACTION;\nINSERT INTO users(id,name,score) VALUES(9001,'Demo',80);\nSELECT id,name,score FROM users WHERE id=9001;\nUPDATE users SET score=90 WHERE id=9001;\nDELETE FROM users WHERE id=9001;\nROLLBACK; ~~ 示例主键必须未被占用，且目标表使用InnoDB。不要在生产数据上练习。
user-grant ~~ 创建业务账号与授权 ~~ MySQL账号包含来源主机，创建后仅授予业务所需权限。 ~~ CREATE USER 'demo'@'localhost' IDENTIFIED BY 'replace-with-a-strong-password';\nGRANT SELECT,INSERT,UPDATE,DELETE ON demo.* TO 'demo'@'localhost';\nSHOW GRANTS FOR 'demo'@'localhost'; ~~ 由管理员执行并替换示例密码；localhost账号不能直接当远程来源账号。
''','sql',version='MySQL 8.4；操作在隔离演示库进行')
    add('Java','基础操作',JVM,'java.',r'''
compile-run ~~ 编译与运行 Java ~~ 先用javac编译，再用java按类名启动；包名、目录和类路径必须对应。 ~~ javac -encoding UTF-8 Hello.java\njava -cp . Hello\njava Hello.java\njava -jar app.jar ~~ 单文件直接运行需要Java11及以后。-jar需要可执行JAR的入口清单。
''')
    add('Java','基础操作',API,'java.',r'''
console ~~ 控制台输入输出 ~~ Scanner读取输入，System.out输出。片段需要在方法中使用并导入相应类。 ~~ Scanner in = new Scanner(System.in);\nString name = in.nextLine();\nint age = Integer.parseInt(in.nextLine());\nSystem.out.printf("%s: %d%n", name, age); ~~ 需要java.util.Scanner。关闭Scanner也关闭底层System.in；数字格式可能无效。
methods ~~ 方法参数与返回值 ~~ 方法声明返回类型和参数类型，调用时传入对应参数。void表示无返回值。 ~~ static int add(int a, int b) {\n    return a + b;\n}\n// Inside a method:\n// int result = add(2, 3); ~~ 声明放类内部而非方法内部；int溢出不会自动变成长整数。
file-lines ~~ 按行读取与写入文件 ~~ 小文件可用Files.readAllLines；大量行用Files.lines并及时关闭流。 ~~ Path p = Path.of("demo.txt");\nFiles.writeString(p, "hello\\n", StandardCharsets.UTF_8);\ntry (Stream<String> lines = Files.lines(p, StandardCharsets.UTF_8)) {\n    lines.forEach(System.out::println);\n} ~~ 需要nio.file、nio.charset、util.stream导入及IOException处理；writeString默认覆盖。
directory ~~ 目录遍历与文件复制 ~~ Files.list仅列直接子项，返回的流必须关闭；copy默认不覆盖已有目标。 ~~ Path dir = Files.createDirectories(Path.of("demo"));\ntry (Stream<Path> files = Files.list(dir)) {\n    files.forEach(System.out::println);\n}\nFiles.copy(Path.of("input.txt"), dir.resolve("copy.txt")); ~~ 需要nio.file及Stream导入和IOException处理。避免将不可信路径直接拼到服务目录。
stream-operations ~~ Stream筛选映射与收集 ~~ filter筛选、map转换，终端操作触发流水线。Stream不能重复使用。 ~~ List<String> names = List.of("Ada", "Bob", "Alice");\nList<String> result = names.stream()\n    .filter(s -> s.startsWith("A"))\n    .map(String::toUpperCase)\n    .toList(); ~~ toList需要Java16及以后，返回不可修改List；并行流不是自动加速开关。
jdbc-query ~~ JDBC参数化查询 ~~ 使用PreparedStatement绑定数据值，依次关闭结果集和语句，连接生命周期由调用方管理。 ~~ try (PreparedStatement ps = connection.prepareStatement(\n        "SELECT name FROM users WHERE id = ?")) {\n    ps.setLong(1, 42L);\n    try (ResultSet rs = ps.executeQuery()) {\n        while (rs.next()) System.out.println(rs.getString("name"));\n    }\n} ~~ 需要java.sql导入、有效Connection、驱动及SQLException处理；不能用参数替代表名。
jdbc-write ~~ JDBC更新与事务 ~~ 关闭自动提交后提交或回滚；确保把连接状态交还连接池。 ~~ connection.setAutoCommit(false);\ntry (PreparedStatement ps = connection.prepareStatement(\n        "UPDATE users SET score = ? WHERE id = ?")) {\n    ps.setInt(1, 90); ps.setLong(2, 42);\n    ps.executeUpdate();\n    connection.commit();\n} catch (SQLException e) {\n    connection.rollback(); throw e;\n} finally {\n    connection.setAutoCommit(true);\n} ~~ 示例要求新取得且原本自动提交的连接，不嵌套已有事务；失败的rollback需保留原始异常。
''','java',version='Java 21；片段需对应导入、方法上下文和异常处理')
    add('JVM','启动与诊断',JVM,'jvm.',r'''
version ~~ 核对Java运行环境 ~~ java和javac可能来自不同安装目录，诊断前确认运行时版本及系统属性。 ~~ java -version\njavac -version\njava -XshowSettings:properties -version ~~ 属性输出含本机路径，分享日志前检查；不要仅根据JAVA_HOME推断实际进程版本。
launch ~~ 启动参数位置与类路径 ~~ JVM参数放主类或-jar之前，其后参数传给程序；Linux类路径用冒号，Windows用分号。 ~~ java -Xms64m -Xmx256m -Dapp.env=dev -jar app.jar\njava -cp 'classes:lib/*' com.example.Main ~~ -jar启动不使用普通-cp指定的用户类路径，依赖须由JAR/启动器配置。
jar ~~ 查看与创建JAR ~~ jar工具来自JDK；--list检查内容，--create打包并可指定入口类。 ~~ jar --list --file app.jar\njar --create --file app.jar --main-class demo.Main -C classes . ~~ classes目录应包含正确包结构；此命令不会自动把第三方依赖一起打包。
javap ~~ javap查看字节码 ~~ javap显示类成员与字节码，-c反汇编，-p包括非公开成员，-v提供详细信息。 ~~ javap -classpath classes -c -p demo.Main\njavap -classpath app.jar -v demo.Main ~~ 字节码反汇编不是恢复原始源码；查看结果应匹配正在运行的构建产物。
''')
    add('算法','基础操作',API,'algorithm.',r'''
stack-ops ~~ 栈的压入弹出与查看 ~~ ArrayDeque的push、pop、peek对应栈顶压入、弹出和查看。 ~~ Deque<Integer> stack = new ArrayDeque<>();\nstack.push(1); stack.push(2);\nInteger top = stack.peek();\nwhile (!stack.isEmpty()) System.out.println(stack.pop()); ~~ 需要java.util导入。pop空栈抛异常；ArrayDeque不允许null。
queue-ops ~~ 队列入队出队 ~~ offer加到尾部，poll取出头部，peek查看头部，组成先进先出队列。 ~~ Queue<Integer> queue = new ArrayDeque<>();\nqueue.offer(1); queue.offer(2);\nInteger first = queue.poll();\nInteger next = queue.peek(); ~~ 需要java.util导入；poll空队列返回null，不要直接拆箱成int。
heap-ops ~~ 优先队列常用操作 ~~ PriorityQueue默认小根堆，offer加入、peek查看最小值、poll删除最小值。 ~~ PriorityQueue<Integer> heap = new PriorityQueue<>();\nheap.offer(3); heap.offer(1); heap.offer(2);\nwhile (!heap.isEmpty()) System.out.println(heap.poll()); ~~ 迭代堆不保证有序；插入删除通常O(log n)，查看根O(1)，按值删除O(n)。
''','java',version='Java 21；java.util数据结构示例')
    return out
