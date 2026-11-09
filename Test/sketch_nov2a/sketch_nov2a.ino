/*
  UNO R4 双 PS2 摇杆 + A/B/X/Y 四按键 + SSD1306 OLED 状态显示
  OLED 依赖 Adafruit SSD1306、Adafruit GFX，库管理器安装时同时安装依赖库。

  接线：       摇杆 1       摇杆 2
  X / VRx       A2            A3
  Y / VRy       A1            A0
  Z / SW        D2 (L3)       D3 (R3)
  VCC / +5V     5V            5V
  GND           GND           GND（两个模块与开发板共地）
  Z 是按压开关，不是第三个模拟轴；按下时接地，程序输出 Z=1。
  新增按键：A -> D4，B -> D5，X -> D6，Y -> D7，另一端均接 GND。
  按键逻辑独立放在 Buttons.h / Buttons.cpp，20ms 消抖，支持同时按下。
  OLED：SDA -> A4，SCL -> A5，GND 共地，VCC 按模块额定电压连接。
  ControllerDisplay.h / .cpp 独立负责显示，默认依次探测 0x3C、0x3D。
  HC-SR04：VCC -> 5V，GND 共地，Trig -> D9，Echo -> D8（外部中断）。

  使用：选择实际 UNO R4 型号上传，串口监视器设为 115200。
  上电或复位时松开两个摇杆，程序在约 160ms 内自动校准四个轴的中心。
  如校准时碰动摇杆，请松手后按开发板 RESET 重新校准。
  rawX/rawY 是 0～1023 原始值；X/Y 是 -1000～1000 处理值。
  默认原始值增大为正方向，实际左右/上下方向取决于模块安装方向。
  默认输出 VOFA FireWater 数据：joystick:0,0,0,0,0,1,512,510,509,513,0,0,0,0,-1,0
  VOFA 中选择串口和 FireWater 协议，添加波形图并绑定 Y 轴通道。
  通道 0～3：J1 X、J1 Y、J2 X、J2 Y（-1000～1000）。
  通道 4～5：J1 Z、J2 Z（按下为 1，松开为 0）。
  通道 6～9：J1 rawX、J1 rawY、J2 rawX、J2 rawY（0～1023）。
  通道 10～13：A、B、X、Y 四个按键（按下为 1，松开为 0）。
  通道 14～15：超声波距离（cm，无效 -1）、刹车状态（0/1）。
  VOFA_OUTPUT 改为 false 可恢复原来的 J1/J2 带标签文字输出。
  本程序仅采集并输出串口数据，后续可再接入电脑端手柄映射。

*/

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "Buttons.h"
#include "ControllerDisplay.h"
#include "Ultrasonic.h"

// true：供 VOFA 的 FireWater 协议绘图；false：供串口助手人工阅读。
// 两种格式互斥，避免带标签的文字混入绘图数据帧。
const bool VOFA_OUTPUT = true;
// 死机排查：改为 false 并重新上传，可完全跳过 OLED/I2C 初始化和刷新。
const bool OLED_ENABLED = true;

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
const unsigned long DEBOUNCE_MS = 20;
const int CALIBRATION_SAMPLES = 32;

struct JoystickConfig {
  int xPin;
  int yPin;
  int buttonPin;
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
  bool buttonCandidate;
  bool pressed;
  unsigned long buttonChangedAt;
};

const JoystickConfig JOYSTICKS[] = {
  {A2, A1, 2, J1_DEADZONE, J1_GAIN, J1_INVERT_X, J1_INVERT_Y},
  {A3, A0, 3, J2_DEADZONE, J2_GAIN, J2_INVERT_X, J2_INVERT_Y}
};
const size_t JOYSTICK_COUNT = sizeof(JOYSTICKS) / sizeof(JOYSTICKS[0]);
JoystickState states[JOYSTICK_COUNT] = {};
unsigned long lastSampleAt = 0;
unsigned long lastReportAt = 0;
// 单帧串口缓冲：主循环分块发送，不要求 USB 缓冲区一次装下整行。
char serialFrame[256];
size_t serialFrameLength = 0;
size_t serialFrameOffset = 0;
unsigned long lastSerialChunkAt = 0;

