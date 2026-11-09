#pragma once

namespace Ultrasonic {

// HC-SR04：VCC -> 5V，GND 共地，Trig -> D9，Echo -> D8。
void begin();
void update();  // 每次 loop 调用，回波由 D8 中断捕获，不阻塞等待。
int distanceCm();  // 最近一次距离（cm）；-1 表示未测得或回波无效。
bool brakePressed();  // 消除阈值抖动后的刹车状态。

}  // namespace Ultrasonic
