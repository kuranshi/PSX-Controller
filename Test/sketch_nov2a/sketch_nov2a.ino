/*
  UNO R4 双 PS2 摇杆 + A/B/X/Y 四按键测试（Minima / WiFi，无需第三方库）

  接线：       摇杆 1       摇杆 2
  X / VRx       A2            A3
  Y / VRy       A1            A0
  SW（L3/R3）   D2            D3
  VCC / +5V     5V            5V
  GND           GND           GND（两个模块与开发板共地）
  SW 是按压开关，不是第三个模拟轴；按下时接地，程序输出 SW=1。
  新增按键：A -> D4，B -> D5，X -> D6，Y -> D7，另一端均接 GND。
  六个按键统一由 Buttons.h / Buttons.cpp 处理，20ms 消抖，支持同时按下。
  A4/SDA、A5/SCL 预留给 I2C OLED；当前尚未添加屏幕驱动和显示逻辑。

  使用：选择实际 UNO R4 型号上传，串口监视器设为 115200。
  上电或复位时松开两个摇杆，程序在约 160ms 内自动校准四个轴的中心。
  如校准时碰动摇杆，请松手后按开发板 RESET 重新校准。
  rawX/rawY 是 0～1023 原始值；X/Y 是 -1000～1000 处理值。
  默认原始值增大为正方向，实际左右/上下方向取决于模块安装方向。
  默认输出 VOFA FireWater 数据：joystick:0,0,0,0,0,1,512,510,509,513,0,0,0,0
  VOFA 中选择串口和 FireWater 协议，添加波形图并绑定 Y 轴通道。
  通道 0～3：J1 X、J1 Y、J2 X、J2 Y（-1000～1000）。
  通道 4～5：J1 SW（L3）、J2 SW（R3）（按下为 1，松开为 0）。
  通道 6～9：J1 rawX、J1 rawY、J2 rawX、J2 rawY（0～1023）。
  通道 10～13：A、B、X、Y 四个按键（按下为 1，松开为 0）。
  VOFA_OUTPUT 改为 false 可恢复原来的 J1/J2 带标签文字输出。
  本程序仅采集并输出串口数据，后续可再接入电脑端手柄映射。

*/

#include <Arduino.h>
#include "Buttons.h"

// true：供 VOFA 的 FireWater 协议绘图；false：供串口助手人工阅读。
// 两种格式互斥，避免带标签的文字混入绘图数据帧。
const bool VOFA_OUTPUT = true;

// ====================【灵敏度调节】====================
// 死区：以校准中心为基准，正负两侧各忽略这么多个 ADC 单位。
// 调大（如 60）可减少回中漂移；调小（如 20）可响应更轻的推动。
// 范围 0～511；实际值应小于中心到任一端点的距离。
const int J1_DEADZONE = 40;
const int J2_DEADZONE = 40;

// 输出增益：死区外归一化后乘此值，输出始终限制在 -1000～1000。
// 1.0 为默认；1.5 更灵敏且会提早到满量程；0.7 更缓和且满推约为 700。
// 请使用正数。示例：只让第二个摇杆更灵敏，把 J2_GAIN 改为 1.5f。
const float J1_GAIN = 1.0f;
const float J2_GAIN = 1.0f;

// 轴反向：对应方向与预期相反时，把 false 改为 true。
const bool J1_INVERT_X = false;
const bool J1_INVERT_Y = false;
const bool J2_INVERT_X = false;
const bool J2_INVERT_Y = false;
// =====================================================

const int ADC_MAX = 1023;
const int AXIS_MAX = 1000;
const unsigned long SAMPLE_INTERVAL_MS = 5;
const unsigned long REPORT_INTERVAL_MS = 50;
const int CALIBRATION_SAMPLES = 32;

struct JoystickConfig {
  int xPin;
  int yPin;
  Buttons::Id swButton;
  int deadzone;
  float gain;
  bool invertX;
  bool invertY;
};

struct JoystickState {
  int centerX;
  int centerY;
  int rawX;
  int rawY;
  int x;
  int y;
};

const JoystickConfig JOYSTICKS[] = {
  {A2, A1, Buttons::L3, J1_DEADZONE, J1_GAIN, J1_INVERT_X, J1_INVERT_Y},
  {A3, A0, Buttons::R3, J2_DEADZONE, J2_GAIN, J2_INVERT_X, J2_INVERT_Y}
};
const size_t JOYSTICK_COUNT = sizeof(JOYSTICKS) / sizeof(JOYSTICKS[0]);
JoystickState states[JOYSTICK_COUNT] = {};
unsigned long lastSampleAt = 0;
unsigned long lastReportAt = 0;

// 显式声明，确保 Arduino 自动生成函数原型时能识别自定义结构体。
int processAxis(int raw, int center, int deadzone, float gain, bool invert);
void sampleJoystick(const JoystickConfig &config, JoystickState &state);
void printJoystick(const char *label, const JoystickConfig &config,
                   const JoystickState &state);
void printVofaFrame();
void calibrateCenters();

