/** Host-only regressions for identities, ordering and case business rules. */
public final class PatternSemanticsTest {
    static int checks;
    static void check(boolean value){checks++;if(!value)throw new AssertionError("semantic check "+checks);}
    public static void main(String[] args){
        PatternExamples.BoxBuilder builder=new PatternExamples.BoxBuilder("rice");
        builder.drink();builder.main();PatternExamples.Meal first=builder.build();
        check(first.value.equals("rice+tea"));builder.drink();check(builder.build().value.equals(first.value));
        check(first!=builder.build());
        PatternExamples.Sender sender=new PatternExamples.MailSender();
        PatternExamples.Message mail=sender.create();sender=new PatternExamples.SmsSender();
        check(mail instanceof PatternExamples.Mail);check(sender.create() instanceof PatternExamples.Sms);
        check(mail.send().equals("mail"));check(sender.create()!=sender.create());
        PatternExamples.Drink coffee=new PatternExamples.Coffee();
        PatternExamples.Drink drink=new PatternExamples.Sugar(new PatternExamples.Milk(coffee));
        check(coffee.cost()==10&&drink.cost()==13);
        PatternExamples.GlyphPool pool=new PatternExamples.GlyphPool();
        PatternExamples.Glyph a=pool.get('A');pool.get('B');
        check(a==pool.get('A')&&pool.values.size()==2);check(a.draw(0)==a.draw(2));
        PatternExamples.Machine machine=new PatternExamples.Machine();
        check(machine.select().equals("insert coin"));machine.coin();
        PatternExamples.State paid=machine.state;machine.coin();check(machine.state==paid);
        check(machine.select().equals("dispensed"));PatternExamples.State sold=machine.state;
        machine.coin();check(machine.state==sold&&machine.select().equals("sold out"));
        PatternExamples.Editor editor=new PatternExamples.Editor();PatternExamples.History history=new PatternExamples.History();
        history.checkpoint(editor);editor.set("B");history.restore(editor);check(editor.read().equals("A"));
        editor.set("C");history.restore(editor);check(editor.read().equals("A"));
        PatternExamples.Shelf shelf=new PatternExamples.Shelf(new String[]{"A","B","C"});
        java.util.Iterator<String> left=shelf.iterator(),right=shelf.iterator();
        check(left!=right);check(left.next().equals("A"));check(left.next().equals("B"));check(right.next().equals("A"));
        PatternExamples.Expr tree=new PatternExamples.Add(new PatternExamples.Variable(),new PatternExamples.NumberExpr(2));
        java.util.Map<String,Integer> context=new java.util.HashMap<String,Integer>();
        for(int x:new int[]{1,5,9}){context.put("x",x);check(tree.eval(context)==x+2);}
        PatternExamples.Channel channel=new PatternExamples.SmsChannel();
        check(new PatternExamples.Notice(channel).send().equals("sms:normal"));
        check(new PatternExamples.UrgentNotice(channel).send().equals("sms:urgent"));
        PatternExamples.Dialog dialog=new PatternExamples.Dialog();dialog.a.select();dialog.b.select();
        check(!dialog.a.on&&dialog.b.on);dialog.a.select();check(dialog.a.on&&!dialog.b.on);
        PatternExamples.Item book=new PatternExamples.Book(),toy=new PatternExamples.Toy();
        for(int option=0;option<3;option++){
            PatternExamples.Visitor visitor=option==0?new PatternExamples.PriceVisitor():option==1?new PatternExamples.WeightVisitor():new PatternExamples.CountVisitor();
            check(book.accept(visitor)+toy.accept(visitor)==new int[]{30,4,2}[option]);
        }
        System.out.println(checks+" Java semantic checks passed");
    }
}
