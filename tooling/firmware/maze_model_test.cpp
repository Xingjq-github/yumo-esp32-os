#include "apps/maze_model.hpp"
#include <array>
using namespace maze;
// Validate every exit against its neighbour, all outer boundaries, tree size,
// reachability and physical clearance of every traversable cell-to-cell route.
constexpr bool connected(unsigned level){
    auto b=generate(level);b.trapCount=0; // Pixel flood-fill tests terrain detours separately.
    bool seen[MaxCells]{};unsigned q[MaxCells]{},head=0,tail=1,edges=0;seen[0]=true;
    constexpr int dx[]={0,1,0,-1},dy[]={-1,0,1,0};constexpr unsigned mask[]={Up,Right,Down,Left},opposite[]={Down,Left,Up,Right};
    while(head<tail){unsigned at=q[head++];if(blocked(b,b.centerX(at),b.centerY(at)))return false;
        for(unsigned k=0;k<4;++k){int x=int(at%b.n)+dx[k],y=int(at/b.n)+dy[k];
            if(x<0||y<0||x>=int(b.n)||y>=int(b.n)){if(!(b.walls[at]&mask[k]))return false;continue;}
            unsigned to=y*b.n+x;
            if(bool(b.walls[at]&mask[k])!=bool(b.walls[to]&opposite[k]))return false;
            if(b.walls[at]&mask[k])continue;
            ++edges;if(!seen[to]){seen[to]=true;q[tail++]=to;}
            for(int s=1;s<4;++s)if(blocked(b,b.centerX(at)+dx[k]*b.cell()*s/4.0f,b.centerY(at)+dy[k]*b.cell()*s/4.0f))return false;
        }
    }
    return tail==b.n*b.n&&edges==2*(tail-1)&&b.goal!=0&&seen[b.goal];
}
// Separate assertions keep compiler constexpr budgets bounded per level.
static_assert(connected(0));static_assert(connected(1));static_assert(connected(2));static_assert(connected(3));static_assert(connected(4));
static_assert(connected(5));static_assert(connected(6));static_assert(connected(7));static_assert(connected(8));static_assert(connected(9));
static_assert(connected(10));static_assert(connected(11));static_assert(connected(12));static_assert(connected(13));static_assert(connected(14));
static_assert(Levels==15&&Stages==5&&MaxCells==81);
constexpr bool fiveStages(){for(unsigned i=0;i<Levels;++i)if(generate(i).n!=5+i/3)return false;return dimension(99)==9;}
static_assert(fiveStages());
constexpr bool solidWalls(){
    Board b;for(auto &w:b.walls)w=15;Ball ball;ball.reset(b);
    for(int i=0;i<60;++i){ball.step(b,-1,-1);if(blocked(b,ball.x,ball.y))return false;}
    return ball.x>=b.origin()+9&&ball.y>=b.origin()+9;
}
static_assert(solidWalls());
static_assert(circleRect(1,1,2,{2,2,3,3})&&!circleRect(0,0,1,{2,2,3,3}));
constexpr auto allBoards(){std::array<Board,Levels> result{};for(unsigned i=0;i<Levels;++i)result[i]=generate(i);return result;}
constexpr auto boards=allBoards();
constexpr bool distinct(){for(unsigned a=0;a<Levels;++a)for(unsigned z=0;z<a;++z){const auto &x=boards[a],&y=boards[z];if(x.n!=y.n)continue;bool same=true;for(unsigned i=0;i<x.n*x.n;++i)if(x.walls[i]!=y.walls[i])same=false;if(same)return false;}return true;}
static_assert(distinct());
constexpr bool contactWins(){
    for(const auto &b:boards){
        const float x=b.centerX(b.goal),y=b.centerY(b.goal),reach=BallRadius+GoalRadius;
        if(!touchesGoal(b,x+reach,y)||!touchesGoal(b,x-reach,y)||
           !touchesGoal(b,x,y+reach)||!touchesGoal(b,x,y-reach))return false;
        if(touchesGoal(b,x+reach+.1f,y)||touchesGoal(b,x+reach,y+reach))return false;
        Ball ball;ball.x=x+reach;ball.y=y;ball.vx=1;ball.vy=1;
        if(!ball.step(b,1,1)||ball.vx||ball.vy)return false;
        // An adjacent cell across a wall cannot trigger the goal.
        for(unsigned i=0;i<b.n*b.n;++i)if(i!=b.goal&&touchesGoal(b,b.centerX(i),b.centerY(i)))return false;
    }
    return true;
}
static_assert(contactWins(),"The visible ball touching the goal edge wins, without waiting at its center");
constexpr bool movingContactWins(){
    Board b;for(auto &w:b.walls)w=0;b.goal=12;
    Ball ball;ball.x=b.centerX(b.goal)-BallRadius-GoalRadius-.5f;ball.y=b.centerY(b.goal);ball.vx=1;
    return ball.step(b,0,0)&&ball.vx==0&&ball.vy==0;
}
static_assert(movingContactWins());
constexpr bool safeTraps(unsigned level){
    const auto b=generate(level);if(b.trapCount!=targetTraps(level))return false;
    unsigned types[4]{},quarters[4]{};
    for(unsigned k=0;k<b.trapCount;++k){
        const auto t=b.traps[k];const auto r=t.area;
        ++types[unsigned(t.kind)];++quarters[quadrant(r.x+r.w/2,r.y+r.h/2)];
        if(t.cell==0||t.cell==b.goal||!(b.walls[t.cell]&t.side)||r.w<=0||r.h<=0||r.x<0||r.y<0||r.x+r.w>240||r.y+r.h>240)return false;
        for(unsigned j=0;j<k;++j)if(t.cell==b.traps[j].cell)return false;
        const bool horizontal=t.side==Up||t.side==Down;
        const float depth=horizontal?r.h:r.w,road=b.roadWidth();
        if(depth!=road*.5f&&depth!=road*(2.f/3.f))return false;
        if(t.kind==Hazard::Pit||t.kind==Hazard::Bumper){
            const float edge=(horizontal?t.cell<b.n:t.cell%b.n==0)?3:0;
            if(t.coreDepth<=0||t.coreDepth>depth||road-t.coreDepth-edge<2*BallRadius+3-.01f)return false;
        }
        const auto span=corridor(b,t.cell,t.side);
        const float length=horizontal?r.w:r.h,lo=horizontal?r.x:r.y;
        const float minimum=(road-((horizontal?t.cell%b.n==0:t.cell<b.n)?3:0))*.65f;
        if(length<minimum-.01f||lo<span.lo-.01f||lo+length>span.hi+.01f||length>span.hi-span.lo)return false;
        for(unsigned j=0;j<k;++j){const auto other=b.traps[j];
            if(t.corridor==other.corridor&&t.kind==other.kind)return false;
            const float gap=t.corridor==other.corridor?2*BallRadius+4:5;
            if(gapSquared(r,other.area)<gap*gap)return false;
        }
        if(touchesTrap(t,r.x,r.y)||touchesTrap(t,r.x+r.w,r.y+r.h))return false;
        if(!touchesTrap(t,r.x+r.w*.5f,r.y+r.h*.5f))return false;
    }
    for(unsigned i=0;i<4;++i)if(types[i]<2||types[i]>3||quarters[i]<b.trapCount/4||quarters[i]>(b.trapCount+3)/4)return false;
    return true;
}
static_assert(safeTraps(0));static_assert(safeTraps(1));static_assert(safeTraps(2));
static_assert(safeTraps(3));static_assert(safeTraps(4));static_assert(safeTraps(5));
static_assert(safeTraps(6));static_assert(safeTraps(7));static_assert(safeTraps(8));
static_assert(safeTraps(9));static_assert(safeTraps(10));static_assert(safeTraps(11));
static_assert(safeTraps(12));static_assert(safeTraps(13));static_assert(safeTraps(14));
