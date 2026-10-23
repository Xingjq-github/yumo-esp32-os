from card import make,section
SOURCE='https://www.informit.com/store/design-patterns-elements-of-reusable-object-oriented-9780201633610'
def cards():
    out=[]
    def p(slug,title,family,problem,roles,code,caution,related):
        out.append(make('java.pattern.'+slug,title,'Java','设计模式/'+family,problem,code,caution,SOURCE,'GoF 23种模式；示例使用Java 21标准库',aliases=[slug,title.split(' ')[0]],related=['java.pattern.'+x for x in related],complete=True,extra=[section('roles','结构与选用',roles)]))
    p('singleton','Singleton 单例','创建型','为一种服务提供单一实例。只有生命周期确实全局唯一时才选用；依赖注入通常更容易测试。','参与者：单例类及访问点。枚举单例由JVM负责实例化，适合无延迟参数配置的单例。',r'''
public class SingletonDemo {
    enum Registry {
        INSTANCE;
        String name() { return "registry"; }
    }
    public static void main(String[] args) {
        System.out.println(Registry.INSTANCE.name());
    }
}
''','单例不使内部可变数据自动线程安全。不同类加载器可各自有实例；不要把进程内唯一误当分布式唯一。',['factory-method'])
    p('factory-method','Factory Method 工厂方法','创建型','父类型定义创建产品的扩展点，子类型决定具体产品。调用流程依赖产品接口而非具体构造。','参与者：Product、Creator和具体子类。新产品通过新建Creator子类接入；产品数量很少时简单工厂更直观。',r'''
public class FactoryMethodDemo {
    interface Message { String text(); }
    static abstract class Sender {
        abstract Message create();
        String send() { return create().text(); }
    }
    static class MailSender extends Sender {
        Message create() { return () -> "mail"; }
    }
    public static void main(String[] args) {
        System.out.println(new MailSender().send());
    }
}
''','简单工厂的单个switch不等同于GoF工厂方法。子类层次过多时可把创建策略作为函数注入。',['abstract-factory','template-method'])
    p('abstract-factory','Abstract Factory 抽象工厂','创建型','创建相互匹配的一组产品，使客户端一次切换整个产品族。适合主题组件、跨平台资源。','工厂提供多种产品创建方法；每个具体工厂保持同一产品族一致。增加产品族容易，增加产品种类需要修改所有工厂。',r'''
public class AbstractFactoryDemo {
    interface Button { String draw(); }
    interface Label { String draw(); }
    interface Theme {
        Button button(); Label label();
    }
    static class Dark implements Theme {
        public Button button() { return () -> "dark button"; }
        public Label label() { return () -> "dark label"; }
    }
    public static void main(String[] args) {
        Theme t = new Dark();
        System.out.println(t.button().draw() + ", " + t.label().draw());
    }
}
''','不要混用两个工厂产生的强关联产品。与工厂方法相比，重点是产品族而非单一创建点。',['factory-method','builder'])
    p('builder','Builder 建造者','创建型','把复杂对象的分步构建与最终对象分开，避免大量位置参数和半初始化对象泄漏。','Builder累积配置，build校验后生成不可变产品。可选Director复用固定构建步骤；不是每个Builder都需要Director。',r'''
public class BuilderDemo {
    record Request(String path, int timeout) {}
    static class Builder {
        private String path = "/";
        private int timeout = 1000;
        Builder path(String p) { path = p; return this; }
        Builder timeout(int t) { timeout = t; return this; }
        Request build() {
            if (path == null || timeout <= 0) throw new IllegalArgumentException();
            return new Request(path, timeout);
        }
    }
    public static void main(String[] args) {
        System.out.println(new Builder().path("/health").timeout(500).build());
    }
}
''','链式setter只是语法形式。关键是最终校验和构建/表示分离；Builder通常不在线程间共享。',['abstract-factory','prototype'])
    p('prototype','Prototype 原型','创建型','从现有对象复制生成新对象，适合初始配置复杂且差异很小的实例。','原型公开复制契约，客户端不依赖具体构造。示例使用复制构造并复制列表容器，元素String不可变。',r'''
import java.util.ArrayList;
import java.util.List;
public class PrototypeDemo {
    static class Profile {
        final List<String> tags;
        Profile(List<String> tags) { this.tags = new ArrayList<>(tags); }
        Profile copy() { return new Profile(tags); }
    }
    public static void main(String[] args) {
        Profile a = new Profile(List.of("base"));
        Profile b = a.copy();
        b.tags.add("new");
        System.out.println(a.tags + " / " + b.tags);
    }
}
''','必须定义浅复制与深复制边界。Object.clone默认浅复制，复杂对象图可能包含循环与共享节点。',['builder'])
    p('adapter','Adapter 适配器','结构型','把已有接口转换成客户端需要的接口，复用无法修改的遗留或第三方实现。','Target是目标接口，Adaptee是原实现，Adapter负责参数和返回值转换。优先对象组合适配。',r'''
public class AdapterDemo {
    interface Celsius { double read(); }
    static class FahrenheitSensor { double value() { return 77; } }
    static class Adapter implements Celsius {
        private final FahrenheitSensor sensor;
        Adapter(FahrenheitSensor s) { sensor = s; }
        public double read() { return (sensor.value() - 32) * 5 / 9; }
    }
    public static void main(String[] args) {
        System.out.println(new Adapter(new FahrenheitSensor()).read());
    }
}
''','适配需要转换语义而不只改方法名；处理单位、异常和生命周期差异。装饰器一般保留原接口。',['decorator','facade'])
    p('bridge','Bridge 桥接','结构型','把两个独立变化维度拆开，以组合连接，避免平台与业务类型的笛卡尔积子类。','Abstraction持有Implementor。例如通知种类和发送通道分别演化。与适配器相比，它通常是在设计时主动分离维度。',r'''
public class BridgeDemo {
    interface Channel { void send(String text); }
    static class ConsoleChannel implements Channel {
        public void send(String text) { System.out.println(text); }
    }
    static class Alert {
        private final Channel channel;
        Alert(Channel c) { channel = c; }
        void warn(String text) { channel.send("WARN: " + text); }
    }
    public static void main(String[] args) {
        new Alert(new ConsoleChannel()).warn("disk");
    }
}
''','只有两个维度确实需要独立扩展时才值得引入桥接。多一层委托增加定位成本。',['adapter','strategy'])
    p('composite','Composite 组合','结构型','用同一接口处理单个对象和对象集合，适合树状目录、菜单或表达式。','Leaf表示叶子，Composite包含子节点。客户端统一调用Component；必须明确是否允许共享子树及循环。',r'''
import java.util.List;
public class CompositeDemo {
    interface Node { int size(); }
    record FileNode(int size) implements Node {}
    record Folder(List<Node> children) implements Node {
        public int size() { return children.stream().mapToInt(Node::size).sum(); }
    }
    public static void main(String[] args) {
        Node tree = new Folder(List.of(new FileNode(2), new FileNode(3)));
        System.out.println(tree.size());
    }
}
''','树深度大时递归可能栈溢出。组合节点如允许父子互相引用，遍历时需要防环。',['visitor','iterator'])
    p('decorator','Decorator 装饰器','结构型','在保留接口的前提下包装对象，按组合顺序追加行为，避免功能组合子类爆炸。','Component是共同接口；Decorator持有被包装对象并转发。常见于IO流、格式化和校验。',r'''
public class DecoratorDemo {
    interface Text { String value(); }
    record Plain(String value) implements Text {}
    record Bracket(Text inner) implements Text {
        public String value() { return "[" + inner.value() + "]"; }
    }
    public static void main(String[] args) {
        Text t = new Bracket(new Bracket(new Plain("hello")));
        System.out.println(t.value());
    }
}
''','包装顺序可能改变行为；资源装饰器需要定义close传递。装饰目标是增加功能，代理主要控制访问。',['proxy','adapter'])
    p('facade','Facade 外观','结构型','给复杂子系统提供简单入口，隐藏调用顺序和内部对象。外观不应变成包含所有业务的巨大类。','Facade编排子系统，调用方减少依赖。子系统仍可有独立接口，外观并不要求禁止高级使用。',r'''
public class FacadeDemo {
    static class Decoder { String decode(String f) { return "decoded " + f; } }
    static class Speaker { void play(String s) { System.out.println(s); } }
    static class Player {
        private final Decoder decoder = new Decoder();
        private final Speaker speaker = new Speaker();
        void play(String file) { speaker.play(decoder.decode(file)); }
    }
    public static void main(String[] args) { new Player().play("song"); }
}
''','外观简化接口，适配器解决接口不兼容；两者可同时存在但目的不同。',['adapter','mediator'])
    p('flyweight','Flyweight 享元','结构型','共享大量重复且不可变的内部状态，外部状态由调用方传入，以减少重复对象占用。','工厂按键缓存共享实例。示例字体信息共享，字符绘制位置在调用时传入。',r'''
import java.util.HashMap;
import java.util.Map;
public class FlyweightDemo {
    record Glyph(char symbol) {
        String draw(int x) { return symbol + "@" + x; }
    }
    static class Factory {
        private final Map<Character, Glyph> cache = new HashMap<>();
        Glyph get(char c) { return cache.computeIfAbsent(c, Glyph::new); }
    }
    public static void main(String[] args) {
        Factory f = new Factory();
        System.out.println(f.get('A').draw(3));
        System.out.println(f.get('A') == f.get('A'));
    }
}
''','无界缓存会反而占满内存。共享状态必须避免可变上下文混入；并发访问工厂需要同步。',['singleton','prototype'])
    p('proxy','Proxy 代理','结构型','在相同接口前设置访问控制层，完成延迟初始化、权限、远程调用或缓存。','客户端依赖Subject，Proxy决定何时调用真实Subject。代理应保持调用者期望的异常和返回值契约。',r'''
public class ProxyDemo {
    interface Data { String load(); }
    static class Real implements Data {
        public String load() { return "data"; }
    }
    static class Lazy implements Data {
        private Data target;
        public String load() {
            if (target == null) target = new Real();
            return target.load();
        }
    }
    public static void main(String[] args) { System.out.println(new Lazy().load()); }
}
''','示例延迟代理只用于单线程。远程代理还要处理超时与失败，不能假定调用像本地方法一样可靠。',['decorator'])
    p('chain','Chain of Responsibility 责任链','行为型','将请求依次交给处理者，由每个节点决定处理或继续传递，降低发送者与处理者耦合。','Handler持有下一个Handler。明确是只由一个节点处理，还是过滤器式的多个节点共同处理。',r'''
public class ChainDemo {
    static class Handler {
        final int limit; final Handler next;
        Handler(int limit, Handler next) { this.limit = limit; this.next = next; }
        String approve(int amount) {
            if (amount <= limit) return "limit=" + limit;
            return next == null ? "rejected" : next.approve(amount);
        }
    }
    public static void main(String[] args) {
        Handler chain = new Handler(100, new Handler(1000, null));
        System.out.println(chain.approve(500));
    }
}
''','顺序直接影响结果。必须处理链尾无人接收、循环链和异常中断，避免请求静默丢失。',['command','decorator'])
    p('command','Command 命令','行为型','把一次操作封装为对象，便于排队、记录、重试或撤销。触发操作的对象不需要了解接收者细节。','Command定义execute，Receiver执行业务，Invoker持有命令。撤销是否可行取决于业务与状态快照。',r'''
public class CommandDemo {
    interface Command { void execute(); void undo(); }
    static class Counter { int value; }
    record Add(Counter counter, int delta) implements Command {
        public void execute() { counter.value += delta; }
        public void undo() { counter.value -= delta; }
    }
    public static void main(String[] args) {
        Counter c = new Counter();
        Command add = new Add(c, 3);
        add.execute(); add.undo();
        System.out.println(c.value);
    }
}
''','重试前考虑幂等。外部支付等操作不能随意通过反向运算撤销，通常需要补偿流程。',['memento','chain'])
    p('interpreter','Interpreter 解释器','行为型','为简单语言定义语法节点和解释操作，适合小型规则、布尔表达式或查询条件。','终结符与非终结符共同实现Expression，Context提供变量。语法复杂时使用成熟解析器更合适。',r'''
import java.util.Map;
public class InterpreterDemo {
    interface Expr { int eval(Map<String, Integer> values); }
    record Var(String name) implements Expr {
        public int eval(Map<String, Integer> values) { return values.get(name); }
    }
    record Add(Expr a, Expr b) implements Expr {
        public int eval(Map<String, Integer> values) { return a.eval(values) + b.eval(values); }
    }
    public static void main(String[] args) {
        Expr e = new Add(new Var("x"), new Var("y"));
        System.out.println(e.eval(Map.of("x", 2, "y", 3)));
    }
}
''','示例只演示已构造语法树，未包含文本解析。外部输入还需限制树深、节点数并处理缺失变量。',['composite','visitor'])
    p('iterator','Iterator 迭代器','行为型','提供顺序访问集合元素的统一方式，不暴露内部存储表示。','Aggregate提供Iterator，Iterator保存遍历状态。Java的Iterable与Iterator提供标准契约。',r'''
import java.util.Iterator;
import java.util.NoSuchElementException;
public class IteratorDemo {
    record Range(int end) implements Iterable<Integer> {
        public Iterator<Integer> iterator() {
            return new Iterator<>() {
                int next;
                public boolean hasNext() { return next < end; }
                public Integer next() {
                    if (!hasNext()) throw new NoSuchElementException();
                    return next++;
                }
            };
        }
    }
    public static void main(String[] args) { for (int n : new Range(3)) System.out.println(n); }
}
''','next越界应抛NoSuchElementException；并发修改策略由集合实现定义，不要默认快照或线程安全。',['composite'])
    p('mediator','Mediator 中介者','行为型','把多个对象之间的交互集中到协调对象，避免组件互相了解所有参与者。','Colleague通过Mediator报告事件并接收协调结果，适合表单联动或流程协调。',r'''
public class MediatorDemo {
    static class Form {
        private String text = "";
        private boolean enabled;
        void textChanged(String value) {
            text = value; enabled = !text.isBlank();
        }
        boolean canSubmit() { return enabled; }
    }
    public static void main(String[] args) {
        Form mediator = new Form();
        mediator.textChanged("hello");
        System.out.println(mediator.canSubmit());
    }
}
''','示例把输入事件和按钮状态协调集中在Form。中介者也可能过大，应按独立交互场景拆分。',['observer','facade'])
    p('memento','Memento 备忘录','行为型','在不暴露对象内部可变结构的情况下保存快照，以后恢复。适合编辑器撤销或局部检查点。','Originator生成并恢复Memento，Caretaker保管快照。记录应是独立值，不能与活动状态共享可变引用。',r'''
public class MementoDemo {
    static class Editor {
        private String text = "";
        record Snapshot(String text) {}
        void set(String value) { text = value; }
        Snapshot save() { return new Snapshot(text); }
        void restore(Snapshot s) { text = s.text(); }
        String text() { return text; }
    }
    public static void main(String[] args) {
        Editor e = new Editor(); e.set("old");
        var mark = e.save(); e.set("new"); e.restore(mark);
        System.out.println(e.text());
    }
}
''','大量全量快照占内存，需要数量上限或差量策略。快照格式跨版本持久化还需要校验和版本规则。',['command','prototype'])
    p('observer','Observer 观察者','行为型','一个对象变化后通知订阅者，解耦事件来源与响应者。适合界面状态及进程内事件。','Subject管理订阅，Observer处理通知。必须约定线程、通知顺序、异常隔离和退订时机。',r'''
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
public class ObserverDemo {
    static class Subject {
        private final List<Consumer<String>> listeners = new ArrayList<>();
        void add(Consumer<String> l) { listeners.add(l); }
        void remove(Consumer<String> l) { listeners.remove(l); }
        void publish(String text) {
            for (var l : List.copyOf(listeners)) l.accept(text);
        }
    }
    public static void main(String[] args) {
        Subject s = new Subject();
        Consumer<String> l = System.out::println;
        s.add(l); s.publish("changed"); s.remove(l);
    }
}
''','示例同步通知且不做异常隔离；生产事件总线应明确策略。忘记退订会让已关闭界面被长期引用。',['mediator','command'])
    p('state','State 状态','行为型','把随内部状态改变的行为放入状态对象，消除分散在各方法中的复杂条件分支。','Context持有State，状态可以决定后续转换。需要先画清允许的转换及失败路径。',r'''
public class StateDemo {
    interface State { void press(Player p); }
    static class Stopped implements State {
        public void press(Player p) { p.state = new Playing(); }
    }
    static class Playing implements State {
        public void press(Player p) { p.state = new Stopped(); }
    }
    static class Player {
        State state = new Stopped();
        void press() { state.press(this); }
    }
    public static void main(String[] args) {
        Player p = new Player(); p.press();
        System.out.println(p.state.getClass().getSimpleName());
    }
}
''','状态转换不能散乱地绕过Context。策略通常由调用者选算法，状态常因自身事件改变。',['strategy'])
    p('strategy','Strategy 策略','行为型','把可互换算法封装为共同接口，让调用者按场景选择，不必修改使用算法的业务流程。','Context持有Strategy。Java函数式接口可表达简单策略，无需为每个算法创建具名类。',r'''
import java.util.function.IntUnaryOperator;
public class StrategyDemo {
    static class Price {
        private final IntUnaryOperator discount;
        Price(IntUnaryOperator d) { discount = d; }
        int quote(int cents) { return discount.applyAsInt(cents); }
    }
    public static void main(String[] args) {
        Price normal = new Price(x -> x);
        Price sale = new Price(x -> x * 9 / 10);
        System.out.println(normal.quote(100) + " / " + sale.quote(100));
    }
}
''','示例采用分为单位的小整数；真实金额需处理舍入与溢出。策略选择本身仍需业务规则。',['state','template-method'])
    p('template-method','Template Method 模板方法','行为型','父类固定算法骨架，将可变步骤交给子类，适用于步骤顺序稳定而局部实现不同的流程。','Template Method通常不可重写，Primitive Operations由子类实现；可选Hook提供默认扩展。',r'''
public class TemplateMethodDemo {
    static abstract class Report {
        final String render() { return "BEGIN\n" + body() + "\nEND"; }
        abstract String body();
    }
    static class Daily extends Report {
        String body() { return "daily result"; }
    }
    public static void main(String[] args) { System.out.println(new Daily().render()); }
}
''','过多钩子会让流程难懂。若算法步骤需要运行时组合，可考虑策略和组合而非继承。',['strategy','factory-method'])
    p('visitor','Visitor 访问者','行为型','在元素类型相对稳定时，把新增操作放到访问者中，避免反复修改各元素类。','Element.accept调用对应visit形成双分派。增加访问者容易，增加元素类型需要更新所有访问者。',r'''
public class VisitorDemo {
    interface Visitor { int visit(NumberNode n); int visit(Pair p); }
    interface Node { int accept(Visitor v); }
    record NumberNode(int value) implements Node {
        public int accept(Visitor v) { return v.visit(this); }
    }
    record Pair(Node a, Node b) implements Node {
        public int accept(Visitor v) { return v.visit(this); }
    }
    static class Sum implements Visitor {
        public int visit(NumberNode n) { return n.value(); }
        public int visit(Pair p) { return p.a().accept(this) + p.b().accept(this); }
    }
    public static void main(String[] args) {
        Node n = new Pair(new NumberNode(2), new NumberNode(3));
        System.out.println(n.accept(new Sum()));
    }
}
''','访问者可能迫使元素暴露内部数据。Java21的sealed类型和模式switch也可用于有限类型集合，按演化方向选择。',['composite','interpreter'])
    for c in out:
        english,chinese=c['title'].rsplit(' ',1)
        c['title']=chinese+' '+english;c['short_title']=chinese
    return out