// 显式声明，确保 Arduino 自动生成函数原型时能识别自定义结构体。
int processAxis(int raw, int center, int deadzone, float gain, bool invert);
void sampleJoystick(const JoystickConfig &config, JoystickState &state,
                    unsigned long now);
void printVofaFrame();
void printTextFrame();
void sendSerialFrame(const char *frame, int length, size_t capacity);
void serviceSerialOutput();
void calibrateCenters();
void submitDisplayState();

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

void sampleJoystick(const JoystickConfig &config, JoystickState &state,
                    unsigned long now)
{
  state.rawX = analogRead(config.xPin);
  state.rawY = analogRead(config.yPin);
  state.x = processAxis(state.rawX, state.centerX, config.deadzone,
                        config.gain, config.invertX);
  state.y = processAxis(state.rawY, state.centerY, config.deadzone,
                        config.gain, config.invertY);

  // 两个按钮各自消抖，不阻塞轴采集，也不抢占摇杆方向输入。
  const bool reading = digitalRead(config.buttonPin) == LOW;
  if (reading != state.buttonCandidate) {
    state.buttonCandidate = reading;
    state.buttonChangedAt = now;
  }
  if (now - state.buttonChangedAt >= DEBOUNCE_MS) {
    state.pressed = state.buttonCandidate;
  }
}

void sendSerialFrame(const char *frame, int length, size_t capacity)
{
  // 已开始发送的帧必须先发完，避免两个采样帧拼接成一行。
  // 尚未发出任何字节时，可以直接替换为最新状态；最多保留一帧。
  if (length <= 0 || static_cast<size_t>(length) >= capacity ||
      static_cast<size_t>(length) >= sizeof(serialFrame) ||
      (serialFrameOffset > 0 && serialFrameOffset < serialFrameLength)) {
    return;
  }
  memcpy(serialFrame, frame, static_cast<size_t>(length));
  serialFrameLength = static_cast<size_t>(length);
  serialFrameOffset = 0;
}

void serviceSerialOutput()
{
  // Minima 的 bool 转换还会服务 USB 事件；仅检查一次，不循环等待连接。
  if (!Serial) {
    serialFrameLength = 0;
    serialFrameOffset = 0;
    return;
  }
  if (serialFrameOffset >= serialFrameLength) {
    return;
  }
#if defined(ARDUINO_UNOR4_WIFI) && defined(NO_USB)
  // WiFi 默认 Serial 是 UART->ESP32 USB 桥，availableForWrite() 未实现，恒为 0。
  // 不用该值判断能否发送。115200 波特率下每 1ms 最多发 8 字节，
  // 单次正常发送耗时约 0.7ms，并为上一块的最后一个字节留出发送时间。
  const unsigned long nowUs = micros();
  if (nowUs - lastSerialChunkAt < 1000) {
    return;
  }
  lastSerialChunkAt = nowUs;
  size_t count = serialFrameLength - serialFrameOffset;
  if (count > 8) {
    count = 8;
  }
  // 选择 UART 的缓冲区重载，避免退化为连续的单字节 write 调用。
  const size_t sent = Serial.write(
    reinterpret_cast<uint8_t *>(serialFrame + serialFrameOffset), count);
#else
  const int space = Serial.availableForWrite();
  if (space <= 0) {
    return;
  }
  size_t count = serialFrameLength - serialFrameOffset;
  if (count > 32) {
    count = 32;
  }
  if (count > static_cast<size_t>(space)) {
    count = static_cast<size_t>(space);
  }
  const size_t sent = Serial.write(
    reinterpret_cast<const uint8_t *>(serialFrame + serialFrameOffset), count);
#endif
  serialFrameOffset += sent;
  if (serialFrameOffset >= serialFrameLength) {
    serialFrameLength = 0;
    serialFrameOffset = 0;
  }
}

