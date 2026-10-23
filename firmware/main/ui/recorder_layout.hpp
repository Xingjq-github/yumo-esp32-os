#pragma once
namespace ui::recorder_layout {
struct Rect { int x,y,w,h; };
inline constexpr Rect Record{0,64,118,77}, Stop{119,64,121,40};
inline constexpr Rect Rewind{119,105,39,36}, Pause{159,105,40,36}, Forward{200,105,40,36};
inline constexpr Rect List{0,142,240,98};
inline constexpr int RowHeight=38, FileWidth=201, DeleteWidth=38, MeterGap=5;
constexpr bool inside(Rect r) { return r.x>=0&&r.y>=0&&r.x+r.w<=240&&r.y+r.h<=240; }
constexpr bool overlaps(Rect a,Rect b) {return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
struct Controls { bool record,stop,transport,navigate,files,remove; };
constexpr Controls controls(bool busy,bool recording,bool ready,bool ending,bool blocked) {
 if(blocked)return {};
 if(!busy)return {true,false,false,true,true,true};
 if(recording)return {ready&&!ending,!ending,false,true,false,false};
 return {!ending,!ending,ready&&!ending,true,false,!ending};
}
constexpr bool canDelete(const Controls &c,bool busy,bool currentFile) {
 return c.remove&&(!busy||currentFile);
}
static_assert(Record.w==118&&Record.h==77&&Stop.h==40);
static_assert(Record.y==Stop.y);
static_assert(List.y==Forward.y+Forward.h+1);
static_assert(Record.y+Record.h==Forward.y+Forward.h);
static_assert(Forward.x+Forward.w==240&&List.y+List.h==240);
} // namespace ui::recorder_layout
