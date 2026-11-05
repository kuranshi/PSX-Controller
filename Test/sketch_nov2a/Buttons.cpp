#include "Buttons.h"

namespace Buttons {
namespace {

// 【按键接线与参数】
// 独立开关：一端接对应 D 引脚，另一端接 GND；不用额外上拉电阻。
// 四脚轻触开关要连接按下时才导通的两组触点，不能连接同侧已短接的两脚。
// 如果是按键模块，信号 S 接 D 引脚，GND 共地，供电按模块要求连接。
// 默认按下输出 LOW；若模块按下输出 HIGH，请将 ACTIVE_LOW 改为 false。
// SW 按下时与模块 GND 导通，始终使用内部上拉、LOW 有效。
const int PINS[COUNT] = {4, 5, 6, 7, 2, 3};
const bool ACTIVE_LOW = true;
const bool ACTIVE_LOW_BY_BUTTON[COUNT] = {
  ACTIVE_LOW, ACTIVE_LOW, ACTIVE_LOW, ACTIVE_LOW, true, true
};
const unsigned long DEBOUNCE_MS = 20;

struct State {
  bool candidate;
  bool pressed;
  unsigned long changedAt;
};

State states[COUNT] = {};

bool readPressed(uint8_t index)
{
  return digitalRead(PINS[index]) == (ACTIVE_LOW_BY_BUTTON[index] ? LOW : HIGH);
}

}  // namespace

void begin()
{
  const unsigned long now = millis();
  for (uint8_t i = 0; i < COUNT; ++i) {
    // 高电平有效的模块必须自行提供稳定的松开电平，使用 INPUT。
    pinMode(PINS[i], ACTIVE_LOW_BY_BUTTON[i] ? INPUT_PULLUP : INPUT);
    states[i].candidate = readPressed(i);
    states[i].pressed = false;
    states[i].changedAt = now;
  }
}

void update(unsigned long now)
{
  for (uint8_t i = 0; i < COUNT; ++i) {
    const bool reading = readPressed(i);
    if (reading != states[i].candidate) {
      states[i].candidate = reading;
      states[i].changedAt = now;
    }
    if (now - states[i].changedAt >= DEBOUNCE_MS) {
      states[i].pressed = states[i].candidate;
    }
  }
}

bool isPressed(Id button)
{
  const uint8_t index = static_cast<uint8_t>(button);
  return index < COUNT && states[index].pressed;
}

}  // namespace Buttons
