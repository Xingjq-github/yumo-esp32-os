#include "core/vault_pattern.hpp"
using namespace vault::pattern;
constexpr bool skippedCells() {
  Stroke s; s.add(0); s.add(2); s.add(8);
  return s.count == 5 && s.cells[0] == 0 && s.cells[1] == 1 &&
         s.cells[2] == 2 && s.cells[3] == 5 && s.cells[4] == 8;
}
constexpr bool usedMidpoint() {
  Stroke s; s.add(4); s.add(0); s.add(8); s.add(0); s.add(4);
  return s.count == 3 && s.cells[2] == 8;
}
constexpr bool fastMotion() {
  Stroke s; s.begin(48,48); s.move(192,48); s.end(192,192);
  return s.count==5 && !s.down && s.cells[1]==1 && s.cells[3]==5 && s.acceptable();
}
constexpr bool restartCancels() {
  Stroke s; s.begin(48,48); s.move(192,192); s.clear(); s.end(192,48);
  if(s.down || s.count) return false;
  s.begin(120,120); s.end(120,120);
  return s.count==1 && !s.acceptable() && s.cells[0]==4;
}
static_assert(skippedCells() && usedMidpoint() && fastMotion() && restartCancels());
static_assert(valid("12369",5));
static_assert(valid("512369874",9));
static_assert(!valid("1396",4)); // missed unvisited midpoint
static_assert(!valid("1123",4) && !valid("1230",4) && !valid("123",3));
static_assert(!valid("1234567891",10) && !valid(nullptr,4));
static_assert(First-HitRadius>=0 && First+2*Pitch+HitRadius<Side);

#ifdef VAULT_PATTERN_HOST_TEST
#include <cassert>
#include <cstdio>
#include <chrono>
#include <random>
size_t accepted=0;
void enumerate(char *value,size_t count,unsigned used) {
  if(count>=Minimum && valid(value,count)) ++accepted;
  if(count==Maximum) return;
  for(unsigned cell=0;cell<9;++cell) if(!(used&(1u<<cell))) {
    value[count]=char('1'+cell);enumerate(value,count+1,used|(1u<<cell));
  }
}
int main() {
  char value[9]{}; enumerate(value,0,0); assert(accepted==389112);
  // Every two-node skip is exactly the standard midpoint rule.
  for(unsigned from=0;from<9;++from) for(unsigned to=0;to<9;++to) {
    Stroke s;s.add(from);s.add(to);
    const int mid=midpoint(from,to);
    assert(s.count==(from==to?1u:mid>=0?3u:2u));
    assert(s.cells[s.count-1]==to);
  }
  std::mt19937 random(0x594D5041);size_t samples=0;
  const auto started=std::chrono::steady_clock::now();
  for(unsigned trial=0;trial<20000;++trial) {
    Stroke s;s.begin(random()%240,random()%240);
    for(unsigned tick=0;tick<64;++tick) {s.move(random()%240,random()%240);++samples;}
    s.end(s.px,s.py);unsigned used=0;
    for(size_t i=0;i<s.count;++i){assert(!(used&(1u<<s.cells[i])));used|=1u<<s.cells[i];value[i]=char('1'+s.cells[i]);}
    assert(s.count<=9);if(s.acceptable())assert(valid(value,s.count));
    s.clear();assert(!s.down&&!s.count);
  }
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
  std::printf("{\"accepted_patterns\":%zu,\"random_strokes\":20000,\"motion_samples\":%zu,\"host_ms\":%lld,\"passed\":true}\n",accepted,samples,(long long)elapsed);
}
#endif
