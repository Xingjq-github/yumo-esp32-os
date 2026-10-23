from card import table,JVM
def cards():
    out=[]
    def add(group,text):out.extend(table(text,'JVM',group,JVM,'OpenJDK/HotSpot 21；参数适用性需在目标JDK核验','jvm.','shell'))
    add('运行原理',r'''
bytecode ~~ 字节码与执行 ~~ javac把源码转换为class字节码，JVM加载并执行。HotSpot可解释执行并对热点代码进行JIT编译。 ~~ javap -c -p Example.class ~~ 字节码不是CPU本机指令；不同JDK的class主版本兼容范围不同。
heap ~~ 堆内存 ~~ 堆用于对象和数组分配，由垃圾收集器管理；优化后部分分配可能被消除。 ~~ java -Xms128m -Xmx512m App ~~ 堆上限不等于进程内存上限，还要计入栈、元空间、直接内存等。
stack ~~ 线程栈 ~~ 每个线程有执行栈，栈帧保存局部变量、操作数栈等。过深递归可能StackOverflowError。 ~~ java -Xss512k App ~~ -Xss主要影响平台线程栈；不能把它直接当虚拟线程总栈占用。
pc ~~ 程序计数器 ~~ JVM为线程维护当前执行位置概念，支持线程切换后继续执行；native方法的该值按规范未定义。 ~~ javap -c Example.class ~~ 字节码偏移用于理解控制流，不直接等于CPU物理程序计数器。
metaspace ~~ 元空间 ~~ HotSpot用本地内存保存类元数据，通常称元空间；不是Java堆的一部分。 ~~ jcmd 1234 VM.metaspace ~~ 方法区是规范概念，元空间是HotSpot实现；动态生成类过多可能耗尽元空间。
direct ~~ 直接内存 ~~ DirectByteBuffer等可使用堆外内存，减少部分IO复制但分配和回收方式不同。 ~~ java -XX:MaxDirectMemorySize=64m App ~~ 该限制不覆盖所有本地malloc；进程RSS仍可超过堆与直接内存限制之和。
loading ~~ 类加载阶段 ~~ 加载、连接（验证、准备、解析）、初始化构成类生命周期。解析可能延迟执行。 ~~ java -Xlog:class+load=info App ~~ 静态字段准备阶段的默认值与初始化代码执行结果要区分。
initialization ~~ 类初始化 ~~ 首次主动使用可能触发初始化，JVM协调并发初始化。读取编译期常量不一定初始化声明类。 ~~ java -Xlog:class+init=info App ~~ 初始化失败可导致后续NoClassDefFoundError，需找第一次异常。
classloader ~~ 类加载器身份 ~~ 类身份由类名和定义它的类加载器共同决定。同名class可能无法互相强制转换。 ~~ jcmd 1234 VM.classloaders ~~ 热部署或插件系统要释放线程和静态引用，否则旧加载器无法卸载。
allocation ~~ 对象分配与TLAB ~~ HotSpot通常在堆中分配对象，TLAB为线程提供快速分配区。逃逸分析可能消除某些分配。 ~~ java -Xlog:gc+tlab=debug App ~~ 不应依据一次优化结果承诺对象一定在栈上分配。
reachable ~~ 可达性与GC Roots ~~ 从根集合可达的对象保留；常见根包括活动线程栈引用及某些运行时引用。循环引用本身不会阻止追踪式GC。 ~~ jcmd 1234 GC.class_histogram ~~ 直方图不能直接证明泄漏，需要引用链和随时间变化的证据。
references ~~ 强软弱虚引用 ~~ 强引用保持可达，软引用回收受内存压力策略影响，弱引用更易被清理，虚引用配合引用队列观察清理。 ~~ javap java.lang.ref.ReferenceQueue ~~ 不把SoftReference当可靠缓存容量策略；资源及时关闭不应依赖GC。
gc-copy ~~ 复制与整理 ~~ 复制把存活对象搬到新区域，整理减少碎片，清扫回收空闲块。不同收集器组合使用这些机制。 ~~ java -Xlog:gc=info App ~~ 复制成本与存活量相关，空间和停顿权衡不能只看算法名称。
generational ~~ 分代假设 ~~ 许多对象很快死亡，分代收集利用这一统计规律降低重复扫描开销。具体代和区域设计取决于收集器。 ~~ java -Xlog:gc+heap=info App ~~ 分代不是所有JVM/所有收集器的固定布局；用日志确认实际配置。
safepoint ~~ 安全点与停顿 ~~ JVM在适当位置协调线程完成部分全局操作。应用暂停既可能来自GC，也可能来自其他安全点操作。 ~~ java -Xlog:safepoint=info App ~~ 只看GC耗时会漏掉到达安全点的等待和其他停顿来源。
''')
    add('常用参数',r'''
xms ~~ 设置初始堆 Xms ~~ -Xms指定初始堆容量，单位可用k/m/g。是否保持较大已提交内存受收集器及运行环境影响。 ~~ java -Xms128m -Xmx512m App ~~ 默认值由运行环境和JDK计算，不应把某台机器默认值推广到所有设备。
xmx ~~ 设置最大堆 Xmx ~~ -Xmx限制Java堆最大容量，需给线程栈、元空间、直接内存和本地库留下空间。 ~~ java -Xmx512m App ~~ 容器内把全部内存都分给堆容易被系统杀死，来不及生成Java堆转储。
xss ~~ 设置平台线程栈 Xss ~~ -Xss指定平台线程栈大小，过小会降低递归深度，过大会增加大量线程的地址空间或内存压力。 ~~ java -Xss512k App ~~ 最小值和对齐要求取决于平台；不要盲目缩小来容纳无限线程。
max-metaspace ~~ 限制元空间 ~~ MaxMetaspaceSize限制类元数据使用的本地内存规模，超限可能触发GC并最终OOM。 ~~ java -XX:MaxMetaspaceSize=128m App ~~ 限制过小会导致频繁类卸载相关GC；先检查类加载器泄漏。
max-direct ~~ 限制直接缓冲区 ~~ MaxDirectMemorySize限制NIO直接缓冲区可分配总量，单位支持k/m/g。 ~~ java -XX:MaxDirectMemorySize=64m App ~~ 不涵盖JNI和全部第三方本地库分配，仍需观察进程总内存。
serial ~~ Serial GC ~~ 单线程收集器，适合小堆、简单环境或资源受限场景，回收时会暂停应用线程。 ~~ java -XX:+UseSerialGC App ~~ 是否更省资源需按真实工作负载测量；长暂停可能不可接受。
parallel ~~ Parallel GC ~~ 多线程吞吐优先收集器，通过并行回收降低回收耗时，但主要回收阶段仍暂停应用。 ~~ java -XX:+UseParallelGC App ~~ 追求吞吐不等于低延迟；线程数和容器CPU配额会影响效果。
g1 ~~ G1 GC ~~ G1按region管理堆，试图根据停顿目标组织回收。MaxGCPauseMillis是目标，不是硬实时保证。 ~~ java -XX:+UseG1GC -XX:MaxGCPauseMillis=100 App ~~ 过小停顿目标可能增加回收频率和CPU开销，先用默认配置获取基线。
zgc ~~ ZGC 与Java21分代模式 ~~ ZGC把大量回收工作并发化；Java21可用ZGenerational选择分代ZGC。 ~~ java -XX:+UseZGC -XX:+ZGenerational App ~~ 后续JDK的默认模式和选项可能变化。本条仅针对HotSpot21，不盲目跨版本复制。
gc-log ~~ 统一GC日志 ~~ -Xlog统一控制日志标签、级别和输出，轮转限制磁盘占用。 ~~ java -Xlog:gc*:file=gc.log:time,uptime,level,tags:filecount=3,filesize=10m App ~~ 日志路径需可写；开启过细日志会增加开销，诊断后恢复。
oom-dump ~~ OOM生成堆转储 ~~ HeapDumpOnOutOfMemoryError尝试在OOM时输出堆快照，HeapDumpPath指定位置。 ~~ java -XX:+HeapDumpOnOutOfMemoryError -XX:HeapDumpPath=/data App ~~ 预留磁盘，转储可能含密码等敏感值；进程被系统强杀时未必生成。
nmt ~~ Native Memory Tracking ~~ NMT在HotSpot启动时启用，追踪JVM部分本地内存分类，用于比较基线变化。 ~~ java -XX:NativeMemoryTracking=summary App\njcmd 1234 VM.native_memory summary ~~ NMT有额外开销且不跟踪所有用户本地分配，不能等同于完整进程RSS。
container ~~ 容器资源识别 ~~ HotSpot可根据容器限制计算资源默认值，可用MaxRAMPercentage控制最大堆占可识别内存比例。 ~~ java -XX:MaxRAMPercentage=50 App ~~ JVM版本和cgroup支持影响识别；用容器内实际结果核对，不只看宿主机内存。
print-flags ~~ 查看有效参数 ~~ PrintFlagsFinal输出有效参数值，jcmd VM.flags查看目标进程参数。 ~~ java -XX:+PrintFlagsFinal -version\njcmd 1234 VM.flags ~~ 参数存在不代表可在线修改；内部选项跨JDK可能废弃或删除。
''')
    add('基础排障',r'''
jcmd ~~ jcmd 诊断入口 ~~ jcmd按进程提供命令列表和诊断功能，先help再执行，优先使用与目标JDK兼容的工具。 ~~ jcmd -l\njcmd 1234 help\njcmd 1234 VM.version ~~ 通常要求相同用户和可用attach权限；不要对未知进程执行有停顿的命令。
thread-dump ~~ 线程转储 ~~ Thread.print查看线程状态、堆栈和锁信息，连续采样比单次快照更有判断力。 ~~ jcmd 1234 Thread.print -l ~~ RUNNABLE不一定代表正在使用CPU；需要结合系统线程CPU数据。
heap-dump ~~ 手动堆转储 ~~ GC.heap_dump输出对象快照，用离线分析工具追踪大对象和GC Roots引用链。 ~~ jcmd 1234 GC.heap_dump /data/heap.hprof ~~ 可能暂停进程且文件很大；执行前确认空间和影响窗口。
histogram ~~ 对象直方图 ~~ GC.class_histogram按类型汇总对象数量和字节数，可初筛增长趋势。 ~~ jcmd 1234 GC.class_histogram ~~ 该操作可能触发明显停顿；多个快照比较仍不能单独确定泄漏根因。
jstat ~~ jstat 采样 ~~ jstat提供JVM统计计数，短周期观测堆和回收变化。不同收集器列含义需结合文档理解。 ~~ jstat -gcutil 1234 1000 5 ~~ 工具输出格式不是稳定编程接口，生产自动化不要只依赖列位置猜测。
jfr ~~ JFR 运行记录 ~~ Flight Recorder采集事件用于定位CPU、锁、分配及IO问题。按需要选择设置和时长。 ~~ jcmd 1234 JFR.start name=diag settings=profile duration=60s filename=/data/diag.jfr ~~ profile比default通常更详细且开销更高；记录文件也可能包含业务信息。
oom-heap ~~ 堆OOM排查 ~~ Java heap space可能是容量不足，也可能是保留对象持续增长。结合堆使用趋势、GC日志和引用链判断。 ~~ jcmd 1234 GC.heap_info ~~ 直接调大Xmx可能掩盖泄漏；先确认输入规模和对象生命周期。
oom-native ~~ 本地内存或线程不足 ~~ unable to create native thread可能来自系统线程限制、内存或地址空间，不一定是Java堆。 ~~ jcmd 1234 VM.native_memory summary\njcmd 1234 Thread.print ~~ NMT必须预先启用；同时检查容器PID限制与线程池上限。
cpu-high ~~ CPU高排查 ~~ 先确定高CPU进程和线程，再把系统线程ID转换成十六进制与转储nid关联，重复采样确认热点。 ~~ top -H -p 1234\njcmd 1234 Thread.print ~~ Linux线程ID关联主要针对平台线程，虚拟线程需结合JFR等支持工具。
deadlock ~~ 死锁排查 ~~ 多个线程循环等待锁会死锁。线程转储可能报告监视器或ownable synchronizer死锁。 ~~ jcmd 1234 Thread.print -l ~~ 报告没有死锁不代表没有业务层等待循环；统一锁顺序并避免持锁等待外部结果。
gc-frequent ~~ 频繁GC排查 ~~ 分清分配过快、存活集过大、堆过小和参数不当。关注单位时间分配率及回收后基线。 ~~ java -Xlog:gc*,gc+heap=debug:file=gc.log:time,uptime App ~~ 单看GC次数无法衡量问题；同时观察暂停、吞吐和用户延迟。
''')
    return out
