#pragma once
#include <cstdint>

namespace apps::rps {
// Choice order on both sides: rock, scissors, paper. Winner: blue=0, red=1,
// draw=2, pending=-1. One round is settled exactly once.
constexpr int winner(unsigned blue,unsigned red) {
    return blue>2 || red>2?-1:blue==red?2:(blue+1)%3==red?0:1;
}
struct Match {
    enum class State { Idle, Running, Done };
    State state[2]={State::Idle,State::Idle};
    unsigned choice[2]={0,0},score[2]={0,0};
    int result=-1;
    constexpr void clearRound() {
        state[0]=state[1]=State::Idle;choice[0]=choice[1]=0;result=-1;
    }
    constexpr bool running() const {
        return state[0]==State::Running || state[1]==State::Running;
    }
    constexpr bool begin(unsigned side) {
        if(side>1) return false;
        if(result>=0) clearRound();
        if(state[side]!=State::Idle) return false;
        state[side]=State::Running;return true;
    }
    constexpr bool complete(unsigned side,unsigned hand) {
        if(side>1 || hand>2 || state[side]!=State::Running) return false;
        choice[side]=hand;state[side]=State::Done;
        if(state[0]==State::Done && state[1]==State::Done && result<0) {
            result=winner(choice[0],choice[1]);
            if(result<2 && score[result]<9999) ++score[result];
            return true;
        }
        return false;
    }
};
}
