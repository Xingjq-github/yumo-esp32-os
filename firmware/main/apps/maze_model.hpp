#pragma once
#include <algorithm>
#include <cstdint>
namespace maze {
constexpr unsigned Levels=15, Stages=5, MaxCells=81;
constexpr float BallRadius=6,GoalRadius=9;
constexpr unsigned stage(unsigned level){return (level<Levels?level:Levels-1)/3;}
constexpr unsigned dimension(unsigned level){return 5+stage(level);}
enum Wall : uint8_t {Up=1,Right=2,Down=4,Left=8};
enum class Material : uint8_t { Steel, Marble, Wood, Rubber, Glass };
enum class Hazard : uint8_t { Sand, Ice, Pit, Bumper };
struct Rect {float x=0,y=0,w=0,h=0;};
struct Trap {
    unsigned cell=0;Hazard kind=Hazard::Sand;Rect area{};
    bool rounded=false;uint8_t side=0;
    float coreDepth=0; // Deep/raised part; the remaining rim is safe to roll over.
    uint16_t corridor=0,shape=0;
};
struct Physics {float acceleration,drag,rebound,maxSpeed;};
constexpr Physics physics(Material m) {
    switch(m) {
    case Material::Steel:return {.20f,.965f,.18f,1.65f};
    case Material::Marble:return {.19f,.945f,.12f,1.6f};
    case Material::Wood:return {.24f,.89f,.10f,1.5f};
    case Material::Rubber:return {.28f,.86f,.48f,1.45f};
    default:return {.17f,.98f,.22f,1.7f};
    }
}
constexpr uint32_t random(uint32_t &s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
struct Board {
    unsigned n=5,goal=24;
    uint8_t walls[MaxCells]{};
    Material material=Material::Steel;
    Trap traps[12]{};
    unsigned trapCount=0;
    bool route[MaxCells]{};
    constexpr int cell() const {return 236/int(n);}
    constexpr int origin() const {return (240-cell()*int(n))/2;}
    constexpr int roadWidth() const {return cell()-3;}
    constexpr float centerX(unsigned i) const {return origin()+int(i%n)*cell()+cell()/2.0f;}
    constexpr float centerY(unsigned i) const {return origin()+int(i/n)*cell()+cell()/2.0f;}
};
constexpr unsigned targetTraps(unsigned level){return 8+stage(level);}
constexpr bool hardTrap(Hazard h){return h==Hazard::Pit||h==Hazard::Bumper;}
constexpr unsigned quadrant(float x,float y){return unsigned(x>=120)+2*unsigned(y>=120);}
constexpr float clamp(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
struct Corridor {unsigned first,last,id;float lo,hi;};
constexpr Corridor corridor(const Board &b,unsigned at,unsigned side){
    const bool horizontal=side==Up||side==Down;
    const unsigned rails=horizontal?(Up|Down):(Left|Right);
    const unsigned before=horizontal?Left:Up,after=horizontal?Right:Down;
    const int step=horizontal?1:int(b.n);
    unsigned first=at,last=at;
    // Stop at bends, junctions, start and goal. A patch never bridges a side exit.
    if((b.walls[at]&rails)==rails){
        while(!(b.walls[first]&before)){
            const int next=int(first)-step;
            if(next<0||unsigned(next)==b.goal||next==0||(b.walls[next]&rails)!=rails)break;
            first=unsigned(next);
        }
        while(!(b.walls[last]&after)){
            const unsigned next=last+step;
            if(next>=b.n*b.n||next==b.goal||(b.walls[next]&rails)!=rails)break;
            last=next;
        }
    }
    const unsigned start=horizontal?first%b.n:first/b.n,end=horizontal?last%b.n:last/b.n;
    return {first,last,first+(horizontal?0:MaxCells),float(b.origin()+int(start)*b.cell()+(start==0?3:0)),
            float(b.origin()+int(end+1)*b.cell()-3)};
}
constexpr float gapSquared(Rect a,Rect b){
    const float x=std::max(0.f,std::max(a.x-b.x-b.w,b.x-a.x-a.w));
    const float y=std::max(0.f,std::max(a.y-b.y-b.h,b.y-a.y-a.h));return x*x+y*y;
}
constexpr bool placeTraps(Board &b,unsigned level,uint32_t seed,unsigned attempt){
    b.trapCount=0;
    const unsigned rotation=(level+attempt)%4;
    for(unsigned slot=0;slot<targetTraps(level);++slot){
        // Two of each kind in opposite quarters; later density stages add one
        // at a time. Extras rotate between kinds instead of always favouring sand.
        const unsigned q=(slot+level)%4,round=slot/4;
        const auto kind=Hazard(((round==0?q:round==1?(q^3):(q+1)%4)+rotation)%4);
        Trap best{};float bestScore=-1.e20f;bool found=false;
        for(unsigned at=1;at<b.n*b.n;++at){
            if(at==b.goal)continue;
            if(quadrant(b.centerX(at),b.centerY(at))!=q)continue;
            bool occupied=false;for(unsigned i=0;i<b.trapCount;++i)if(b.traps[i].cell==at)occupied=true;
            if(occupied)continue;
            for(unsigned side:{unsigned(Up),unsigned(Right),unsigned(Down),unsigned(Left)}){
                if(!(b.walls[at]&side))continue;
                const bool horizontal=side==Up||side==Down;
                const int cell=b.cell(),x=b.origin()+int(at%b.n)*cell,y=b.origin()+int(at/b.n)*cell;
                const float road=b.roadWidth();const auto span=corridor(b,at,side);
                const float crossEdge=(horizontal?at<b.n:at%b.n==0)?3.f:0.f;
                const float alongRoad=road-((horizontal?at%b.n==0:at<b.n)?3.f:0.f);
                const float depth=road*((random(seed)&1)?.5f:2.f/3.f);
                const float minimum=alongRoad*.65f;
                const float maximum=std::max(minimum,(span.hi-span.lo)*(span.first==span.last?.95f:.72f));
                const float length=minimum+(maximum-minimum)*float(random(seed)%101)/100.f;
                const float wanted=horizontal?b.centerX(at):b.centerY(at);
                const float centre=clamp(wanted,span.lo+length*.5f,span.hi-length*.5f);
                Rect area{};
                if(side==Up)area={centre-length*.5f,float(y)+crossEdge,length,depth};
                if(side==Right)area={x+cell-3-depth,centre-length*.5f,depth,length};
                if(side==Down)area={centre-length*.5f,y+cell-3-depth,length,depth};
                if(side==Left)area={float(x)+crossEdge,centre-length*.5f,depth,length};
                const float cx=area.x+area.w*.5f,cy=area.y+area.h*.5f;
                if(quadrant(cx,cy)!=q)continue;
                const float core=hardTrap(kind)?std::min(depth,road-crossEdge-2*BallRadius-3):depth;
                if(core<=0)continue;
                float nearest=60000,same=60000;bool allowed=true;
                for(unsigned i=0;i<b.trapCount;++i){
                    const auto t=b.traps[i];const float dx=cx-t.area.x-t.area.w*.5f,dy=cy-t.area.y-t.area.h*.5f;
                    const float d2=dx*dx+dy*dy;nearest=std::min(nearest,d2);
                    if(t.kind==kind)same=std::min(same,d2);
                    // Never line a straight corridor with repetitions of one kind.
                    // Long patches also leave a full-ball gap for direction changes.
                    const bool shared=t.corridor==span.id;
                    const float gap=shared?2*BallRadius+4:5.f;
                    if((shared&&t.kind==kind)||gapSquared(t.area,area)<gap*gap){allowed=false;break;}
                    if(t.kind==kind&&d2<6400){allowed=false;break;}
                }
                const Rect start{b.centerX(0)-10,b.centerY(0)-10,20,20};
                const Rect goal{b.centerX(b.goal)-10,b.centerY(b.goal)-10,20,20};
                if(!allowed||gapSquared(area,start)<16||gapSquared(area,goal)<16)continue;
                const float tx=(q&1)?180.f:60.f,ty=(q&2)?180.f:60.f;
                const float score=std::min(nearest,10000.f)*2+std::min(same,30000.f)
                    -(cx-tx)*(cx-tx)-(cy-ty)*(cy-ty)+float(random(seed)%1000);
                if(!found||score>bestScore){
                    best={at,kind,area,(random(seed)&1)!=0,uint8_t(side),core,uint16_t(span.id),uint16_t(random(seed))};
                    bestScore=score;found=true;
                }
            }
        }
        if(!found)return false;
        b.traps[b.trapCount++]=best;
    }
    return true;
}
constexpr Board generate(unsigned level) {
    if(level>=Levels)level=Levels-1;
    Board b;b.n=dimension(level);b.material=Material(level%5);for(auto &w:b.walls)w=15;
    bool visited[MaxCells]{};unsigned stack[MaxCells]{},top=1;visited[0]=true;
    uint32_t seed=0x594d7a31u+level*0x9e3779b9u;
    constexpr int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
    constexpr unsigned bit[]={Up,Right,Down,Left},opposite[]={Down,Left,Up,Right};
    while(top) {
        const unsigned at=stack[top-1];unsigned choices[4]{},count=0;
        for(unsigned k=0;k<4;++k){const int x=int(at%b.n)+dx[k],y=int(at/b.n)+dy[k];
            if(x>=0&&x<int(b.n)&&y>=0&&y<int(b.n)&&!visited[y*b.n+x])choices[count++]=k;}
        if(!count){--top;continue;}
        const auto k=choices[random(seed)%count];const unsigned to=int(at)+dx[k]+dy[k]*int(b.n);
        b.walls[at]&=~bit[k];b.walls[to]&=~opposite[k];visited[to]=true;stack[top++]=to;
    }
    // Put the goal at the farthest reachable cell, so every level has a real route.
    unsigned queue[MaxCells]{},parent[MaxCells]{},head=0,tail=1;for(auto &v:visited)v=false;visited[0]=true;
    while(head<tail){const unsigned at=queue[head++];b.goal=at;
        for(unsigned k=0;k<4;++k)if(!(b.walls[at]&bit[k])) {
            const unsigned to=int(at)+dx[k]+dy[k]*int(b.n);
            if(!visited[to]){visited[to]=true;parent[to]=at;queue[tail++]=to;}
        }
    }
    for(unsigned at=b.goal;;at=parent[at]){b.route[at]=true;if(!at)break;}
    // Layout retries change only the decoration seed, not the maze or its route.
    for(unsigned attempt=0;attempt<12;++attempt)
        if(placeTraps(b,level,random(seed),attempt))break;
    return b;
}
constexpr bool circleRect(float x,float y,float radius,Rect r){
    float dx=x-clamp(x,r.x,r.x+r.w),dy=y-clamp(y,r.y,r.y+r.h);
    return dx*dx+dy*dy<radius*radius;
}
constexpr Rect hazardArea(const Trap &t){
    Rect r=t.area;if(!hardTrap(t.kind)||!t.coreDepth)return r;
    if(t.side==Up)r.h=t.coreDepth;
    if(t.side==Down){r.y+=r.h-t.coreDepth;r.h=t.coreDepth;}
    if(t.side==Left)r.w=t.coreDepth;
    if(t.side==Right){r.x+=r.w-t.coreDepth;r.w=t.coreDepth;}
    return r;
}
constexpr bool trapContour(const Trap &t,Rect r,float x,float y,float extra) {
    if(x<r.x-extra||y<r.y-extra||x>r.x+r.w+extra||y>r.y+r.h+extra)return false;
    // Pooled/collapsed outline, shared by drawing and physics. No hidden box.
    const bool alongX=t.side==Up||t.side==Down;
    const float u=alongX?x-r.x:y-r.y,v=alongX?y-r.y:x-r.x;
    const float length=alongX?r.w:r.h,depth=alongX?r.h:r.w;
    const float offset=t.rounded?.06f:-.06f;
    for(unsigned i=0;i<3;++i) {
        const float cu=length*(i==0?.29f:i==1?.52f:.76f);
        const float wobble=(float((t.shape>>(i*3))&7)-3.5f)*.008f;
        const float cv=depth*(i==0?.48f+offset+wobble:i==1?.5f:.53f-offset+wobble);
        const float a=length*(i==0?.29f:i==1?.33f:.24f)+extra;
        const float b=depth*(i==0?.38f:i==1?.5f:.36f)+extra;
        const float du=(u-cu)/a,dv=(v-cv)/b;
        if(du*du+dv*dv<1)return true;
    }
    return false;
}
constexpr bool touchesTrap(const Trap &t,float x,float y,float extra=0){return trapContour(t,t.area,x,y,extra);}
constexpr bool touchesHazard(const Trap &t,float x,float y,float extra=0){
    return touchesTrap(t,x,y,extra)&&trapContour(t,hazardArea(t),x,y,extra);
}
template<class F> constexpr void eachWall(const Board &b,F fn) {
    const int o=b.origin(),c=b.cell();
    fn(Rect{float(o),float(o),float(c*b.n),3});fn(Rect{float(o),float(o),3,float(c*b.n)});
    for(unsigned i=0;i<b.n*b.n;++i){const float x=o+int(i%b.n)*c,y=o+int(i/b.n)*c;
        if(b.walls[i]&Right)fn(Rect{x+c-3,y,3,float(c)});
        if(b.walls[i]&Down)fn(Rect{x,y+c-3,float(c),3});}
}
constexpr bool blocked(const Board &b,float x,float y,float radius=BallRadius) {
    bool hit=false;eachWall(b,[&](Rect r){if(circleRect(x,y,radius,r))hit=true;});
    for(unsigned i=0;i<b.trapCount;++i)if(b.traps[i].kind==Hazard::Bumper && touchesHazard(b.traps[i],x,y,radius))hit=true;
    return hit;
}
constexpr bool touchesGoal(const Board &b,float x,float y){
    const float dx=x-b.centerX(b.goal),dy=y-b.centerY(b.goal);
    const float reach=BallRadius+GoalRadius;
    return dx*dx+dy*dy<=reach*reach;
}
struct Ball {
    float x=0,y=0,vx=0,vy=0;
    float travel=0,impact=0; // Peak collision speed in the last step; audio only.
    unsigned falling=0;
    constexpr void reset(const Board &b){x=b.centerX(0);y=b.centerY(0);vx=vy=travel=impact=0;falling=0;}
    constexpr void stop(){vx=vy=0;}
    constexpr bool step(const Board &b,float ax,float ay) {
        impact=0;
        if(falling) {if(!--falling)reset(b);return false;}
        if(touchesGoal(b,x,y)){stop();return true;}
        // Two small substeps prevent tunnelling through 3px walls; axis separation
        // lets the ball slide along corners instead of becoming stuck diagonally.
        for(int i=0;i<2;++i){
            auto p=physics(b.material);
            for(unsigned k=0;k<b.trapCount;++k) {
                const auto trap=b.traps[k];
                if(!touchesHazard(trap,x,y,trap.kind==Hazard::Pit?BallRadius*.4f:0))continue;
                if(trap.kind==Hazard::Sand){p.drag=.78f;p.acceleration*=.8f;}
                else if(trap.kind==Hazard::Ice)p.drag=.99f;
                else if(trap.kind==Hazard::Pit) {stop();falling=24;return false;}
            }
            vx=clamp((vx+clamp(ax,-1,1)*p.acceleration)*p.drag,-p.maxSpeed,p.maxSpeed);
            vy=clamp((vy+clamp(ay,-1,1)*p.acceleration)*p.drag,-p.maxSpeed,p.maxSpeed);
            if(!blocked(b,x+vx,y)){x+=vx;travel+=vx;}else {impact=std::max(impact,vx<0?-vx:vx);vx=-vx*p.rebound;}
            if(!blocked(b,x,y+vy)){y+=vy;travel+=vy;}else {impact=std::max(impact,vy<0?-vy:vy);vy=-vy*p.rebound;}
            if(travel>1000||travel<-1000)travel=0;
            // Contact during either substep counts, even while sliding past.
            if(touchesGoal(b,x,y)){stop();return true;}
        }
        return false;
    }
};
} // namespace maze
