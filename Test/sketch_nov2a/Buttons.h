#pragma once

#include <Arduino.h>

// 四个独立按键：A -> D4，B -> D5，X -> D6，Y -> D7。
// 两个摇杆按压键：L3（摇杆 1 SW）-> D2，R3（摇杆 2 SW）-> D3。
// 与 .ino 放在同一 sketch 文件夹，Arduino 会自动编译 Buttons.cpp。
namespace Buttons {

enum Id : uint8_t { A, B, X, Y, L3, R3, COUNT };

void begin();
void update(unsigned long now);
bool isPressed(Id button);  // 消抖后的状态：true 表示按下。

}  // namespace Buttons
