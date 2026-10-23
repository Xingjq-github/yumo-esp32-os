import java.util.*;

/** Java 8 counterparts of the three workshop missions (option 0/1/2).
 * The device simulates objects; it does not execute Java. Integer units only. */
public final class PatternExamples {
    static final List<String> trace = new ArrayList<String>();
    static void hit(String text) { trace.add(text); }
    interface Demo { String run(int option); }

    // @objects
    static final class Counter { int value; void add() { value++; hit("Counter.add -> " + value); } }
    static String objects(int option) {
        Counter first = new Counter(), second = new Counter();
        for (int i=0;i<=option;i++) first.add();
        for (int i=0;i<option;i++) second.add();
        return first.value + "/" + second.value;
    }
    // @interfaces
    interface Sound { String play(); }
    static final class Bell implements Sound { public String play(){hit("Bell.play");return "ding";} }
    static final class Drum implements Sound { public String play(){hit("Drum.play");return "boom";} }
    static final class Flute implements Sound { public String play(){hit("Flute.play");return "toot";} }
    static String interfaces(int option) {
        Sound sound = option==0 ? new Bell() : option==1 ? new Drum() : new Flute();
        hit("Client -> Sound.play"); return sound.play();
    }
    // @composition
    interface Motor { int speed(); }
    static final class Slow implements Motor { public int speed(){hit("Slow.speed");return 10;} }
    static final class Fast implements Motor { public int speed(){hit("Fast.speed");return 20;} }
    static final class Turbo implements Motor { public int speed(){hit("Turbo.speed");return 30;} }
    static final class Car {
        final Motor motor; Car(Motor motor){this.motor=motor;}
        int drive(){hit("Car -> Motor.speed");return motor.speed();}
    }
    static String composition(int option) {
        return ""+new Car(option==0?new Slow():option==1?new Fast():new Turbo()).drive();
    }
    // @singleton
    enum Registry { INSTANCE; int value;
        void add(){value++;hit("Registry.INSTANCE.add");}
    }
    static String singleton(int option) {
        Registry.INSTANCE.value=0; // Test isolation, not production reset API.
        Registry a=Registry.INSTANCE,b=Registry.INSTANCE;
        for(int i=0;i<(option==0?1:2);i++)a.add();
        for(int i=0;i<(option==2?2:1);i++)b.add();
        hit("second reference reads shared state");return (a==b)+":"+b.value;
    }
    // @factory-method
    interface Message { String send(); }
    static final class Mail implements Message {public String send(){hit("Mail.send");return "mail";}}
    static final class Sms implements Message {public String send(){hit("Sms.send");return "sms";}}
    static final class Push implements Message {public String send(){hit("Push.send");return "push";}}
    static abstract class Sender {
        abstract Message create();
        final String deliver(){hit("Sender.deliver -> create");return create().send();}
    }
    static final class MailSender extends Sender {Message create(){hit("MailSender.create");return new Mail();}}
    static final class SmsSender extends Sender {Message create(){hit("SmsSender.create");return new Sms();}}
    static final class PushSender extends Sender {Message create(){hit("PushSender.create");return new Push();}}
    static String factoryMethod(int option) {
        Sender sender=option==0?new MailSender():option==1?new SmsSender():new PushSender();
        String result="";for(int i=0;i<(option==2?3:2);i++){if(i>0)result+="/";result+=sender.deliver();}return result;
    }
    // @abstract-factory
    interface Button {String color();} interface Label {String color();}
    interface Theme {Button button();Label label();}
    static final class Light implements Theme {
        public Button button(){hit("Light.button");return ()->"white";}
        public Label label(){hit("Light.label");return ()->"white";}
    }
    static final class Dark implements Theme {
        public Button button(){hit("Dark.button");return ()->"black";}
        public Label label(){hit("Dark.label");return ()->"black";}
    }
    static final class Blue implements Theme {
        public Button button(){hit("Blue.button");return ()->"blue";}
        public Label label(){hit("Blue.label");return ()->"blue";}
    }
    static String abstractFactory(int option) {
        Theme factory=option==0?new Light():option==1?new Dark():new Blue();
        hit("Client -> Theme product family");return factory.button().color()+"/"+factory.label().color();
    }
    // @builder
    static final class Meal {final String value;Meal(String value){this.value=value;}}
    interface MealBuilder {void main();void drink();Meal build();}
    static final class BoxBuilder implements MealBuilder {
        final String type;boolean hasMain,hasTea;BoxBuilder(String type){this.type=type;}
        public void main(){hit("Builder.main");hasMain=true;}
        public void drink(){hit("Builder.drink");hasTea=true;}
        public Meal build(){if(!hasMain||!hasTea)throw new IllegalStateException();hit("Builder.build");return new Meal(type+"+tea");}
    }
    static final class Kitchen {Meal cook(MealBuilder b){hit("Director.cook");b.main();b.drink();return b.build();}}
    static String builder(int option) {
        return new Kitchen().cook(new BoxBuilder(new String[]{"rice","noodle","salad"}[option])).value;
    }
    // @prototype
    interface Copyable {Copyable copy();}
    static final class Document implements Copyable {
        final List<String> tags;
        Document(List<String> tags){this.tags=new ArrayList<String>(tags);}
        public Document copy(){hit("Prototype.copy: own list");return new Document(tags);}
    }
    static String prototype(int option) {
        Document original=new Document(Arrays.asList("base"));
        Document copy=original.copy();for(int i=0;i<=option;i++)copy.tags.add("new");
        hit("modify copy; original unchanged");return original.tags.size()+"/"+copy.tags.size();
    }
    // @adapter
    interface Celsius {int read();}
    static final class OldSensor {final int f;OldSensor(int f){this.f=f;}int fahrenheit(){hit("OldSensor.fahrenheit");return f;}}
    static final class SensorAdapter implements Celsius {
        final OldSensor old;SensorAdapter(OldSensor old){this.old=old;}
        public int read(){hit("Adapter.read -> old interface");return (old.fahrenheit()-32)*5/9;}
    }
    static String adapter(int option) {
        Celsius sensor=new SensorAdapter(new OldSensor(new int[]{32,68,212}[option]));
        hit("Client -> Celsius.read");return sensor.read()+"C";
    }
    // @bridge
    interface Channel {String send(String text);}
    static final class EmailChannel implements Channel {public String send(String s){hit("EmailChannel.send");return "mail:"+s;}}
    static final class SmsChannel implements Channel {public String send(String s){hit("SmsChannel.send");return "sms:"+s;}}
    static class Notice {
        final Channel channel;Notice(Channel channel){this.channel=channel;}
        String send(){hit("Notice -> Channel");return channel.send("normal");}
    }
    static final class UrgentNotice extends Notice {
        UrgentNotice(Channel channel){super(channel);}
        String send(){hit("UrgentNotice -> Channel");return channel.send("urgent");}
    }
    static String bridge(int option) {
        Channel channel=option==0?new EmailChannel():new SmsChannel();
        Notice n=option==1?new Notice(channel):new UrgentNotice(channel);
        return n.send();
    }
    // @composite
    interface Node {int size();}
    static final class FileNode implements Node {final int n;FileNode(int n){this.n=n;}public int size(){hit("File.size -> "+n);return n;}}
    static final class Folder implements Node {
        final List<Node> children=new ArrayList<Node>();
        public int size(){hit("Folder.size -> children");int total=0;for(Node n:children)total+=n.size();return total;}
    }
    static String composite(int option) {
        Folder root=new Folder(),sub=new Folder();root.children.add(new FileNode(10));
        if(option==2)root.children.add(new FileNode(10));
        if(option>=1){sub.children.add(new FileNode(5));root.children.add(sub);}
        Node node=root;return ""+node.size();
    }
    // @decorator
    interface Drink {int cost();}
    static final class Coffee implements Drink {public int cost(){hit("Coffee.cost -> 10");return 10;}}
    static final class Milk implements Drink {
        final Drink inner;Milk(Drink inner){this.inner=inner;}
        public int cost(){hit("Milk -> Drink.cost + 2");return inner.cost()+2;}
    }
    static final class Sugar implements Drink {
        final Drink inner;Sugar(Drink inner){this.inner=inner;}
        public int cost(){hit("Sugar -> Drink.cost + 1");return inner.cost()+1;}
    }
    static String decorator(int option) {
        Drink drink=new Coffee();if(option>=1)drink=new Milk(drink);if(option==2)drink=new Sugar(drink);
        hit("Client -> Drink.cost");return ""+drink.cost();
    }
    // @facade
    static final class Audio {void on(){hit("Audio.on");}}
    static final class Video {void on(){hit("Video.on");}}
    static final class Captions {void on(){hit("Captions.on");}}
    static final class Cinema {
        final Audio a=new Audio();final Video v=new Video();final Captions c=new Captions();
        String play(int kind){hit("Facade.play");a.on();if(kind>=1)v.on();if(kind==2)c.on();return kind==0?"audio":kind==1?"audio+video":"audio+video+text";}
    }
    static String facade(int option){return new Cinema().play(option);}
    // @flyweight
    static final class Glyph {
        final char shape;Glyph(char shape){this.shape=shape;}
        char draw(int x){hit("Glyph("+shape+").draw x="+x);return shape;}
    }
    static final class GlyphPool {
        final Map<Character,Glyph> values=new HashMap<Character,Glyph>();
        Glyph get(char c){if(!values.containsKey(c))values.put(c,new Glyph(c));return values.get(c);}
    }
    static String flyweight(int option) {
        // Editing the draft allocates no glyphs. Each print job owns a fresh pool.
        String word=new String[]{"AAA","ABA","ABC"}[option];GlyphPool pool=new GlyphPool();
        for(int x=0;x<word.length();x++)pool.get(word.charAt(x)).draw(x);
        return "shared="+pool.values.size()+",draws=3";
    }
    // @proxy
    interface Image {String show();}
    static final class RealImage implements Image {
        RealImage(){hit("RealImage.load");}
        public String show(){hit("RealImage.show");return "image";}
    }
    static final class ImageProxy implements Image {
        final boolean allowed;RealImage real;
        ImageProxy(boolean allowed){this.allowed=allowed;}
        public String show(){hit("Proxy.check");if(!allowed)return "denied";if(real==null)real=new RealImage();return real.show();}
    }
    static String proxy(int option) {
        Image image=new ImageProxy(option!=0);String text=image.show();if(option==2)text+="/"+image.show();return text;
    }
    // @chain
    static class Approver {
        final int limit;final String name;Approver next;
        Approver(int limit,String name){this.limit=limit;this.name=name;}
        String handle(int amount){hit(name+".handle "+amount);if(amount<=limit)return name;return next==null?"unhandled":next.handle(amount);}
    }
    static String chain(int option) {
        Approver lead=new Approver(100,"lead"),boss=new Approver(1000,"boss");lead.next=boss;
        return lead.handle(new int[]{50,500,2000}[option]);
    }
    // @command
    static final class Lamp {boolean on;void set(boolean v){on=v;hit("Lamp.set "+v);}}
    interface Command {void execute();}
    static final class LampCommand implements Command {
        final Lamp lamp;final boolean target;LampCommand(Lamp lamp,boolean target){this.lamp=lamp;this.target=target;}
        public void execute(){hit("Command.execute");lamp.set(target);}
    }
    static final class Remote {
        Command binding;void bind(Command c){binding=c;hit("Remote.bind");}
        void press(){hit("Remote.press");binding.execute();}
    }
    static String command(int option) {
        Lamp lamp=new Lamp();Remote remote=new Remote();remote.bind(new LampCommand(lamp,true));
        remote.press();String sequence="on";
        if(option>=1){remote.bind(new LampCommand(lamp,false));remote.press();sequence+="/off";}
        if(option==2){remote.bind(new LampCommand(lamp,true));remote.press();sequence+="/on";}
        return sequence;
    }
    // @interpreter
    interface Expr {int eval(Map<String,Integer> context);}
    static final class NumberExpr implements Expr {final int value;NumberExpr(int n){value=n;}public int eval(Map<String,Integer> c){hit("Number.eval");return value;}}
    static final class Variable implements Expr {public int eval(Map<String,Integer> c){hit("Variable.eval x");return c.get("x");}}
    static final class Add implements Expr {
        final Expr a,b;Add(Expr a,Expr b){this.a=a;this.b=b;}
        public int eval(Map<String,Integer> c){hit("Add.eval -> children");return a.eval(c)+b.eval(c);}
    }
    static String interpreter(int option) {
        Expr expression=new Add(new Variable(),new NumberExpr(2)); // Prepared structure for tasks 1/2.
        Map<String,Integer> context=new HashMap<String,Integer>();context.put("x",1);
        context.put("x",new int[]{1,5,9}[option]);return ""+expression.eval(context);
    }
    // @iterator
    static final class Shelf implements Iterable<String> {
        final String[] items;Shelf(String[] items){this.items=items.clone();}
        public Iterator<String> iterator(){return new Iterator<String>() {
            int cursor;public boolean hasNext(){return cursor<items.length;}
            public String next(){if(!hasNext())throw new NoSuchElementException();hit("Iterator.next "+cursor);return items[cursor++];}
            public void remove(){throw new UnsupportedOperationException();}
        };}
    }
    static String iterator(int option) {
        Shelf shelf=new Shelf(new String[]{"A","B","C"});
        Iterator<String> a=shelf.iterator(),b=shelf.iterator();String left="-",right="-";
        for(int i=0;i<=option;i++)left=a.next();
        for(int i=0;i<option;i++)right=b.next();return left+"/"+right;
    }
    // @mediator
    interface Mediator {void changed(Switch source);}
    static final class Switch {
        final Mediator mediator;final String name;boolean on;
        Switch(Mediator m,String name){mediator=m;this.name=name;}
        void select(){hit(name+" -> Mediator.changed");mediator.changed(this);}
    }
    static final class Dialog implements Mediator {
        final Switch a=new Switch(this,"A"),b=new Switch(this,"B");
        public void changed(Switch source){hit("Dialog coordinates A/B");a.on=source==a;b.on=source==b;}
    }
    static String mediator(int option) {
        Dialog dialog=new Dialog();hit("Dialog initialized: neither selected");
        if(option==2){dialog.b.select();dialog.a.select();}
        else{dialog.a.select();if(option==1)dialog.b.select();}
        return dialog.a.on+"/"+dialog.b.on;
    }
    // @memento
    static final class Editor {
        private String text="A";
        private static final class Snapshot {private final String text;private Snapshot(String t){text=t;}}
        Snapshot save(){hit("Editor.save");return new Snapshot(text);}
        void set(String s){text=s;hit("Editor.set "+s);}
        void restore(Snapshot s){hit("Editor.restore");text=s.text;}
        String read(){return text;}
    }
    static final class History {
        private Editor.Snapshot token;
        void checkpoint(Editor e){token=e.save();}
        void restore(Editor e){e.restore(token);} // opaque: never inspect token.text
    }
    static String memento(int option) {
        Editor editor=new Editor();History history=new History();
        if(option==2)editor.set("B");history.checkpoint(editor);
        editor.set(option==2?"C":"B");if(option>=1)history.restore(editor);return editor.read();
    }
    // @observer
    interface Observer {void update(int value);}
    static final class Display implements Observer {
        final String name;int value;Display(String name){this.name=name;}
        public void update(int value){this.value=value;hit(name+".update "+value);}
    }
    static final class Stock {
        final List<Observer> observers=new ArrayList<Observer>();
        void subscribe(Observer o){observers.add(o);}
        void unsubscribe(Observer o){observers.remove(o);}
        void change(int value){hit("Stock.change -> notify");for(Observer o:new ArrayList<Observer>(observers))o.update(value);}
    }
    static String observer(int option) {
        Stock stock=new Stock();Display a=new Display("A"),b=new Display("B");stock.subscribe(a);
        if(option>=1)stock.subscribe(b);
        if(option==2){stock.change(7);stock.unsubscribe(a);} // Same prepared history as task 2.
        stock.change(option==0?7:9);return a.value+"/"+b.value;
    }
    // @state
    interface State {String select(Machine machine);}
    static final class Empty implements State {public String select(Machine m){hit("Empty.select");return "insert coin";}}
    static final class Paid implements State {
        public String select(Machine m){hit("Paid.select -> dispense");m.state=new SoldOut();return "dispensed";}
    }
    static final class SoldOut implements State {public String select(Machine m){hit("SoldOut.select");return "sold out";}}
    static final class Machine {
        State state=new Empty();void coin(){
            if(state instanceof Empty){state=new Paid();hit("Machine.coin -> Paid");}
            else hit("Machine.coin -> returned");
        }
        String select(){hit("Machine -> State.select");return state.select(this);}
    }
    static String state(int option) {
        Machine machine=new Machine();if(option>=1)machine.coin();String result=machine.select();
        if(option==2)result=machine.select();return result;
    }
    // @strategy
    interface PriceStrategy {int price(int amount);}
    static final class Normal implements PriceStrategy {public int price(int a){hit("Normal.price");return a;}}
    static final class Discount implements PriceStrategy {public int price(int a){hit("Discount.price");return a*8/10;}}
    static final class Reduction implements PriceStrategy {public int price(int a){hit("Reduction.price");return a>=100?a-20:a;}}
    static final class Order {
        final PriceStrategy strategy;Order(PriceStrategy s){strategy=s;}
        int total(int amount){hit("Order -> PriceStrategy.price");return strategy.price(amount);}
    }
    static String strategy(int option) {
        Order order=new Order(option==1?new Reduction():new Discount());
        return ""+order.total(new int[]{100,150,60}[option]);
    }
    // @template-method
    static abstract class Importer {
        final String run(){hit("Template.read");String value=parse();hit("Template.save");return value+":saved";}
        abstract String parse();
    }
    static final class CsvImporter extends Importer {String parse(){hit("Csv.parse");return "csv";}}
    static final class JsonImporter extends Importer {String parse(){hit("Json.parse");return "json";}}
    static final class XmlImporter extends Importer {String parse(){hit("Xml.parse");return "xml";}}
    static String templateMethod(int option) {
        Importer importer=option==0?new CsvImporter():option==1?new JsonImporter():new XmlImporter();return importer.run();
    }
    // @visitor
    interface Visitor {int visit(Book book);int visit(Toy toy);}
    interface Item {int accept(Visitor visitor);}
    static final class Book implements Item {public int accept(Visitor v){hit("Book.accept -> visit(Book)");return v.visit(this);}}
    static final class Toy implements Item {public int accept(Visitor v){hit("Toy.accept -> visit(Toy)");return v.visit(this);}}
    static final class PriceVisitor implements Visitor {public int visit(Book b){hit("Price.visit Book");return 10;}public int visit(Toy t){hit("Price.visit Toy");return 20;}}
    static final class WeightVisitor implements Visitor {public int visit(Book b){hit("Weight.visit Book");return 1;}public int visit(Toy t){hit("Weight.visit Toy");return 3;}}
    static final class CountVisitor implements Visitor {public int visit(Book b){hit("Count.visit Book");return 1;}public int visit(Toy t){hit("Count.visit Toy");return 1;}}
    static String visitor(int option) {
        Visitor operation=option==0?new PriceVisitor():option==1?new WeightVisitor():new CountVisitor();
        int total=0;for(Item item:Arrays.asList(new Book(),new Toy()))total+=item.accept(operation);return ""+total;
    }
    // @end
    static final Demo[] DEMOS={PatternExamples::objects,PatternExamples::interfaces,PatternExamples::composition,
        PatternExamples::singleton,PatternExamples::factoryMethod,PatternExamples::abstractFactory,
        PatternExamples::builder,PatternExamples::prototype,PatternExamples::adapter,PatternExamples::bridge,
        PatternExamples::composite,PatternExamples::decorator,PatternExamples::facade,PatternExamples::flyweight,
        PatternExamples::proxy,PatternExamples::chain,PatternExamples::command,PatternExamples::interpreter,
        PatternExamples::iterator,PatternExamples::mediator,PatternExamples::memento,PatternExamples::observer,
        PatternExamples::state,PatternExamples::strategy,PatternExamples::templateMethod,PatternExamples::visitor};
    // Independent expected examples; used for regression, never the patterned path.
    static final String[][] EXPECTED={
        {"1/0","2/1","3/2"},{"ding","boom","toot"},{"10","20","30"},
        {"true:2","true:3","true:4"},{"mail/mail","sms/sms","push/push/push"},{"white/white","black/black","blue/blue"},
        {"rice+tea","noodle+tea","salad+tea"},{"1/2","1/3","1/4"},{"0C","20C","100C"},
        {"mail:urgent","sms:normal","sms:urgent"},{"10","15","25"},{"10","12","13"},
        {"audio","audio+video","audio+video+text"},{"shared=1,draws=3","shared=2,draws=3","shared=3,draws=3"},
        {"denied","image","image/image"},{"lead","boss","unhandled"},{"on","on/off","on/off/on"},
        {"3","7","11"},{"A/-","B/A","C/B"},{"true/false","false/true","true/false"},
        {"B","A","B"},{"7/0","9/9","7/9"},{"insert coin","dispensed","sold out"},
        {"80","130","48"},{"csv:saved","json:saved","xml:saved"},{"30","4","2"}};
    static String run(int id,int design,int option) {
        trace.clear();
        if(design==1){hit("Client -> hardcoded sample table");return EXPECTED[id][option];}
        return DEMOS[id].run(option);
    }
    public static void main(String[] args) {
        for(int id=0;id<DEMOS.length;id++)for(int design=0;design<2;design++)for(int option=0;option<3;option++) {
            String result=run(id,design,option);
            if(!result.equals(EXPECTED[id][option]))throw new AssertionError(id+":"+result);
            if(trace.isEmpty())throw new AssertionError("Missing execution trace: "+id);
            System.out.println(id+"\t"+design+"\t"+option+"\t"+result+"\t"+String.join("|",trace));
        }
    }
}
