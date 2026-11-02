/*
  UNO R4 双 PS2 摇杆测试（Minima / WiFi，无需第三方库）

  接线：       摇杆 1       摇杆 2
  X / VRx       A2            A3
  Y / VRy       A1            A4
  Z / SW        A0            A5
  VCC / +5V     5V            5V
  GND           GND           GND（两个模块与开发板共地）
  Z 是按压开关，不是第三个模拟轴；按下时接地，程序输出 Z=1。
  A4/A5 已用于摇杆，本接线不能同时占用它们连接 I2C 设备。

  使用：选择实际 UNO R4 型号上传，串口监视器设为 115200。
  上电或复位时松开两个摇杆，程序在约 160ms 内自动校准四个轴的中心。
  如校准时碰动摇杆，请松手后按开发板 RESET 重新校准。
  rawX/rawY 是 0～1023 原始值；X/Y 是 -1000～1000 处理值。
  默认原始值增大为正方向，实际左右/上下方向取决于模块安装方向。
  示例：J1 rawX=512 rawY=510 X=0 Y=0 Z=0 | J2 rawX=509 rawY=513 X=0 Y=0 Z=1
  本程序仅采集并输出串口数据，后续可再接入电脑端手柄映射。

*/

#include <Arduino.h>

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
  {A2, A1, A0, J1_DEADZONE, J1_GAIN, J1_INVERT_X, J1_INVERT_Y},
  {A3, A4, A5, J2_DEADZONE, J2_GAIN, J2_INVERT_X, J2_INVERT_Y}
};
const size_t JOYSTICK_COUNT = sizeof(JOYSTICKS) / sizeof(JOYSTICKS[0]);
JoystickState states[JOYSTICK_COUNT] = {};
unsigned long lastSampleAt = 0;
unsigned long lastReportAt = 0;

// 显式声明，确保 Arduino 自动生成函数原型时能识别自定义结构体。
int processAxis(int raw, int center, int deadzone, float gain, bool invert);
void sampleJoystick(const JoystickConfig &config, JoystickState &state,
                    unsigned long now);
void printJoystick(const char *label, const JoystickState &state);
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

void printJoystick(const char *label, const JoystickState &state)
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
  Serial.print(" Z=");
  Serial.print(state.pressed ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  // 不等待串口连接，保证未打开监视器时程序也能正常采集。
  analogReadResolution(10);
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    pinMode(JOYSTICKS[i].xPin, INPUT);
    pinMode(JOYSTICKS[i].yPin, INPUT);
    pinMode(JOYSTICKS[i].buttonPin, INPUT_PULLUP);
  }

  calibrateCenters();
  const unsigned long now = millis();
  for (size_t i = 0; i < JOYSTICK_COUNT; ++i) {
    sampleJoystick(JOYSTICKS[i], states[i], now);
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
      sampleJoystick(JOYSTICKS[i], states[i], now);
    }
  }

  if (now - lastReportAt >= REPORT_INTERVAL_MS) {
    lastReportAt = now;
    printJoystick("J1", states[0]);
    Serial.print(" | ");
    printJoystick("J2", states[1]);
    Serial.println();
  }
}
