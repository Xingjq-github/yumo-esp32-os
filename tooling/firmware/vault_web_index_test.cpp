#include "core/vault_web_index.hpp"
#include "core/vault_list.hpp"
using I = vault::interaction::WebIndex;
using W = vault::interaction::ListWindow;
struct Metadata { std::string_view category, username; };
constexpr bool mixedGroups() {
  const std::array<Metadata, 8> entries{{
    {"web", "alice"}, {"work", "alice"}, {"web", "Bob"},
    {"web", "alice"}, {"web", "ALICE"}, {"web", ""},
    {"web", ""}, {"app", "Bob"}
  }};
  I index;index.build(entries);
  if (index.groups()!=4 || index.entry(0)!=5 || index.count(0)!=2 ||
      index.entry(1)!=4 || index.entry(2)!=0 || index.entry(2,1)!=3 ||
      index.entry(3)!=2 || index.groupFor(1)!=I::None ||
      index.groupFor(7)!=I::None || index.groupFor(999)!=I::None) return false;
  W accounts;
  for(size_t group=0;group<index.groups();++group)accounts.append(index.entry(group));
  if(accounts.count()!=4 || accounts.entry(2)!=0) return false;
  W purposes;
  const auto selected=index.groupFor(accounts.entry(2));
  for(size_t row=0;row<index.count(selected);++row)purposes.append(index.entry(selected,row));
  // The second purpose must open session entry 3, not list row 1.
  if(purposes.entry(1)!=3 || purposes.count()!=2) return false;
  index.clear();
  return index.groups()==0 && index.groupFor(0)==I::None && index.entry(0)==I::None;
}
constexpr bool largeGroup() {
  std::array<Metadata,256> entries{};
  for(auto &e:entries)e={"web","same"};
  I index;index.build(entries);
  if(index.groups()!=1 || index.count(0)!=256) return false;
  W list;
  for(size_t i=0;i<256;++i) {
    if(index.entry(0,i)!=i || index.groupFor(i)!=0) return false;
    list.append(index.entry(0,i));
  }
  if(list.slots()!=8 || list.entry(255)!=255 || list.first(99999)!=248) return false;
  const std::array<Metadata,1> replacement{{{"app","same"}}};
  index.build(replacement);
  return !index.groups() && !index.count(0) && index.entry(0)==I::None && index.groupFor(255)==I::None;
}
constexpr bool distinctGroups() {
  std::array<std::array<char,3>,256> names{};
  std::array<Metadata,256> entries{};
  for(size_t i=0;i<256;++i) {
    names[i][0]=char('A'+i/16);names[i][1]=char('A'+i%16);
    entries[255-i]={"web",std::string_view(names[i].data(),2)};
  }
  I index;index.build(entries);
  if(index.groups()!=256) return false;
  for(size_t i=0;i<256;++i)
    if(index.count(i)!=1 || index.entry(i)!=255-i || index.groupFor(255-i)!=i)return false;
  return index.entry(255,1)==I::None && index.count(256)==0;
}
static_assert(mixedGroups());
static_assert(largeGroup());
static_assert(distinctGroups());
static_assert(I::compareAccounts("User","user")<0);
static_assert(I::compareAccounts("z","aa")>0);
