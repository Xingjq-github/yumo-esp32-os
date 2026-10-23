#include "../../firmware/main/apps/rps_match.hpp"
using namespace apps::rps;
constexpr bool checkRounds() {
    for(unsigned b=0;b<3;++b) for(unsigned r=0;r<3;++r) {
        Match m;
        if(!m.begin(0) || m.begin(0) || m.complete(0,b) || m.result!=-1) return false;
        if(m.begin(0) || !m.begin(1) || !m.complete(1,r)) return false;
        const int expected=b==r?2:(b+1)%3==r?0:1;
        if(m.result!=expected || m.score[0]!=(expected==0) || m.score[1]!=(expected==1)) return false;
        if(m.complete(1,r) || m.complete(0,b)) return false;
        if(!m.begin(1) || m.state[0]!=Match::State::Idle || m.result!=-1) return false;
        m.clearRound();if(m.running() || m.result!=-1) return false;
    }
    Match m;m.score[0]=9999;m.begin(1);m.begin(0);m.complete(1,1);m.complete(0,0);
    return m.score[0]==9999 && !m.begin(2) && winner(4,0)==-1;
}
static_assert(checkRounds(),"Independent sides, no duplicate award, cancel and capped scores");
