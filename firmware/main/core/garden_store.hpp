#pragma once
#include "garden_model.hpp"
#include <string>
namespace core::garden::store {
struct Snapshot {State state;uint64_t revision=0;bool exists=false,recovered=false;};
// Call inside an SD Session; save also requires an exclusive mutation claim.
int load(const std::string &directory,Snapshot &out);
int save(const std::string &directory,const State &state,uint64_t expected,Snapshot &out);
}
