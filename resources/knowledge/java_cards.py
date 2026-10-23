from card import table,JLS,API
def cards():
    out=[]
    def add(group,text,source=JLS):out.extend(table(text,'Java',group,source,'Java 21；示例片段，按条目补充上下文','java.'))
    add('基础语法',r'''
byte ~~ byte 字节整数 ~~ 有符号8位整数，范围-128到127。算术运算通常先提升为int。 ~~ byte value = 127;\nint next = value + 1; ~~ byte不是无符号字节。读取二进制时用 value & 0xff 转为0到255。
short ~~ short 短整数 ~~ 有符号16位整数，范围-32768到32767，适合明确限定的二进制字段。 ~~ short port = 3000;\nint sum = port + 1; ~~ 加法结果不是short；强制转回时要检查溢出。
int ~~ int 整数 ~~ 有符号32位整数。溢出会保留低32位，不自动抛异常。 ~~ int n = Integer.MAX_VALUE;\nint checked = Math.addExact(n, 1); ~~ 示例的addExact会抛ArithmeticException；普通 n+1 会溢出。
long ~~ long 长整数 ~~ 有符号64位整数。大整数字面量使用L后缀，乘法需先提升操作数。 ~~ long ms = 24L * 60 * 60 * 1000; ~~ 先用int乘出溢出值，再赋给long不能补救。
float ~~ float 单精度 ~~ IEEE 754二进制32位浮点数，通常约6到7位十进制有效数字。 ~~ float ratio = 0.1f;\nboolean finite = Float.isFinite(ratio); ~~ 不是所有十进制小数都能精确表示；货币一般使用BigDecimal。
double ~~ double 双精度 ~~ IEEE 754二进制64位浮点数，通常约15到16位十进制有效数字。 ~~ double value = 0.1 + 0.2;\nboolean close = Math.abs(value - 0.3) < 1e-12; ~~ 误差阈值应按业务尺度确定；NaN不等于自身，用isNaN检测。
char ~~ char 字符单元 ~~ 无符号16位UTF-16代码单元，范围0到65535。一个char不一定对应完整字符。 ~~ char unit = 'A';\nint cp = "\uD83D\uDE00".codePointAt(0); ~~ 补充字符占两个char，按字符处理应考虑codePoints及字素边界。
boolean ~~ boolean 布尔值 ~~ 只有true和false，条件表达式不接受数字。语言规范没有承诺其对象或数组元素统一占几个字节。 ~~ boolean enabled = true;\nif (enabled) System.out.println("on"); ~~ 不要把布尔逻辑值与JVM具体存储布局混为一谈。
literals ~~ 字面量与分隔符 ~~ 整数支持十进制、十六进制、二进制及八进制；下划线提高数字可读性。 ~~ int mask = 0b1111_0000;\nlong size = 1_000_000L; ~~ 前导0是八进制；下划线不能位于数字开头或结尾。
variables ~~ 变量与局部类型推断 ~~ var自Java10用于有初始化值的局部变量，类型在编译时确定。 ~~ var names = new java.util.ArrayList<String>();\nnames.add("Ada"); ~~ var不是动态类型；不能用于字段，也不能只用null推断类型。
defaults ~~ 默认值与确定赋值 ~~ 实例/静态字段和数组元素有默认值；局部变量必须在读取前完成确定赋值。 ~~ int[] values = new int[2];\nint local;\nlocal = 3;\nSystem.out.println(values[0] + local); ~~ 数值字段默认0，boolean默认false，引用默认null；局部变量不套用这些规则。
scope ~~ 作用域与生命周期 ~~ 块内声明的局部变量只能在其作用域使用。变量作用域与对象可达时间是不同概念。 ~~ for (int i = 0; i < 3; i++) {\n    int square = i * i;\n    System.out.println(square);\n} ~~ 离开块不会立即回收对象；仍被其他引用持有的对象继续存活。
constant ~~ 常量与final ~~ final变量只能赋值一次；引用final不代表指向的对象不可变。 ~~ final java.util.List<String> xs = new java.util.ArrayList<>();\nxs.add("allowed"); ~~ 只有符合规范条件的基本类型/String常量表达式才是编译期常量。
boxing ~~ 装箱与拆箱 ~~ 基本类型与包装类可自动转换；拆箱会读取包装对象的值。 ~~ Integer boxed = 42;\nint value = boxed; ~~ null拆箱会抛NullPointerException；包装类用equals比较值。
conversion ~~ 扩大与缩小转换 ~~ 扩大数值转换通常自动发生，缩小转换需要强制转换且可能丢失精度或高位。 ~~ long large = 1000;\nbyte low = (byte) large; ~~ int转float、long转double也可能损失精度，扩大并不等于无损。
promotion ~~ 数值提升 ~~ byte、short、char参与多数整数运算时会提升为int，混合运算按数值类型规则提升。 ~~ byte a = 1;\nbyte b = 2;\nint sum = a + b; ~~ a += b 包含隐式转换，和 a = a + b 的类型检查不完全一样。
references ~~ 引用与null ~~ 引用保存对对象的访问关系，可以为null。对象赋值通常只是复制引用。 ~~ String text = null;\nString safe = java.util.Objects.requireNonNullElse(text, ""); ~~ 不要把Java引用当成可进行地址算术的裸指针。
pass-by-value ~~ Java 按值传递 ~~ 方法参数总是按值传递；传入对象时复制的是引用值。修改对象可被看见，重新赋参不会改调用方变量。 ~~ void append(StringBuilder s) {\n    s.append("x");\n    s = new StringBuilder("y");\n} ~~ 示例是类中的方法片段。重新给s赋值不替换调用方原引用。
packages ~~ 包与导入 ~~ package组织命名空间，import简化类型名称。导入不会复制代码或自动导入子包。 ~~ import java.util.List;\n// A compilation unit may now use List. ~~ java.lang自动导入；import java.util.* 不包含java.util.concurrent。
equality ~~ 身份与值相等 ~~ 引用类型的==判断是否指向同一对象；equals的值语义由类实现。 ~~ String a = new String("x");\nString b = new String("x");\nboolean sameValue = a.equals(b); ~~ String的驻留可能掩盖==误用；可空比较使用Objects.equals。
arithmetic ~~ 算术运算符 ~~ 整数除法向0截断；余数符号跟随被除数。除数为0时整数除法抛异常。 ~~ int q = -7 / 3;\nint r = -7 % 3; ~~ 浮点除0可能得到无穷或NaN；不能照搬整数异常规则。
comparison ~~ 比较运算符 ~~ 比较数值得到boolean。浮点比较须考虑NaN及舍入误差。 ~~ boolean less = 3 < 5;\nint order = Integer.compare(3, 5); ~~ 排序比较器不要用a-b代替compare，减法可能溢出。
logic ~~ 短路逻辑 ~~ &&在左侧为false时跳过右侧，逻辑或在左侧为true时跳过右侧。 ~~ String s = null;\nboolean valid = s != null && !s.isEmpty(); ~~ 不要依赖被短路表达式中的副作用；按位与不会短路。
bitwise ~~ 位运算 ~~ 按位与、或、异或、取反用于标志位和二进制处理。异或同一掩码两次恢复原值。 ~~ int flags = 0b1010;\nboolean set = (flags & 0b0010) != 0;\nflags ^= 0b0010; ~~ 运算优先级容易误读；位操作子表达式明确加括号。
shift ~~ 移位运算 ~~ 左移补0，带符号右移补符号位，无符号右移补0。int只使用距离低5位，long使用低6位。 ~~ int sign = -1 >> 1;\nint positive = -1 >>> 1; ~~ 移32位的int等于移0位；移位并不自动检查溢出。
assignment ~~ 赋值与自增 ~~ 赋值表达式有值；前缀自增先改后取值，后缀先取值后改。 ~~ int i = 1;\nint before = i++;\nint after = ++i; ~~ 避免在一个表达式里多次修改同一变量，可读性和维护成本很差。
ternary ~~ 条件表达式 ~~ condition ? yes : no只计算一个分支，分支类型依据条件表达式规则合并。 ~~ int min = 3 < 8 ? 3 : 8; ~~ 基本类型与包装类混合可能触发拆箱，null分支要特别检查。
instanceof ~~ instanceof 与模式变量 ~~ Java21中可用模式变量在类型检查成功后直接访问对象。 ~~ Object x = "hello";\nif (x instanceof String s) {\n    System.out.println(s.length());\n} ~~ null instanceof任意引用类型为false；模式变量仅在匹配有效的作用域可用。
if ~~ if 条件分支 ~~ 条件必须是boolean，else与最近尚未匹配的if配对。 ~~ int score = 80;\nif (score >= 60) {\n    System.out.println("pass");\n} else {\n    System.out.println("retry");\n} ~~ 即使单行也使用花括号，避免追加语句时改变逻辑。
switch ~~ switch 表达式 ~~ 箭头分支无传统贯穿，可返回结果；块分支用yield返回值。switch表达式自Java14正式提供。 ~~ int n = 2;\nString size = switch (n) {\n    case 1 -> "one";\n    case 2 -> "two";\n    default -> "many";\n}; ~~ switch表达式必须穷尽。Java21的模式switch与旧版本语法不同。
for ~~ for 循环 ~~ 适用于次数或索引可控的循环，依次执行初始化、条件、循环体、更新。 ~~ for (int i = 0; i < 3; i++) {\n    System.out.println(i);\n} ~~ 索引边界通常是i < length，不是i <= length。
foreach ~~ 增强for ~~ 遍历数组或Iterable，隐藏迭代器细节。 ~~ for (String name : java.util.List.of("A", "B")) {\n    System.out.println(name);\n} ~~ 循环变量重新赋值不修改数组元素；结构性修改集合请使用合适的迭代器方法。
while ~~ while 循环 ~~ 先检查条件再执行循环体，循环体可能一次也不执行。 ~~ int n = 3;\nwhile (n > 0) {\n    System.out.println(n--);\n} ~~ 确保条件能够变化；忙等会持续消耗CPU。
do-while ~~ do while 循环 ~~ 先执行循环体再检查条件，因此至少执行一次。 ~~ int n = 0;\ndo {\n    n++;\n} while (n < 3); ~~ 末尾while后必须有分号；不适合必须先验证输入才能执行的操作。
break ~~ break 与标签 ~~ break退出最近循环或switch，可用标签退出指定外层循环。 ~~ outer: for (int i = 0; i < 3; i++) {\n    for (int j = 0; j < 3; j++) {\n        if (i + j == 2) break outer;\n    }\n} ~~ 标签不是任意跳转，不能用于跳入代码块。
continue ~~ continue 跳过本轮 ~~ continue跳到循环下一次迭代；for会先执行更新表达式。 ~~ for (int i = 0; i < 5; i++) {\n    if (i % 2 == 0) continue;\n    System.out.println(i);\n} ~~ while中把更新放在continue之后可能导致死循环。
return ~~ return 返回 ~~ void方法可直接return，有返回类型的方法需返回兼容值。return会结束当前方法。 ~~ int absSmall(int n) {\n    if (n >= 0) return n;\n    return -n;\n} ~~ 示例对Integer.MIN_VALUE溢出；返回前仍会执行相关finally。
array-create ~~ 数组初始化 ~~ 数组长度创建后固定，元素有默认值；引用数组元素初始为null。 ~~ int[] values = {3, 1, 2};\nString[] names = new String[2]; ~~ names[0]还不是String对象，直接调用方法会空指针。
array-index ~~ 数组边界 ~~ 有效索引为0到length-1。越界访问会抛ArrayIndexOutOfBoundsException。 ~~ int[] a = {10, 20};\nint last = a[a.length - 1]; ~~ 空数组没有最后一个元素，访问前检查length。
array-copy ~~ 数组复制 ~~ Arrays.copyOf创建新数组；System.arraycopy支持范围复制，并正确处理源目标重叠。 ~~ int[] a = {1, 2, 3};\nint[] b = java.util.Arrays.copyOf(a, 5); ~~ 对象数组只是浅复制，元素仍引用同一对象。
array-sort ~~ 数组排序与查找 ~~ Arrays.sort排序后才能使用binarySearch按相同顺序查找。 ~~ int[] a = {3, 1, 2};\njava.util.Arrays.sort(a);\nint p = java.util.Arrays.binarySearch(a, 2); ~~ 查找返回负值编码插入位置，并不是总返回-1。
array-deep ~~ 多维数组 ~~ Java的多维数组是数组的数组，各行长度可以不同。 ~~ int[][] grid = {{1, 2}, {3}};\nint n = grid[1].length; ~~ grid.length是行数；不要假定每行长度相同或都非null。
varargs ~~ 可变参数 ~~ 可变参数在方法内是数组，必须放在参数列表最后。 ~~ int total(int... values) {\n    int n = 0;\n    for (int v : values) n += v;\n    return n;\n} ~~ 调用时可传数组；泛型可变参数需要留意堆污染警告。
array-equals ~~ 数组内容相等 ~~ 数组未重写Object.equals，内容比较用Arrays.equals；嵌套对象数组用deepEquals。 ~~ boolean same = java.util.Arrays.equals(new int[]{1}, new int[]{1}); ~~ toString同样默认不显示元素，使用Arrays.toString或deepToString。
''')
    add('面向对象',r'''
class ~~ 类与对象 ~~ 类定义状态和行为，对象是运行时实例。把保持约束的行为放在对象内部。 ~~ class Counter {\n    private int value;\n    void increment() { value++; }\n    int value() { return value; }\n} ~~ 随意暴露可变字段会让调用方绕过类的约束。
constructor ~~ 构造方法 ~~ 构造方法负责初始化，名称与类一致且没有返回类型。没有声明任何构造方法时才可能生成默认构造方法。 ~~ class User {\n    final String name;\n    User(String name) {\n        this.name = java.util.Objects.requireNonNull(name);\n    }\n} ~~ 不要在构造方法中泄漏this或调用依赖子类状态的可重写方法。
inheritance ~~ 继承 ~~ 类只能直接继承一个类，可实现多个接口。继承表达可替代的类型关系。 ~~ class Animal { void speak() {} }\nclass Dog extends Animal {\n    @Override void speak() { System.out.println("woof"); }\n} ~~ 仅为代码复用而建立继承链容易破坏替换原则，优先考虑组合。
interface ~~ 接口 ~~ 接口定义契约，可有抽象、default、static及private方法。字段隐式为public static final。 ~~ interface Printer { void print(String s); }\nclass Console implements Printer {\n    public void print(String s) { System.out.println(s); }\n} ~~ 实现方法不能降低可见性；default冲突可能需要显式选择实现。
polymorphism ~~ 多态 ~~ 可重写实例方法根据实际对象类型选择实现，引用类型决定编译时可用成员。 ~~ java.util.List<String> names = new java.util.ArrayList<>();\nnames.add("A"); ~~ 字段和static方法不按同样的动态分派规则选择。
overload ~~ 方法重载 ~~ 同名方法参数列表不同，编译时根据参数静态类型和适用性选择。 ~~ void print(int n) {}\nvoid print(String s) {} ~~ 仅返回类型不同不能重载；null遇到无继承关系的引用重载可能歧义。
override ~~ 方法重写 ~~ 子类替换父类实例方法实现，使用Override让编译器检查签名。 ~~ class Base { Number value() { return 1; } }\nclass Child extends Base {\n    @Override Integer value() { return 2; }\n} ~~ 返回值允许协变；受检异常不能随意扩大，可见性不能缩小。
static ~~ static 成员 ~~ 静态成员属于类，静态方法没有this。共享可变静态状态需要同步和生命周期管理。 ~~ class Limits {\n    static final int MAX = 100;\n    static int clamp(int n) { return Math.min(n, MAX); }\n} ~~ 用类名访问静态成员；static不自动保证线程安全。
final ~~ final 类和方法 ~~ final类不能被继承，final方法不能被重写。可用来固定不可变类型或关键约束。 ~~ final class Id {\n    private final long value;\n    Id(long value) { this.value = value; }\n    long value() { return value; }\n} ~~ final字段中的可变集合仍可修改；不可变类还需要防御性复制。
abstract ~~ 抽象类 ~~ 抽象类不能直接实例化，可同时提供公共状态、已实现方法和抽象方法。 ~~ abstract class Shape { abstract double area(); }\nclass Square extends Shape {\n    double side = 2;\n    double area() { return side * side; }\n} ~~ 公共状态耦合会随继承加深；纯能力契约通常用接口。
this-super ~~ this 与super ~~ this指向当前实例，super用于访问父类成员或构造方法。Java21的显式构造调用位于构造体首句。 ~~ class Base { Base(int n) {} }\nclass Child extends Base {\n    Child() { super(1); }\n} ~~ 本条采用Java21规则，不把后续版本构造语法套入。
access ~~ 访问控制 ~~ public公开，protected同包及受限制的子类访问，缺省仅同包，private限定在规范定义的封闭范围内。 ~~ class Account {\n    private int balance;\n    public int balance() { return balance; }\n} ~~ 跨包子类访问protected实例成员有限制，不是对子类所有引用无限开放。
composition ~~ 组合与委托 ~~ 对象持有协作者并转发工作，便于替换实现而不占用继承关系。 ~~ class Report {\n    private final java.io.PrintStream out;\n    Report(java.io.PrintStream out) { this.out = out; }\n    void show(String s) { out.println(s); }\n} ~~ 明确协作者由谁关闭，注入共享资源时不要随意释放它。
enum ~~ 枚举 ~~ 枚举为有限实例集合，可定义字段和方法，适合状态、策略键或模式选择。 ~~ enum State { IDLE, RUNNING, CLOSED }\nState s = State.IDLE; ~~ ordinal随声明顺序变化，持久化时优先保存稳定名称或业务编码。
record ~~ record 数据载体 ~~ Java16正式支持record，自动生成访问器、构造方法、equals、hashCode和toString。 ~~ record Point(int x, int y) {}\nPoint p = new Point(2, 3); ~~ record只保证组件引用不可重新赋值；可变组件仍需防御性复制。
sealed ~~ sealed 封闭层次 ~~ Java17正式支持sealed限制允许继承者，子类型需声明final、sealed或non-sealed。 ~~ sealed interface Result permits Ok, Fail {}\nrecord Ok(String value) implements Result {}\nrecord Fail(String message) implements Result {} ~~ permitted类型的位置受模块或包规则约束；不是访问控制的替代。
equals-hashcode ~~ equals 与hashCode 契约 ~~ equals相等的对象必须拥有相同hashCode。集合键在使用期间应保持相等性相关字段稳定。 ~~ record Key(String tenant, long id) {}\nvar map = new java.util.HashMap<Key, String>();\nmap.put(new Key("A", 1), "value"); ~~ 修改可变键后原桶位置不自动变化，可能导致查找失败。
''')
    add('常用 API',r'''
string ~~ String 不可变文本 ~~ String不可变，多次修改产生新值。length返回UTF-16代码单元数。 ~~ String s = "  Hello  ";\nString clean = s.strip().toLowerCase(java.util.Locale.ROOT); ~~ 忽略返回值不会改变原字符串；大小写转换应明确Locale。
stringbuilder ~~ StringBuilder 拼接 ~~ 单线程中循环拼接优先使用可变缓冲区，最终调用toString。 ~~ StringBuilder b = new StringBuilder();\nfor (int i = 0; i < 3; i++) b.append(i);\nString text = b.toString(); ~~ StringBuilder不保证线程安全，不要作为共享临时缓冲区。
substring ~~ 截取与分割 ~~ substring起点包含、终点不包含。split参数是正则，保留尾部空项需要负limit。 ~~ String[] cells = "a,,".split(",", -1);\nString part = "abcd".substring(1, 3); ~~ 按char边界截取可能截断代理对；字面点的正则需转义。
format ~~ 格式化文本 ~~ String.format和formatted生成格式化结果；数字小数点随Locale变化。 ~~ String s = String.format(java.util.Locale.ROOT, "%04d %.2f", 7, 1.5); ~~ 日志高频格式化有分配成本；格式字符串与参数类型必须匹配。
integer-parse ~~ 整数解析 ~~ parseInt解析十进制整数，也可指定进制；非法输入或越界抛NumberFormatException。 ~~ int n = Integer.parseInt("ff", 16);\nString hex = Integer.toHexString(n); ~~ 先确认输入范围和符号规则，不要吞掉错误后默认当0。
bigdecimal ~~ BigDecimal 十进制 ~~ 用字符串构造精确十进制，运算返回新对象。除不尽时需指定舍入规则。 ~~ var x = new java.math.BigDecimal("0.1");\nvar y = x.divide(new java.math.BigDecimal("3"), 2, java.math.RoundingMode.HALF_UP); ~~ new BigDecimal(0.1)保留二进制近似；equals包含scale，数值比较用compareTo。
biginteger ~~ BigInteger 大整数 ~~ BigInteger支持任意精度整数运算，精度由可用内存限制。 ~~ var n = new java.math.BigInteger("12345678901234567890");\nvar square = n.multiply(n); ~~ 大数运算不是固定时间；解析外部超长数字前限制长度。
math ~~ Math 常用计算 ~~ Math提供取整、绝对值、最值和精确整数运算检测。 ~~ long rounded = Math.round(2.5);\nint sum = Math.addExact(1, 2); ~~ Math.abs(Integer.MIN_VALUE)仍为负数；精确API会对溢出抛异常。
random ~~ Random 与安全随机 ~~ 普通伪随机用于模拟；令牌等安全用途使用SecureRandom并编码足够熵。 ~~ var r = new java.security.SecureRandom();\nbyte[] bytes = new byte[16];\nr.nextBytes(bytes); ~~ 时间戳和自定义种子不是安全令牌；有界nextInt上界不包含。
localdate ~~ LocalDate 日期 ~~ LocalDate表示不带时区和时间的日历日期，用于生日或账期。 ~~ var d = java.time.LocalDate.of(2024, 2, 29);\nvar next = d.plusYears(1); ~~ 日期加减会调整无效日期；不要用固定24小时秒数代表所有日历日。
instant ~~ Instant 时间点 ~~ Instant表示时间线上的点，与地区时区分开保存。 ~~ var t = java.time.Instant.parse("2024-01-01T00:00:00Z");\nlong epoch = t.toEpochMilli(); ~~ 显示本地时间必须指定ZoneId；System.nanoTime只用于经过时长。
zonedtime ~~ 时区与夏令时 ~~ ZonedDateTime结合日期时间与时区规则，处理夏令时缺口和重叠。 ~~ var zone = java.time.ZoneId.of("Asia/Shanghai");\nvar time = java.time.Instant.now().atZone(zone); ~~ 固定偏移不等于地区时区，历史规则可能不同。
formatter ~~ 日期格式化 ~~ DateTimeFormatter不可变且线程安全，模式中u为年，M为月，m为分钟。 ~~ var f = java.time.format.DateTimeFormatter.ofPattern("uuuu-MM-dd");\nString s = java.time.LocalDate.of(2024, 1, 2).format(f); ~~ YYYY是基于周的年，不能当普通日历年使用。
duration ~~ Duration 与Period ~~ Duration描述秒和纳秒时长；Period描述年、月、日的日历差。 ~~ var duration = java.time.Duration.ofMinutes(5);\nvar period = java.time.Period.ofMonths(1); ~~ 跨夏令时的一天不一定是24小时，选用类型取决于业务含义。
regex ~~ 正则表达式 ~~ Pattern可复用编译后的正则，Matcher维护一次匹配状态。matches匹配整体，find搜索片段。 ~~ var p = java.util.regex.Pattern.compile("[0-9]+");\nvar m = p.matcher("id=42");\nboolean found = m.find(); ~~ 外部输入遇到灾难性回溯可能很慢；避免嵌套不受限量词。
objects ~~ Objects 空值工具 ~~ Objects.requireNonNull用于快速声明契约，equals支持任一参数为空。 ~~ String name = java.util.Objects.requireNonNull("A", "name");\nboolean eq = java.util.Objects.equals(null, name); ~~ 检查失败应提供字段含义；不要用工具调用掩盖可空设计。
optional ~~ Optional 可选结果 ~~ Optional显式表达可能缺失的返回值，可用map、filter、orElseGet组合处理。 ~~ var value = java.util.Optional.ofNullable("x");\nString s = value.map(String::trim).orElseGet(() -> "default"); ~~ orElse参数会立即计算；不宜无差别用于实体字段和所有参数。
uuid ~~ UUID 标识 ~~ randomUUID生成随机UUID；常用作跨系统标识，展示采用标准字符串格式。 ~~ String id = java.util.UUID.randomUUID().toString(); ~~ UUID不等于授权凭据，不要仅因标识难猜就取消访问控制。
''',API)
    add('集合与泛型',r'''
list ~~ List 有序集合 ~~ List保留位置和重复值。选实现时区分随机访问、修改频率和线程共享。 ~~ java.util.List<String> xs = new java.util.ArrayList<>();\nxs.add("A");\nxs.add("A"); ~~ List.of创建不可修改集合并拒绝null；接口本身不保证线程安全。
arraylist ~~ ArrayList 动态数组 ~~ 随机访问通常O(1)，尾部追加摊还O(1)，中间插删需要移动元素。 ~~ var xs = new java.util.ArrayList<Integer>(100);\nxs.add(1);\nint n = xs.get(0); ~~ 初始容量不是元素个数；扩容可能带来分配和复制。
linkedlist ~~ LinkedList 链表 ~~ 双向链表适合已定位节点附近的插删，按索引访问需遍历。 ~~ java.util.Deque<String> q = new java.util.LinkedList<>();\nq.addLast("A");\nString s = q.removeFirst(); ~~ 插入O(1)通常不包括查找位置；多数队列使用ArrayDeque更紧凑。
set ~~ Set 去重集合 ~~ Set不允许按equals判定重复的元素，具体实现决定顺序和性能。 ~~ java.util.Set<String> set = new java.util.HashSet<>();\nset.add("A");\nset.add("A"); ~~ 哈希集合需要正确的equals/hashCode，不能依赖迭代顺序。
hashset ~~ HashSet 与顺序集合 ~~ HashSet基于哈希，LinkedHashSet通常保留插入顺序，TreeSet按比较器排序。 ~~ var xs = new java.util.LinkedHashSet<String>();\nxs.add("B");\nxs.add("A"); ~~ TreeSet按比较结果0判重；比较器和equals不一致会造成意外。
map ~~ Map 键值映射 ~~ 每个键最多映射一个值，重复put通常替换旧值。 ~~ java.util.Map<String, Integer> ages = new java.util.HashMap<>();\nages.put("Ada", 20);\nint n = ages.getOrDefault("Bob", 0); ~~ get返回null可能代表键不存在或值为null，要用containsKey区分。
hashmap ~~ HashMap 查找 ~~ 良好哈希分布下基本操作平均O(1)，允许null键和值但不保证遍历顺序。 ~~ var map = new java.util.HashMap<String, Integer>();\nmap.merge("A", 1, Integer::sum); ~~ 不同JDK实现细节不是接口契约；并发写需使用合适的并发集合。
treemap ~~ TreeMap 排序键 ~~ 红黑树按自然顺序或比较器组织键，查找插入删除为O(log n)。 ~~ var map = new java.util.TreeMap<Integer, String>();\nmap.put(10, "A");\nvar entry = map.ceilingEntry(8); ~~ 自然排序通常拒绝null键；比较结果0被视为同一键。
queue ~~ Queue 队列 ~~ offer、poll、peek用特殊返回值表达失败或空；add、remove、element倾向抛异常。 ~~ java.util.Queue<String> q = new java.util.ArrayDeque<>();\nq.offer("A");\nString s = q.poll(); ~~ ArrayDeque拒绝null；空返回null与真实null元素不能混淆。
deque ~~ Deque 双端队列 ~~ 双端队列可实现栈或FIFO队列，推荐替代旧Stack类型。 ~~ java.util.Deque<Integer> stack = new java.util.ArrayDeque<>();\nstack.push(1);\nint n = stack.pop(); ~~ pop遇空会抛异常，若预期可空用pollFirst。
priorityqueue ~~ PriorityQueue 优先队列 ~~ 堆顶是比较器定义的最小元素，入队出队O(log n)，查看堆顶O(1)。 ~~ var q = new java.util.PriorityQueue<Integer>();\nq.add(3); q.add(1);\nint first = q.remove(); ~~ 迭代器不保证按优先级排序；有序输出要重复poll。
iterator ~~ Iterator 遍历删除 ~~ 使用迭代器自己的remove删除当前元素，避免绕过遍历器修改结构。 ~~ var xs = new java.util.ArrayList<>(java.util.List.of(1, 2));\nfor (var it = xs.iterator(); it.hasNext();) {\n    if (it.next() == 1) it.remove();\n} ~~ fail-fast只是尽力检测错误，不是线程安全保证。
comparator ~~ Comparator 比较器 ~~ 比较器需满足反对称、传递等契约，可按多个字段连接排序。 ~~ var order = java.util.Comparator.comparingInt(String::length).thenComparing(java.util.Comparator.naturalOrder()); ~~ 不一致比较器可能使排序抛异常或破坏有序集合行为。
immutable-list ~~ 不可修改与不可变 ~~ unmodifiableList通常是只读视图，List.copyOf创建不可修改快照；元素本身仍可能可变。 ~~ var source = new java.util.ArrayList<>(java.util.List.of("A"));\nvar snapshot = java.util.List.copyOf(source); ~~ 浅不可修改不等于深不可变；copyOf拒绝null元素。
concurrent-map ~~ ConcurrentHashMap ~~ 并发Map支持线程安全的单项操作和原子组合方法，不允许null键和值。 ~~ var counts = new java.util.concurrent.ConcurrentHashMap<String, Integer>();\ncounts.merge("A", 1, Integer::sum); ~~ 先get再put不是一个原子事务；compute回调应短且避免递归更新。
collections ~~ Collections 工具 ~~ Collections提供排序、二分搜索、视图和同步包装等集合工具。 ~~ var xs = new java.util.ArrayList<>(java.util.List.of(3, 1));\njava.util.Collections.sort(xs);\njava.util.Collections.reverse(xs); ~~ synchronizedList的复合遍历仍需要按其API要求同步。
generic-class ~~ 泛型类 ~~ 泛型把类型检查前移到编译期，减少强制转换。类型参数不能直接是基本类型。 ~~ class Box<T> {\n    private T value;\n    void set(T x) { value = x; }\n    T get() { return value; }\n} ~~ Box<Integer>不是Box<Number>的子类型，泛型通常不协变。
generic-method ~~ 泛型方法 ~~ 方法可独立声明类型参数，调用时经常可由参数推断。 ~~ static <T> T first(java.util.List<T> xs) {\n    return xs.get(0);\n} ~~ 示例要求非空列表；泛型只保证类型关系，不保证业务前置条件。
wildcard-extends ~~ 上界通配符 ~~ ? extends T允许读取为T，适合只从集合获取T值的接口。 ~~ double sum(java.util.List<? extends Number> xs) {\n    double s = 0;\n    for (Number n : xs) s += n.doubleValue();\n    return s;\n} ~~ 通常不能加入非null元素，因为实际元素类型可能是某个更窄子类。
wildcard-super ~~ 下界通配符 ~~ ? super T适合写入T；读取只能静态保证是Object。 ~~ void add(java.util.List<? super Integer> xs) {\n    xs.add(1);\n} ~~ PECS只是常用设计规则；既读又写且维持精确类型时可用T本身。
erasure ~~ 类型擦除 ~~ 泛型大多通过擦除实现，运行时通常不能区分List<String>与List<Integer>。 ~~ Class<?> a = new java.util.ArrayList<String>().getClass();\nClass<?> b = new java.util.ArrayList<Integer>().getClass(); ~~ 不能写new T()、new T[]或instanceof List<String>；使用工厂或类型令牌。
raw-type ~~ 原始类型风险 ~~ 不带类型参数的泛型类型会绕过部分检查，为兼容旧代码保留。 ~~ java.util.List<?> safeView = java.util.List.of("A");\nObject first = safeView.get(0); ~~ 用List<?>表达未知类型，不要用原始List隐藏未检查转换。
bounded-type ~~ 类型参数上界 ~~ T extends Number约束可使用的成员，多界限中类界限必须在前。 ~~ static <T extends Number> double twice(T value) {\n    return value.doubleValue() * 2;\n} ~~ 类型边界允许访问方法，不允许把任意Number直接赋给T。
''',API)
    add('异常与资源',r'''
exceptions ~~ 异常分类 ~~ Throwable分为Error与Exception；RuntimeException及其子类是非受检异常。其余受检异常须捕获或声明。 ~~ void fail() throws java.io.IOException {\n    throw new java.io.IOException("read failed");\n} ~~ 不要把所有Error当普通业务异常吞掉；通常应保留失败上下文。
try-catch ~~ try catch 捕获 ~~ 从具体到一般排列catch，匹配第一个兼容类型。多重捕获可共享处理逻辑。 ~~ try {\n    Integer.parseInt("x");\n} catch (NumberFormatException e) {\n    System.out.println("invalid number");\n} ~~ 空catch掩盖故障；记录或转换异常时保留原因链。
finally ~~ finally 清理 ~~ finally通常在try或catch结束时执行，但不能保证进程终止等场景执行。 ~~ try {\n    System.out.println("work");\n} finally {\n    System.out.println("cleanup");\n} ~~ 不要在finally中return，它可能覆盖返回值或吞掉异常。
try-resources ~~ 自动关闭资源 ~~ try-with-resources在退出时逆序关闭AutoCloseable资源，支持保留被抑制的关闭异常。 ~~ try (var in = java.nio.file.Files.newInputStream(java.nio.file.Path.of("data.txt"))) {\n    int first = in.read();\n} ~~ 示例需处理IOException；只关闭自己拥有的资源，不关闭借来的共享连接。
exception-chain ~~ 异常原因链 ~~ 转换异常时把原异常传入cause，便于追踪根因和分层定位。 ~~ try {\n    Integer.parseInt("bad");\n} catch (NumberFormatException e) {\n    throw new IllegalArgumentException("age is invalid", e);\n} ~~ 只复制e.getMessage会丢失堆栈与异常类型。
nullpointer ~~ 空指针定位 ~~ 空指针通常来自未初始化引用、缺失返回值或null拆箱，优先在边界明确契约。 ~~ String name = java.util.Objects.requireNonNull(input, "name"); ~~ 示例中的input来自调用上下文；到处判空会掩盖必须存在的数据错误。
''',API)
    add('并发与进阶',r'''
thread ~~ Thread 与中断 ~~ start创建新执行路径，直接run只是普通方法调用。interrupt是协作式取消信号。 ~~ Thread t = new Thread(() -> System.out.println("work"));\nt.start(); ~~ 不使用stop强杀；捕获InterruptedException后通常恢复中断或向上传递。
executor ~~ 线程池生命周期 ~~ ExecutorService复用线程并管理任务。Java21支持关闭时等待终止的AutoCloseable用法。 ~~ try (var pool = java.util.concurrent.Executors.newFixedThreadPool(2)) {\n    pool.submit(() -> System.out.println("job"));\n} ~~ newFixedThreadPool使用无界队列；生产系统按负载设置队列和拒绝策略。
synchronized ~~ synchronized 锁 ~~ 同一监视器上的互斥和释放/获取关系保证同步可见性，实例锁与类锁不同。 ~~ class Counter {\n    private int n;\n    synchronized void add() { n++; }\n    synchronized int get() { return n; }\n} ~~ 多把锁需保持固定顺序，避免在锁内执行慢IO。
volatile ~~ volatile 可见性 ~~ volatile写与后续读建立可见性及顺序约束，但复合读改写不会自动原子化。 ~~ class Worker {\n    volatile boolean stop;\n    void stop() { stop = true; }\n} ~~ volatile int的n++仍可能丢失更新，需原子类或锁。
atomic ~~ 原子变量 ~~ AtomicInteger提供原子增量、比较交换等操作，适合单个数值状态。 ~~ var count = new java.util.concurrent.atomic.AtomicInteger();\nint n = count.incrementAndGet(); ~~ 多个原子变量组合不自动构成事务，复合不变量应单独协调。
lock ~~ ReentrantLock 显式锁 ~~ 显式锁支持可中断获取、超时和条件队列；必须在finally释放。 ~~ var lock = new java.util.concurrent.locks.ReentrantLock();\nlock.lock();\ntry { System.out.println("critical"); }\nfinally { lock.unlock(); } ~~ 未持锁不能unlock；公平锁可能增加调度成本。
future ~~ CompletableFuture 组合 ~~ 可用thenApply转换值、thenCompose展平异步链，异常也要明确处理。 ~~ var f = java.util.concurrent.CompletableFuture.supplyAsync(() -> 2).thenApply(x -> x * 3);\nint n = f.join(); ~~ 默认异步执行器不适合大量阻塞IO；join会将失败包装为CompletionException。
virtual-thread ~~ 虚拟线程 ~~ Java21正式支持虚拟线程，适合大量等待型任务，不提升CPU密集计算的可用核数。 ~~ try (var pool = java.util.concurrent.Executors.newVirtualThreadPerTaskExecutor()) {\n    pool.submit(() -> System.out.println("task"));\n} ~~ Java21里长时间在synchronized内阻塞可能固定载体线程；不要直接套用后续JDK改进结论。
lambda ~~ Lambda 函数式接口 ~~ Lambda为只有一个抽象方法的目标接口提供实现，捕获的局部变量须为final或事实final。 ~~ java.util.function.IntUnaryOperator twice = x -> x * 2;\nint n = twice.applyAsInt(3); ~~ 捕获对象可变不代表并发安全；Lambda中的this来自外围实例。
stream ~~ Stream 流处理 ~~ 中间操作通常惰性执行，终端操作触发计算；流不能重复消费。 ~~ var xs = java.util.List.of(1, 2, 3);\nint n = xs.stream().filter(x -> x > 1).mapToInt(Integer::intValue).sum(); ~~ 不在流操作中修改源集合；并行流不保证更快。
reflection ~~ 反射与注解 ~~ 反射检查类型和成员，运行时读取注解需要RUNTIME保留策略。模块边界仍限制深反射。 ~~ Class<?> type = String.class;\nString name = type.getName(); ~~ setAccessible不代表可以突破所有模块和安全边界；缓存反射结果避免热路径重复查找。
files ~~ Files 与字符编码 ~~ NIO Path与Files处理路径和文件，文本读写明确UTF-8，并避免把超大文件一次读入内存。 ~~ var p = java.nio.file.Path.of("data.txt");\ntry (var lines = java.nio.file.Files.lines(p, java.nio.charset.StandardCharsets.UTF_8)) {\n    lines.limit(3).forEach(System.out::println);\n} ~~ 示例需处理IOException；Files.lines返回的流必须关闭。
''',API)
    return out