void printVofaFrame()
{
  // 原 14 通道顺序不变，追加距离和刹车状态。
  char frame[256];
  const int length = snprintf(frame, sizeof(frame),
    "joystick:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
    states[0].x, states[0].y, states[1].x, states[1].y,
    states[0].pressed ? 1 : 0, states[1].pressed ? 1 : 0,
    states[0].rawX, states[0].rawY, states[1].rawX, states[1].rawY,
    Buttons::isPressed(Buttons::A) ? 1 : 0,
    Buttons::isPressed(Buttons::B) ? 1 : 0,
    Buttons::isPressed(Buttons::X) ? 1 : 0,
    Buttons::isPressed(Buttons::Y) ? 1 : 0,
    Ultrasonic::distanceCm(), Ultrasonic::brakePressed() ? 1 : 0);
  sendSerialFrame(frame, length, sizeof(frame));
}

void printTextFrame()
{
  char frame[256];
  const int length = snprintf(frame, sizeof(frame),
    "J1 rawX=%d rawY=%d X=%d Y=%d Z=%d | "
    "J2 rawX=%d rawY=%d X=%d Y=%d Z=%d | A=%d B=%d X=%d Y=%d | DistanceCm=%d Brake=%d\r\n",
    states[0].rawX, states[0].rawY, states[0].x, states[0].y,
    states[0].pressed ? 1 : 0,
    states[1].rawX, states[1].rawY, states[1].x, states[1].y,
    states[1].pressed ? 1 : 0,
    Buttons::isPressed(Buttons::A) ? 1 : 0,
    Buttons::isPressed(Buttons::B) ? 1 : 0,
    Buttons::isPressed(Buttons::X) ? 1 : 0,
    Buttons::isPressed(Buttons::Y) ? 1 : 0,
    Ultrasonic::distanceCm(), Ultrasonic::brakePressed() ? 1 : 0);
  sendSerialFrame(frame, length, sizeof(frame));
}

void submitDisplayState()
{
  const ControllerDisplay::Snapshot snapshot = {
    states[0].x, states[0].y, states[1].x, states[1].y,
    states[0].pressed, states[1].pressed,
    Buttons::isPressed(Buttons::A), Buttons::isPressed(Buttons::B),
    Buttons::isPressed(Buttons::X), Buttons::isPressed(Buttons::Y)
  };
  ControllerDisplay::submit(snapshot);
}

void setup()
{
  Serial.begin(115200);
#if !defined(NO_USB)
  // 仅原生 USB 目标：允许未拉高 DTR 的串口助手接收数据。
  (void)Serial.dtr();
#endif
  // 不使用 while(!Serial)，不开串口也能操作手柄与 OLED。
  // 不等待串口连接，保证未打开监视器时程序也能正常采集。
  analogReadResolution(10);
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    pinMode(JOYSTICKS[i].xPin, INPUT);
    pinMode(JOYSTICKS[i].yPin, INPUT);
    pinMode(JOYSTICKS[i].buttonPin, INPUT_PULLUP);
  }

  calibrateCenters();
  // OLED 不存在或初始化失败时不阻塞等待，不向 FireWater 数据插入提示文字。
  if (OLED_ENABLED) {
    ControllerDisplay::begin();
  }
  Buttons::begin();
  Ultrasonic::begin();
  const unsigned long now = millis();
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    sampleJoystick(JOYSTICKS[i], states[i], now);
  }
  lastSampleAt = now;
  lastReportAt = now;
  submitDisplayState();
}

void loop()
{
  Ultrasonic::update();
  const unsigned long now = millis();
  // 无符号时间差也能正确处理 millis() 计数回绕。
  if (now - lastSampleAt >= SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
      sampleJoystick(JOYSTICKS[i], states[i], now);
    }
    Buttons::update(now);
    submitDisplayState();
  }

  if (now - lastReportAt >= REPORT_INTERVAL_MS) {
    lastReportAt = now;
    if (VOFA_OUTPUT) {
      printVofaFrame();
    } else {
      printTextFrame();
    }
  }
  serviceSerialOutput();
  if (OLED_ENABLED) {
    ControllerDisplay::update(millis());
  }
}
