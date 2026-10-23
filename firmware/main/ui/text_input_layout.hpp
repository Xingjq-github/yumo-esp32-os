#pragma once
namespace ui {
// Shared system keyboard and network field geometry.
struct TextInputLayout {
    static constexpr int Width=240, ScreenHeight=240, Gap=1;
    static constexpr int BaseRowHeight=40;
    static constexpr int RowHeight=(BaseRowHeight*4+2)/3; // round up 1/3 taller
    static constexpr int LeftWidth=(Width-Gap)/2;
    static constexpr int RightWidth=Width-Gap-LeftWidth;
    static constexpr int Height=2*RowHeight+Gap;
    static constexpr int Top=ScreenHeight-Height;
    static constexpr int NetworkFieldHeight=36;
    static constexpr int viewport(int current,int y) {
        const int available=Top-Gap-y;
        const int height=available<40?40:available;
        return current<height?current:height;
    }
};
} // namespace ui
