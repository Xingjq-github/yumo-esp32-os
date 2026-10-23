#pragma once
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>

// Small, deterministic object simulations. The UI only renders their state;
// success is evaluated from the objects, never from a multiple-choice answer.
namespace core::patterns::play {
struct Mission {
    const char *scene;
    std::array<const char *,4> nodes;
    std::array<const char *,3> goals;
    const char *lesson;
    const char *fire;
};
inline constexpr Mission Missions[]={
 {"双人计数站",{"计数器A","计数器B","独立实例","交付箱"},{"让 A=1，B 保持为 0","让 A=2，B=1","让 A=3，B=2"},"两个 new 实例，各自保存字段。","验收"},
 {"声音工作室",{"调用方","铃声对象","鼓声对象","笛声对象"},{"接上铃声，播放 ding","换成鼓声，播放 boom","换成笛声，播放 toot"},"接口是调用约定；声音来自实现对象。","播放"},
 {"引擎装配厂",{"车辆","普通引擎","快引擎","涡轮引擎"},{"装入普通引擎，行驶 10 米","换装快引擎，行驶 20 米","换装涡轮，行驶 30 米"},"Car 持有 Motor，把行驶委托给引擎。","发车"},
 {"唯一登记处",{"调用方A","共享实例","调用方B","登记总数"},{"A、B 各登记一次，总数 2","A 登记两次，B 一次，总数 3","A、B 各登记两次，总数 4"},"A、B 取得同一个 INSTANCE。","验收"},
 {"消息配送厂",{"待发消息","创建者","产品出口","收件箱"},{"装入两件消息，用邮件发送","装入两件消息，改用短信发送","装入三件消息，用推送发送"},"Sender 流程调用子类重写的 create。","发送"},
 {"成套控件厂",{"产品族","制造按钮","制造标签","装配台"},{"造出一套白色按钮与标签","切换黑色工厂，再造一套","用蓝色工厂造出匹配的一套"},"一个工厂创建相关产品族；不混搭。","装配"},
 {"套餐建造厂",{"主食配方","加入主食","加入茶饮","餐盒"},{"分步装好米饭与茶，再封盒","改为面条与茶，再封盒","改为沙拉与茶，再封盒"},"Builder 保存构建状态，最后交付产品。","封盒"},
 {"文档复制室",{"原件","复制文档","副本加标签","副本"},{"复制后加一个标签：原1副2","复制后加两个标签：原1副3","复制后加三个标签：原1副4"},"副本拥有独立列表，不修改原件。","验收"},
 {"旧设备接入",{"旧传感器","转换插头","摄氏客户端","读数屏"},{"接入转换插头，把 32F 变成 0C","读出 68F 对应的 20C","读出 212F 对应的 100C"},"适配器转换接口及单位，旧设备不变。","读取"},
 {"双轨通知站",{"通知种类","发送渠道","通知对象","收件箱"},{"发送一封紧急邮件","独立更换为普通短信","保持短信渠道，改为紧急通知"},"通知层次与渠道层次可独立扩展。","发送"},
 {"目录搭建场",{"当前目录","添加文件","子目录","添加小文件"},{"根目录放一个 10KB 文件","根目录 10KB，子目录 5KB","根目录 20KB，子目录 5KB"},"Folder 与 File 都是 Node，递归求大小。","求大小"},
 {"咖啡叠加店",{"咖啡基底","加奶包装","加糖包装","拆外层"},{"交付一杯原味咖啡，价格 10","包上一层牛奶，价格 12","奶外再包糖，价格 13"},"每层都是 Drink；调用内层并加价。","出杯"},
 {"一键电影院",{"统一入口","音响接线","画面接线","字幕接线"},{"接好音响，一键播放声音","再接好画面，一键播放影片","接齐三个子系统，播放带字幕影片"},"Cinema 协调子系统，客户端只调一个入口。","播放"},
 {"字形印刷厂",{"草稿字形","位置一","位置二","位置三"},{"草稿 AAA，本次印刷创建1个字形","草稿 ABA，本次印刷创建2个字形","草稿 ABC，本次印刷创建3个字形"},"每次印刷新建池，共享字形；坐标从外部传入。","印刷"},
 {"图片守门员",{"访问授权","图像代理","真实图像","请求次数"},{"无授权访问一次，图像不能加载","授权后访问，延迟加载图像","授权后访问两次，只加载一次"},"代理使用同一 Image 接口，控制真实对象。","访问"},
 {"报销接力站",{"报销金额","接上组长","接上经理","处理结果"},{"接好链路，让组长处理 50","让 500 经过组长转交经理","让 2000 走完整条链，返回无人处理"},"请求从链头开始，由当前处理者处理或转交。","提交"},
 {"遥控命令台",{"绑定开灯","绑定关灯","灯泡","待执行命令"},{"先绑定开灯，再按遥控器","先开灯，再换命令关灯","依次执行开灯、关灯、开灯"},"绑定不执行；Remote 调 Command，命令调 Lamp。","按遥控器"},
 {"表达式拼装",{"变量上下文","放入变量x","放入常数2","安装加法"},{"拼成 x+2；x=1 时结果是 3","保持表达式树，令 x=5，结果7","令 x=9，解释同一棵树得到11"},"Add 递归解释子表达式，变量从上下文取值。","求值"},
 {"书架双游标",{"书架ABC","游标A下一本","游标B下一本","重建游标B"},{"A 读到 A，B 尚未开始","A 读到 B，B 只读到 A","A 读到 C，B 只读到 B"},"每个 Iterator 保存自己的遍历位置。","验收"},
 {"中央调度室",{"接入中介","开关A","开关B","状态屏"},{"接入中介，然后选中 A","先选 A，再选 B，让 A 自动关闭","先选 B，再选 A，让 B 自动关闭"},"开关通知 Dialog，由中介管理相互影响。","验收"},
 {"时间快照室",{"编辑内容","保存快照","恢复快照","历史保管箱"},{"保存 A，然后编辑成 B","保存 A，编辑成 B，再恢复 A","编辑为 B 并保存，改为 C 后恢复 B"},"Editor 创建和恢复快照，History 只保管它。","验收"},
 {"订阅广播站",{"库存数值","显示屏A","显示屏B","广播出口"},{"只让 A 订阅，把库存 7 发给 A","让两个显示屏都收到库存 9","取消 A；更新为9，A留7，B变9"},"Stock 只通知当前订阅者；退订不会清除旧值。","发布"},
 {"自动售货机",{"投入硬币","当前状态","商品余量","出货口"},{"不投币直接购买，观察拒绝出货","投币后购买，成功取走一件商品","出货后再次购买，得到售罄提示"},"同一购买请求委托给当前 State 对象。","购买"},
 {"订单算法店",{"订单金额","计价策略","结算台","实付金额"},{"订单100用八折策略，收取80","订单150用满减策略，收取130","订单60用八折策略，收取48"},"Order 把计价委托给策略，不改结算流程。","结算"},
 {"导入流水线",{"具体导入器","输入文件","固定流程","入库结果"},{"用 CSV 导入器处理 CSV 文件","换 JSON 导入器与文件，保留流程","用 XML 导入器处理 XML 文件"},"基类固定读取、解析、保存顺序；子类只改解析。","启动"},
 {"物品巡检队",{"访问者","放入图书","放入玩具","汇总屏"},{"放入两种物品，用价格访问者求30","保留物品，换重量访问者求4","用计数访问者得到2，不改物品类"},"元素 accept 分派给对应的 visit 重载。","巡检"}
};
static_assert(sizeof(Missions)/sizeof(Missions[0])==26);

using State=std::array<int,12>;
struct Route {std::array<unsigned,8> nodes{};unsigned size=0;};
struct Plan {std::array<int,16> actions{};unsigned size=0;};
inline Plan plan(std::initializer_list<int> list){Plan p;for(auto a:list)if(p.size<p.actions.size())p.actions[p.size++]=a;return p;}
// Each walkthrough is a playable solution, not the win condition. Other valid
// operation orders are accepted by goal(). Hints are opt-in after the tutorial.
inline Plan solution(unsigned id,unsigned s){
 switch(id){
 case 0:return s==0?plan({0,4}):s==1?plan({0,0,1,4}):plan({0,0,0,1,1,4});
 case 1:case 2:return plan({int(s+1),4});
 case 3:return s==0?plan({0,2,4}):s==1?plan({0,0,2,4}):plan({0,0,2,2,4});
 case 4:return s==0?plan({0,0,4}):s==1?plan({1,0,0,4}):plan({1,1,0,0,0,4});
 case 5:return s==0?plan({1,2,4}):s==1?plan({0,1,2,4}):plan({0,0,1,2,4});
 case 6:return s==0?plan({1,2,4}):s==1?plan({0,1,2,4}):plan({0,0,1,2,4});
 case 7:return s==0?plan({1,2,4}):s==1?plan({1,2,2,4}):plan({1,2,2,2,4});
 case 8:return s==0?plan({1,4}):s==1?plan({1,0,4}):plan({1,0,0,4});
 case 9:return s==0?plan({0,4}):s==1?plan({0,1,4}):plan({0,4});
 case 10:return s==0?plan({1,4}):s==1?plan({1,2,3,4}):plan({1,1,2,3,4});
 case 11:return s==0?plan({4}):s==1?plan({1,4}):plan({1,2,4});
 case 12:return plan({int(s+1),4});
 case 13:return s==0?plan({1,2,3,4}):s==1?plan({1,3,0,2,4}):plan({1,0,2,0,3,4});
 case 14:return s==0?plan({4}):s==1?plan({0,4}):plan({0,4,4});
 case 15:return s==0?plan({1,2,4}):s==1?plan({1,2,0,4}):plan({1,2,0,0,4});
 case 16:return s==0?plan({0,4}):s==1?plan({0,4,1,4}):plan({0,4,1,4,0,4});
 case 17:return s==0?plan({1,2,3,4}):plan({0,4});
 case 18:return s==0?plan({1,4}):s==1?plan({1,1,2,4}):plan({1,1,1,2,2,4});
 case 19:return s==0?plan({0,1,4}):s==1?plan({0,1,2,4}):plan({0,2,1,4});
 case 20:return s==0?plan({1,0,4}):s==1?plan({1,0,2,4}):plan({0,1,0,2,4});
 case 21:return s==0?plan({1,4}):s==1?plan({1,2,0,4}):plan({1,0,4});
 case 22:return s==0?plan({4}):s==1?plan({0,4}):plan({0,4,4});
 case 23:return s==0?plan({0,1,4}):s==1?plan({0,0,1,1,4}):plan({1,4});
 case 24:return s==0?plan({4}):s==1?plan({0,1,4}):plan({0,0,1,1,4});
 case 25:return s==0?plan({1,2,4}):plan({0,4});
 default:return {};
 }
}

struct Game {
 unsigned id=0,stage=0;
 State v{};
 Route route;
 bool error=false,won=false;
 char message[128]{};
 void say(const char *s){std::snprintf(message,sizeof(message),"%s",s);}
 void path(std::initializer_list<unsigned> list){route={};for(auto n:list)if(route.size<route.nodes.size())route.nodes[route.size++]=n;}
 void reset(unsigned i,unsigned s){id=std::min(i,25u);stage=std::min(s,2u);v={};route={};won=error=false;
     if(id==1||id==2)v[0]=-1;
     if(id==4)v[2]=-1;
     if(id==5){v[1]=v[2]=-1;}
     if(id==6)v[1]=-1;
     if(id==13)v[1]=v[2]=v[3]=-1;
     if(id==16||id==20)v[1]=-1;
     if(id==9&&stage){v[0]=stage==1?1:0;v[1]=stage==2?1:0;}
     if(id==12&&stage)v[0]=(1<<stage)-1;
     if(id==17&&stage){v[0]=int(stage)-1;v[1]=v[2]=v[3]=1;}
     if(id==25&&stage){v[0]=int(stage)-1;v[1]=v[2]=1;}
     if(id==21&&stage==2){v[1]=v[2]=1;v[3]=v[4]=7;}
     say("点亮描边的部件，开始动手");
 }
 // Each mission has a deterministic prepared workbench, also used on resume.
 // Never silently claim to carry over an arbitrary previous sandbox state.
 const char *setup()const{
     if(id==9&&stage)return stage==1?"预置紧急邮件；分别更换种类和渠道。":"预置普通短信；保留渠道，只换通知。";
     if(id==12&&stage)return "已预置前序接线；补接新系统，再运行。";
     if(id==17&&stage)return "已预置 x+2 的树；只改上下文再求值。";
     if(id==25&&stage)return "已预置图书和玩具；只换访问者再巡检。";
     if(id==21&&stage==2)return "预置两屏订阅且已收到7；退订A再发布9。";
     return "本项使用新工作台；操作部件完成目标。";
 }
 bool editable(unsigned n)const{
     static constexpr unsigned masks[]={3,14,14,5,3,7,7,6,3,3,15,14,14,15,1,7,3,15,14,7,7,7,1,3,3,7};
     return n<4&&(masks[id]&(1u<<n));
 }
 bool goal()const{
 switch(id){
 case 0:return v[0]==int(stage+1)&&v[1]==int(stage);
 case 1:case 2:return v[0]==int(stage)&&v[1]>0;
 case 3:return v[0]==(stage?2:1)&&v[2]==(stage==2?2:1)&&v[1]==v[0]+v[2];
 case 4:return v[2]==int(stage)&&v[4+stage]==(stage==2?3:2)&&v[3]==v[4+stage]&&v[0]==0;
 case 5:return v[0]==int(stage)&&v[1]==int(stage)&&v[2]==int(stage)&&v[3]>0;
 case 6:return v[0]==int(stage)&&v[1]==int(stage)&&v[2]&&v[3];
 case 7:return v[0]&&v[1]==int(stage+2);
 case 8:return v[0]==int(stage)&&v[1]&&v[2];
 case 9:return v[0]==(stage==1?0:1)&&v[1]==(stage==0?0:1)&&v[2];
 case 10:return v[0]==(stage==2?2:1)&&v[1]==(stage?1:0)&&bool(v[2])==bool(stage);
 case 11:return v[0]==int(stage)&&(!stage||v[1]==1)&&(stage<2||v[2]==2);
 case 12:return v[0]==((1<<int(stage+1))-1)&&v[1]==v[0];
 case 13:return v[1]==0&&v[2]==(stage?1:0)&&v[3]==(stage==2?2:0)&&v[4]==int(stage+1);
 case 14:return stage==0?(!v[0]&&!v[1]&&v[3]==1):(v[0]&&v[1]==1&&v[2]>=int(stage));
 case 15:return v[0]==int(stage)&&v[1]&&v[2]&&v[3]==int(stage+1);
 case 16:return v[3]==int(stage+1)&&v[2]==(stage==1?0:1)&&v[4]==(stage==0?1:stage==1?2:5);
 case 17:return v[0]==int(stage)&&v[1]&&v[2]&&v[3]&&v[4]==(stage==0?3:stage==1?7:11);
 case 18:return v[0]==int(stage+1)&&v[1]==int(stage);
 case 19:return v[0]&&v[1]==(stage==1?0:1)&&v[2]==(stage==1?1:0)&&(!stage||v[3]==3)&&v[4]==(stage==1?2:1)&&(!stage||v[5]==(stage==1?6:9));
 case 20:return stage==0?(v[0]==1&&v[1]==0&&v[3]):stage==1?(v[0]==0&&v[1]==0&&v[2]&&v[3]):(v[0]==1&&v[1]==1&&v[2]&&v[3]);
 case 21:return stage==0?(v[1]&&!v[2]&&v[3]==7&&v[4]==0):stage==1?(v[1]&&v[2]&&v[3]==9&&v[4]==9):(!v[1]&&v[2]&&v[3]==7&&v[4]==9);
 case 22:return stage==0?(v[0]==0&&v[2]==1):stage==1?(v[0]==2&&v[1]==1):(v[0]==2&&v[1]==1&&v[2]==3);
 case 23:return v[0]==(stage==0?1:stage==1?2:0)&&v[1]==(stage==1?2:1)&&v[2]==(stage==0?80:stage==1?130:48);
 case 24:return v[0]==int(stage)&&v[1]==int(stage)&&v[2]==3;
 case 25:return v[0]==int(stage)&&v[1]&&v[2]&&v[3]==(stage==0?30:stage==1?4:2);
 }return false;
 }
 void act(unsigned a){
     if(a>4||won||(a<4&&!editable(a)))return;
     error=false;route={};say("部件已更新");
     auto cycle=[&](int &x,int count=3){x=(x+1)%count;};
     auto fail=[&](const char *why){error=true;say(why);};
     switch(id){
     case 0:if(a<2){v[a]=std::min(v[a]+1,9);path({a,3});say("只改变这个实例的字段");}break;
     case 1:case 2:
         if(a>=1&&a<=3){v[0]=int(a)-1;v[1]=0;path({a,0});say(id==1?"已接上实现对象":"已把引擎装入车辆");}
         else if(a==4){if(v[0]<0)fail("先点击一个部件，接入调用方");else{++v[1];path({0,unsigned(v[0]+1),0});say(id==1?(v[0]==0?"play() -> ding":v[0]==1?"play() -> boom":"play() -> toot"):(v[0]==0?"Motor 驱动车辆行驶10米":v[0]==1?"Motor 驱动车辆行驶20米":"Motor 驱动车辆行驶30米"));}}
         break;
     case 3:if(a==0||a==2){v[a]=std::min(v[a]+1,9);v[1]=v[0]+v[2];path({a,1,3});say("取得同一个实例，共享值加一");}break;
     case 4:
         if(a==0){v[0]=std::min(5,v[0]+1);say("待发消息加一");}
         if(a==1){cycle(v[1]);say("更换创建者，已交付的产品保持不变");}
         if(a==4){if(!v[0])fail("先点待发消息，装入要送的消息");else{v[2]=v[1];v[3]=std::min(255,v[3]+v[0]);v[4+v[1]]=std::min(255,v[4+v[1]]+v[0]);v[0]=0;path({0,1,2,3});say("Sender 调 create，再发送产品");}}break;
     case 5:
         if(a==0){cycle(v[0]);v[3]=0;say("换工厂不会改变已经造出的产品");}
         if(a==1||a==2){v[a]=v[0];v[3]=0;path({0,a});say("当前工厂造出一个产品");}
         if(a==4){if(v[1]<0||v[2]<0)fail("按钮和标签都造好才能装配");else if(v[1]!=v[2])fail("产品族不匹配，请重新制造");else{v[3]=1;path({1,3,2,3});say("同族产品装配成功");}}break;
     case 6:
         if(a==0){cycle(v[0]);v[3]=0;say("配方改变；已加入的主食不会变");}
         if(a==1){v[1]=v[0];v[3]=0;path({0,1,3});say("Builder 加入当前主食");}
         if(a==2){v[2]=1;v[3]=0;path({2,3});say("Builder 加入茶饮");}
         if(a==4){if(v[1]<0||!v[2])fail("餐盒还缺主食或茶饮");else {v[3]=1;path({1,3,2,3});say("完成构建，取得独立套餐产品");}}break;
     case 7:
         if(a==1){v[0]=1;v[1]=1;path({0,1,3});say("copy() 复制了标签列表");}
         if(a==2){if(!v[0])fail("先复制文档，再修改副本");else{v[1]=std::min(9,v[1]+1);path({2,3});say("副本加标签；原件仍为1");}}break;
     case 8:
         if(a==0){cycle(v[0]);v[2]=0;say("旧传感器的华氏读数已改变");}
         if(a==1){v[1]^=1;v[2]=0;say(v[1]?"SensorAdapter 已接入":"转换插头已拔出");}
         if(a==4){if(!v[1])fail("接口不匹配：请接入转换插头");else{v[2]=1;v[3]=(v[0]==0?32:v[0]==1?68:212);v[3]=(v[3]-32)*5/9;path({2,1,0,1,3});say("适配器返回换算后的摄氏温度");}}break;
     case 9:
         if(a<2){cycle(v[a],2);v[2]=0;say("只改变一个维度，另一维度不变");}
         if(a==4){v[2]=1;path({0,2,1,3});say(v[0]?"紧急通知委托渠道发送":"普通通知委托渠道发送");}break;
     case 10:
         if(a==0){v[3]=0;say("选中根目录");}
         if(a==2){v[2]=1;v[3]=1;path({0,2});say("建立并选中子目录");}
         if(a==1||a==3){if(a==3&&!v[2])fail("先建立子目录，再放入小文件");else if(a==1&&v[3])fail("10KB文件放根目录，请先选回根目录");else{int &n=v[a==3?1:0];n=std::min(5,n+1);say(a==3?"5KB文件加入子目录":"10KB文件加入根目录");path({a,a==3?2u:0u});}}
         if(a==4){path(v[2]?std::initializer_list<unsigned>{0,1,2,3,0}:std::initializer_list<unsigned>{0,1,0});std::snprintf(message,sizeof(message),"递归统计：根目录总大小 %d KB",v[0]*10+v[1]*5);}break;
     case 11:
         if(a==1||a==2){if(v[0]>=3)fail("最多叠三层，可拆掉外层");else{v[++v[0]]=int(a);path({a,0});say(a==1?"Milk 包装内层，价格加2":"Sugar 包装内层，价格加1");}}
         if(a==3&&v[0]){v[v[0]--]=0;say("只拆掉最外层包装");}
         if(a==4){route={};for(int j=v[0];j>0;--j)route.nodes[route.size++]=unsigned(v[j]);route.nodes[route.size++]=0;for(int j=1;j<=v[0];++j)route.nodes[route.size++]=unsigned(v[j]);int total=10;for(int j=1;j<=v[0];++j)total+=v[j]==1?2:1;std::snprintf(message,sizeof(message),"Coffee=10；外层 Drink 总价=%d",total);}break;
     case 12:
         if(a>=1&&a<=3){v[0]^=1<<int(a-1);v[1]=0;path({0,a});say("更新子系统接线");}
         if(a==4){const int need=(1<<int(stage+1))-1;if((v[0]&need)!=need)fail("本次播放缺少子系统接线");else{v[1]=need;path(stage==0?std::initializer_list<unsigned>{0,1}:stage==1?std::initializer_list<unsigned>{0,1,0,2}:std::initializer_list<unsigned>{0,1,0,2,0,3});say("统一入口按顺序启动子系统");}}break;
     case 13:
         if(a==0){cycle(v[0]);say("选择草稿字形；尚未创建共享对象");}
         if(a>=1&&a<=3){v[a]=v[0];v[4]=0;path({0,a});say("更新草稿位置；印刷时才创建字形池");}
         if(a==4){
             if(v[1]<0||v[2]<0||v[3]<0){fail("先填好三个草稿位置");break;}
             // One short-lived pool per print job. IDs A/B/C are stable in it.
             unsigned created=0;v[5]=0;
             for(int j=1;j<=3;++j){unsigned bit=1u<<v[j];if(created&bit)++v[5];created|=bit;}
             v[4]=(created&1)+((created>>1)&1)+((created>>2)&1);
             path({0,1,0,2,0,3});std::snprintf(message,sizeof(message),"本次印刷：创建%d，复用%d；结束释放池",v[4],v[5]);}break;
     case 14:
         if(a==0){v[0]^=1;say(v[0]?"已授权；真实图像还不会立即加载":"已撤销访问授权");}
         if(a==4){if(!v[0]){v[3]=1;path({1,0});say("代理拒绝访问，未触碰真实图像");}else{v[1]=1;++v[2];path({1,0,2,1});say(v[2]>1?"再次访问，复用同一个真实图像":"首次访问，代理延迟创建图像");}}break;
     case 15:
         if(a==0){cycle(v[0]);v[3]=0;say("发送者准备了新的报销请求");}
         if(a==1||a==2){v[a]^=1;v[3]=0;path(a==1?std::initializer_list<unsigned>{0,1}:std::initializer_list<unsigned>{1,2});say("更新处理者的后继接线");}
         if(a==4){if(!v[1])fail("发送者还没有接到链头");else if(v[0]&&!v[2])fail("组长处理不了，请接上后继经理");else{v[3]=v[0]+1;path(v[0]?std::initializer_list<unsigned>{0,1,2,3}:std::initializer_list<unsigned>{0,1,3});say(v[0]==0?"组长处理，停止继续传递":v[0]==1?"组长转交经理，经理处理":"所有处理者都不能处理，返回失败");}}break;
     case 16:
         if(a==0||a==1){v[1]=int(a);say(a==0?"已绑定开灯命令；灯暂时不变":"已绑定关灯命令；灯暂时不变");path({a,3});}
         if(a==4){if(v[1]<0)fail("先绑定一个命令，再按遥控器");else{v[2]=v[1]==0;++v[3];v[4]=((v[4]<<1)|v[2])&255;path({3,unsigned(v[1]),2});say(v[2]?"Command.execute -> Lamp.on":"Command.execute -> Lamp.off");}}break;
     case 17:
         if(a==0){cycle(v[0]);v[4]=0;say("只更新上下文里的变量值");}
         if(a>=1&&a<=3){v[a]^=1;v[4]=0;path({a,3});say("更新表达式树的节点");}
         if(a==4){if(!(v[1]&&v[2]&&v[3]))fail("表达式还缺变量、常数或加法节点");else{v[4]=(v[0]==0?1:v[0]==1?5:9)+2;path({3,1,0,1,3,2,3});std::snprintf(message,sizeof(message),"x=%d；Add: %d + 2 = %d",v[4]-2,v[4]-2,v[4]);}}break;
     case 18:
         if(a==1||a==2){auto &n=v[a-1];if(n>=3)fail("hasNext=false，不能再前进");else{++n;path({a,0,a});say("只移动这个 Iterator 的游标");}}
         if(a==3){v[1]=0;path({0,2});say("重建游标B，游标A不受影响");}break;
     case 19:
         if(a==0){v[0]^=1;v[3]=v[4]=v[5]=0;if(v[0]&&v[1]&&v[2])v[2]=0;say("接线完成；接着选择开关，触发中介通知");}
         if(a==1||a==2){v[a]=1;if(v[0]){v[5]=(v[4]<<2)|int(a);v[3]|=1<<int(a-1);v[4]=int(a);v[3-a]=0;path({a,0,3-a,3});say("Dialog 通知另一个开关取消选择");}else{path({a,3});say("未接中介，开关不会协调其他对象");}}break;
     case 20:
         if(a==0){cycle(v[0]);v[2]=0;if(v[1]>=0&&v[0]==(stage==2?2:1)&&v[0]!=v[1])v[3]=1;path({0});say("编辑器内容改变，快照保持不变");}
         if(a==1){v[1]=v[0];v[2]=v[3]=0;path({0,1,3});say("Editor 创建快照，交给 History 保管");}
         if(a==2){if(v[1]<0)fail("还没有快照，请先保存");else{v[2]=v[2]||(v[3]&&v[0]==(stage==2?2:1));v[0]=v[1];path({3,2,0});say("Editor 从不透明快照恢复自身状态");}}break;
     case 21:
         if(a==0){cycle(v[0],2);say("库存改变，点击发布才发送通知");}
         if(a==1||a==2){v[a]^=1;path({a,3});say(v[a]?"已订阅；等待下一次发布":"已退订；仍保留上次显示的值");}
         if(a==4){const int n=v[0]?9:7;if(v[1])v[3]=n;if(v[2])v[4]=n;path(v[1]&&v[2]?std::initializer_list<unsigned>{0,3,1,3,2}:v[1]?std::initializer_list<unsigned>{0,3,1}:v[2]?std::initializer_list<unsigned>{0,3,2}:std::initializer_list<unsigned>{0,3});say(v[1]||v[2]?"只通知当前订阅的显示屏":"没有订阅者，没有人收到通知");}break;
     case 22:
         if(a==0){if(v[0]==0){v[0]=1;path({0,1});say("投币，状态从 Empty 变为 Paid");}else fail(v[0]==2?"已经售罄，硬币退回":"已经投币，不重复收取");}
         if(a==4){if(v[0]==0){v[2]=1;path({1,3});say("Empty.buy：请先投币");}else if(v[0]==1){v[0]=2;v[1]=1;v[2]=2;path({1,2,3,1});say("Paid.buy：出货，转为 SoldOut");}else{v[2]=3;path({1,3});say("SoldOut.buy：已经售罄");}}break;
     case 23:
         if(a<2){cycle(v[a]);v[2]=0;say(a?"更换计价算法，不改 Order":"订单金额已改变");}
         if(a==4){int n=v[0]==0?60:v[0]==1?100:150;v[2]=v[1]==1?n*8/10:v[1]==2&&n>=100?n-20:n;path({0,2,1,2,3});say("Order.total 委托当前计价策略");}break;
     case 24:
         if(a<2){cycle(v[a]);v[2]=0;say(a?"输入文件已更换":"更换子类，只改变 parse 步骤");}
         if(a==4){if(v[0]!=v[1])fail("文件与解析器不匹配，尚未保存");else{v[2]=3;path({1,2,0,2,3});say("固定顺序：读取 -> 解析 -> 保存");}}break;
     case 25:
         if(a==0){cycle(v[0]);v[3]=0;say("换访问者，不改图书与玩具类");}
         if(a==1||a==2){v[a]^=1;v[3]=0;say(v[a]?"物品已放入对象集合":"物品已移出对象集合");}
         if(a==4){v[3]=v[0]==0?v[1]*10+v[2]*20:v[0]==1?v[1]+v[2]*3:v[1]+v[2];path(v[1]&&v[2]?std::initializer_list<unsigned>{1,0,3,2,0,3}:v[1]?std::initializer_list<unsigned>{1,0,3}:v[2]?std::initializer_list<unsigned>{2,0,3}:std::initializer_list<unsigned>{0,3});say("accept 分派给 visit(Book) / visit(Toy)");}break;
     }
     // Multi-operation missions (commands/proxy/state) intentionally need more
     // than one execution. A valid intermediate action is not an error.
     if(a==4&&!error)won=goal();
 }
 int hint()const{
     if(won||goal())return 4;
     const int t=int(stage);
     switch(id){
     case 0:if(v[0]>t+1||v[1]>t)return -1;return v[0]<t+1?0:v[1]<t?1:4;
     case 1:case 2:return v[0]!=t?t+1:4;
     case 3:{int a=stage?2:1,b=stage==2?2:1;if(v[0]>a||v[2]>b)return -1;return v[0]<a?0:v[2]<b?2:4;}
     case 4:{int n=stage==2?3:2;if(v[3]!=v[4+stage]||v[3]+v[0]>n)return -1;return v[1]!=t?1:v[3]+v[0]<n?0:4;}
     case 5:return v[0]!=t?0:v[1]!=t?1:v[2]!=t?2:4;
     case 6:return v[0]!=t?0:v[1]!=t?1:!v[2]?2:4;
     case 7:return !v[0]||v[1]>t+2?1:v[1]<t+2?2:4;
     case 8:return !v[1]?1:v[0]!=t?0:4;
     case 9:return v[0]!=(stage==1?0:1)?0:v[1]!=(stage==0?0:1)?1:4;
     case 10:{int n=stage==2?2:1;if(v[0]>n||v[1]>(stage?1:0)||(!stage&&v[2]))return -1;
         if(v[0]<n)return v[3]?0:1;
         return stage&&!v[2]?2:stage&&!v[1]?3:4;}
     case 11:for(int j=1;j<=v[0];++j)if(j>t||v[j]!=j)return 3;return v[0]<t?v[0]+1:4;
     case 12:{unsigned need=(1u<<(stage+1))-1;for(unsigned j=0;j<3;++j)if((unsigned(v[0])^need)&(1u<<j))return int(j+1);return 4;}
     case 13:for(int j=1;j<=3;++j){int want=j==1?0:j==2?(stage?1:0):(stage==2?2:0);if(v[j]!=want)return v[0]!=want?0:j;}return 4;
     case 14:if(!stage&&(v[1]||v[2]))return -1;return bool(v[0])!=bool(stage)?0:4;
     case 15:return v[0]!=t?0:!v[1]?1:!v[2]?2:4;
     case 16:{int n=t+1,seq=stage==0?1:stage==1?2:5;
         if(v[3]>n||v[4]!=(seq>>(n-v[3])))return -1;
         int command=v[3]%2;return v[3]==n?4:v[1]!=command?command:4;}
     case 17:if(v[0]!=t)return 0;for(int j=1;j<=3;++j)if(!v[j])return j;return 4;
     case 18:if(v[0]>t+1)return -1;return v[1]>t?3:v[0]<t+1?1:v[1]<t?2:4;
     case 19:if(!v[0])return 0;if(!stage)return v[4]!=1?1:4;
         {int first=stage==1?1:2,last=3-first;return v[5]==((first<<2)|last)?4:v[4]!=first?first:last;}
     case 20:{int saved=stage==2?1:0,modified=stage==2?2:1;
         if(v[1]!=saved)return v[0]!=saved?0:1;
         if(!stage)return v[0]!=modified?0:4;
         return v[2]?4:v[0]!=modified?0:2;}
     case 21:
         if(!stage&&v[4])return -1;
         if(stage==2&&v[3]!=7){if(v[0])return 0;if(!v[1])return 1;return 4;}
         if(v[1]!=(stage==2?0:1))return 1;
         if(v[2]!=(stage?1:0))return 2;
         return v[0]!=(stage?1:0)?0:4;
     case 22:if(!stage&&v[0])return -1;return stage&&!v[0]?0:4;
     case 23:return v[0]!=(stage==0?1:stage==1?2:0)?0:v[1]!=(stage==1?2:1)?1:4;
     case 24:return v[0]!=t?0:v[1]!=t?1:4;
     case 25:return v[0]!=t?0:!v[1]?1:!v[2]?2:4;
     }return -1;
 }
 void value(unsigned node,char *out,size_t bytes)const{
     const auto fmt=[&](const char *s,int n){std::snprintf(out,bytes,s,n);};
     const auto txt=[&](const char *s){std::snprintf(out,bytes,"%s",s);};
     static constexpr const char *families[]={"白色","黑色","蓝色"},*meals[]={"米饭","面条","沙拉"},*formats[]={"CSV","JSON","XML"};
     switch(id){
     case 0:if(node<2)fmt("%d",v[node]);else txt(node==2?"new / new":"A 与 B 独立");break;
     case 1:case 2:if(node==0)txt(v[0]<0?"尚未接入":"通过接口调用");else txt(v[0]==int(node-1)?"已装入":"点击装入");break;
     case 3:if(node==0||node==2)fmt("登记 %d 次",v[node]);else if(node==1)txt("INSTANCE");else fmt("%d",v[1]);break;
     case 4:if(node==0)fmt("待发 %d 件",v[0]);else if(node==1)txt(v[1]==0?"MailSender":v[1]==1?"SmsSender":"PushSender");else if(node==2){if(v[2]<0)txt("尚未创建");else std::snprintf(out,bytes,"%s #%d",v[2]==0?"Mail":v[2]==1?"Sms":"Push",v[3]);}else fmt("收到 %d 件",v[3]);break;
     case 5:if(node==0)txt(families[v[0]]);else if(node<3)txt(v[node]<0?"尚未制造":families[v[node]]);else txt(v[3]?"配套成功":"等待两件产品");break;
     case 6:if(node==0)txt(meals[v[0]]);else if(node==1)txt(v[1]<0?"空":meals[v[1]]);else if(node==2)txt(v[2]?"茶饮已加入":"空");else txt(v[3]?"已封盒":"构建中");break;
     case 7:if(node==0)txt("标签 1");else if(node==3)fmt("标签 %d",v[1]);else txt(node==1?"copy()":"只修改副本");break;
     case 8:if(node==0)fmt("%d F",v[0]==0?32:v[0]==1?68:212);else if(node==1)txt(v[1]?"已接入":"未接入");else if(node==2)txt("readCelsius");else if(v[2])fmt("%d C",v[3]);else txt("--");break;
     case 9:if(node==0)txt(v[0]?"紧急":"普通");else if(node==1)txt(v[1]?"短信":"邮件");else txt(node==2?(v[0]?"UrgentNotice":"Notice"):v[2]?"已发送":"等待发送");break;
     case 10:if(node==0)txt(v[3]?"选回根目录":"已选根目录");else if(node==1)fmt("10KB x%d",v[0]);else if(node==2)txt(v[2]?"已建子目录":"点击建立");else fmt("5KB x%d",v[1]);break;
     case 11:if(node==0)txt("Coffee = 10");else if(node==3){int price=10;for(int i=1;i<=v[0];++i)price+=v[i]==1?2:1;fmt("外层总价 %d",price);}else txt(node==1?"外包一层 +2":"外包一层 +1");break;
     case 12:if(node==0)txt("Cinema.play");else txt(v[0]&(1<<int(node-1))?"已接通":"点击接线");break;
     case 13:if(node==0)fmt("%c",'A'+v[0]);else if(v[node]>=0)fmt("%c",'A'+v[node]);else txt("点此印字");break;
     case 14:if(node==0)txt(v[0]?"已授权":"未授权");else if(node==1)txt("ImageProxy");else if(node==2)fmt("加载 %d 次",v[1]);else fmt("成功 %d 次",v[2]);break;
     case 15:if(node==0)fmt("%d",v[0]==0?50:v[0]==1?500:2000);else if(node==1||node==2)txt(v[node]?(node==1?"上限100":"上限1000"):"未接线");else txt(v[3]==1?"组长处理":v[3]==2?"经理处理":v[3]==3?"无人处理":"--");break;
     case 16:if(node==2)txt(v[2]?"亮":"灭");else if(node==3)txt(v[1]<0?"未绑定":v[1]==0?"LampOn":"LampOff");else txt(v[1]==int(node)?"已绑定":"只绑定不执行");break;
     case 17:if(node==0)fmt("x = %d",v[0]==0?1:v[0]==1?5:9);else if(node==3&&v[4])fmt("x+2 = %d",v[4]);else txt(v[node]?"已放入":"点击放入");break;
     case 18:if(node==0)txt("A  B  C");else if(node<3){int n=v[node-1];if(n)fmt("当前 %c",'A'+n-1);else txt("尚未开始");}else txt("只重置 B");break;
     case 19:if(node==0)txt(v[0]?"已接入":"未接入");else if(node<3)txt(v[node]?"选中":"未选中");else txt(v[0]?"由 Dialog 协调":"尚未协调");break;
     case 20:if(node==0)fmt("%c",'A'+v[0]);else if(node==1)txt("Editor.save");else if(node==2)txt("Editor.restore");else txt(v[1]<0?"空":"不透明快照");break;
     case 21:if(node==0)fmt("%d",v[0]?9:7);else if(node<3)std::snprintf(out,bytes,"%s %d",v[node]?"订阅":"退订",v[node+2]);else txt("notify()");break;
     case 22:if(node==0)txt("投入一枚");else if(node==1)txt(v[0]==0?"Empty":v[0]==1?"Paid":"SoldOut");else if(node==2)fmt("%d 件",v[0]==2?0:1);else fmt("出货 %d 件",v[1]);break;
     case 23:if(node==0)fmt("%d",v[0]==0?60:v[0]==1?100:150);else if(node==1)txt(v[1]==0?"原价":v[1]==1?"八折":"满100减20");else if(node==2)txt("Order.total");else fmt("%d",v[2]);break;
     case 24:if(node<2)txt(formats[v[node]]);else txt(node==2?"读 / 解析 / 存":v[2]?"保存成功":"尚未保存");break;
     case 25:if(node==0)txt(v[0]==0?"价格":v[0]==1?"重量":"数量");else if(node<3)txt(v[node]?"已放入":"点击放入");else fmt("%d",v[3]);break;
     default:txt("");break;
     }
 }
};
}
