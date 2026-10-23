#pragma once
namespace ui {
class BootAnimation {
public:
    static void start(); // app_main only, once per boot
    static void stop();
};
}
