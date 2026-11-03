#pragma once

#include <Arduino.h>

// 四个独立按键：A -> D4，B -> D5，X -> D6，Y -> D7。
// 与 .ino 放在同一 sketch 文件夹，Arduino 会自动编译 Buttons.cpp。
namespace Buttons {

enum Id : uint8_t { A, B, X, Y, COUNT };

void begin();
void update(unsigned long now);
bool isPressed(Id button);  // 消抖后的状态：true 表示按下。

}  // namespace Buttons
