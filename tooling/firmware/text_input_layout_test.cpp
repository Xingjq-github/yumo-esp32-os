// Exercise production geometry, not a duplicate handwritten screen model.
#include "ui/text_input_layout.hpp"
using L=ui::TextInputLayout;
static_assert(L::Width==240 && L::ScreenHeight==240);
static_assert(L::BaseRowHeight==40 && L::RowHeight==54);
static_assert(L::RowHeight*3>=L::BaseRowHeight*4);
static_assert((L::RowHeight-1)*3<L::BaseRowHeight*4);
static_assert(L::LeftWidth==119 && L::RightWidth==120);
static_assert(L::LeftWidth+L::Gap+L::RightWidth==L::Width);
static_assert(L::RowHeight>=36 && L::LeftWidth>=36 && L::RightWidth>=36);
static_assert(L::Height==109 && L::Top==131);
static_assert(L::Top+L::RowHeight+L::Gap+L::RowHeight==L::ScreenHeight);
static_assert(L::NetworkFieldHeight==36);
static_assert(L::viewport(203,37)==93); // settings and vault scroll panels
static_assert(37+L::viewport(203,37)+L::Gap==L::Top);
static_assert(L::viewport(240,0)==130);
static_assert(L::viewport(80,37)==80); // do not enlarge a smaller viewport
static_assert(L::viewport(20,37)==20);
static_assert(L::viewport(203,200)==40); // retain minimum scroll interaction
// Both editable Wi-Fi fields fit above the unchanged two-row keyboard.
static_assert(2*L::NetworkFieldHeight+L::Gap<=L::viewport(203,37));
static_assert(L::NetworkFieldHeight-2-2*((L::NetworkFieldHeight-21-2)/2)>=21);
static_assert(L::NetworkFieldHeight-2-2*((L::NetworkFieldHeight-14-2)/2)>=14);
