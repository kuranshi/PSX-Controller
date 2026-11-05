#pragma once

namespace ControllerDisplay {

// 来自主程序的处理后状态；显示模块不直接读取摇杆或按键引脚。
struct Snapshot {
  int leftX;
  int leftY;
  int rightX;
  int rightY;
  bool l3;
  bool r3;
  bool a;
  bool b;
  bool x;
  bool y;
};

bool begin();                       // 探测并初始化 OLED；失败时返回 false。
bool available();                   // 初始化或传输失败后为 false，复位可重试。
void submit(const Snapshot &state);  // 仅保存最新状态，不改动正在发送的画面。
void update(unsigned long now);     // 每次 loop 调用，只发送一块图像数据。

}  // namespace ControllerDisplay