int processAxis(int raw, int center, int deadzone, float gain, bool invert)
{
  const int offset = raw - center;
  const int distance = offset < 0 ? -offset : offset;
  if (distance <= deadzone) {
    return 0;
  }

  // 正负两侧分别计算有效行程；扣除死区后从 0 连续增长到满量程。
  const int travel = (offset > 0 ? ADC_MAX - center : center) - deadzone;
  if (travel <= 0) {
    return 0;  // 防止异常校准中心或过大死区导致除以零。
  }
  const float scaled = (distance - deadzone) * (float)AXIS_MAX / travel * gain;
  // 先限幅再转整数，避免大增益导致整数转换越界。
  int value = scaled >= AXIS_MAX ? AXIS_MAX : (scaled > 0 ? (int)scaled : 0);
  if (offset < 0) {
    value = -value;
  }
  return invert ? -value : value;
}

void calibrateCenters()
{
  long sumsX[JOYSTICK_COUNT] = {};
  long sumsY[JOYSTICK_COUNT] = {};
  for (int sample = 0; sample < CALIBRATION_SAMPLES; ++sample) {
    for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
      sumsX[i] += analogRead(JOYSTICKS[i].xPin);
      sumsY[i] += analogRead(JOYSTICKS[i].yPin);
    }
    delay(SAMPLE_INTERVAL_MS);  // 仅启动校准阶段等待，正常采集使用 millis()。
  }
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    states[i].centerX = (sumsX[i] + CALIBRATION_SAMPLES / 2) / CALIBRATION_SAMPLES;
    states[i].centerY = (sumsY[i] + CALIBRATION_SAMPLES / 2) / CALIBRATION_SAMPLES;
  }
}

void sampleJoystick(const JoystickConfig &config, JoystickState &state)
{
  state.rawX = analogRead(config.xPin);
  state.rawY = analogRead(config.yPin);
  state.x = processAxis(state.rawX, state.centerX, config.deadzone,
                        config.gain, config.invertX);
  state.y = processAxis(state.rawY, state.centerY, config.deadzone,
                        config.gain, config.invertY);
}

void printJoystick(const char *label, const JoystickConfig &config,
                   const JoystickState &state)
{
  Serial.print(label);
  Serial.print(" rawX=");
  Serial.print(state.rawX);
  Serial.print(" rawY=");
  Serial.print(state.rawY);
  Serial.print(" X=");
  Serial.print(state.x);
  Serial.print(" Y=");
  Serial.print(state.y);
  Serial.print(" SW=");
  Serial.print(Buttons::isPressed(config.swButton) ? 1 : 0);
}

void printVofaFrame()
{
  // 一个采样帧包含两个摇杆的全部数据，通道顺序固定。
  const int channels[] = {
    states[0].x, states[0].y, states[1].x, states[1].y,
    Buttons::isPressed(JOYSTICKS[0].swButton) ? 1 : 0,
    Buttons::isPressed(JOYSTICKS[1].swButton) ? 1 : 0,
    states[0].rawX, states[0].rawY, states[1].rawX, states[1].rawY,
    Buttons::isPressed(Buttons::A) ? 1 : 0,
    Buttons::isPressed(Buttons::B) ? 1 : 0,
    Buttons::isPressed(Buttons::X) ? 1 : 0,
    Buttons::isPressed(Buttons::Y) ? 1 : 0
  };
  Serial.print("joystick:");
  for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
    if (i > 0) {
      Serial.print(',');
    }
    Serial.print(channels[i]);
  }
  Serial.println();  // FireWater 必须以换行结束一帧。
}

void setup()
{
  Serial.begin(115200);
  // 不等待串口连接，保证未打开监视器时程序也能正常采集。
  analogReadResolution(10);
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    pinMode(JOYSTICKS[i].xPin, INPUT);
    pinMode(JOYSTICKS[i].yPin, INPUT);
  }

  // A/B/X/Y、L3、R3 各自独立消抖，SW 引脚作为数字输入使用。
  Buttons::begin();
  calibrateCenters();
  const unsigned long now = millis();
  Buttons::update(now);
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    sampleJoystick(JOYSTICKS[i], states[i]);
  }
  lastSampleAt = now;
  lastReportAt = now;
}

void loop()
{
  const unsigned long now = millis();
  // 无符号时间差也能正确处理 millis() 计数回绕。
  if (now - lastSampleAt >= SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
      sampleJoystick(JOYSTICKS[i], states[i]);
    }
    Buttons::update(now);
  }

  if (now - lastReportAt >= REPORT_INTERVAL_MS) {
    lastReportAt = now;
    if (VOFA_OUTPUT) {
      printVofaFrame();
    } else {
      printJoystick("J1", JOYSTICKS[0], states[0]);
      Serial.print(" | ");
      printJoystick("J2", JOYSTICKS[1], states[1]);
      Serial.print(" | A=");
      Serial.print(Buttons::isPressed(Buttons::A) ? 1 : 0);
      Serial.print(" B=");
      Serial.print(Buttons::isPressed(Buttons::B) ? 1 : 0);
      Serial.print(" X=");
      Serial.print(Buttons::isPressed(Buttons::X) ? 1 : 0);
      Serial.print(" Y=");
      Serial.print(Buttons::isPressed(Buttons::Y) ? 1 : 0);
      Serial.println();
    }
  }
}
