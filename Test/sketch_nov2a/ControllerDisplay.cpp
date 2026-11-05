#include "ControllerDisplay.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <stdio.h>

namespace ControllerDisplay {
namespace {

// ====================【OLED 配置】====================
// SSD1306 128x64 四针 I2C 模块：SDA -> A4，SCL -> A5，GND 共地。
// VCC 请按模块标注供电。按顺序尝试以下两个常见的 7 位地址。
const uint8_t OLED_ADDRESSES[] = {0x3C, 0x3D};
// 安装方向：0 = 正常，2 = 旋转 180 度（倒装）。本布局使用横屏方向 0 或 2。
const uint8_t OLED_ROTATION = 2;
const unsigned long REFRESH_INTERVAL_MS = 200;  // 5 帧/秒，降低面包板总线负担。
const uint32_t I2C_CLOCK_HZ = 100000;            // 杜邦线接线优先使用标准速率。
const unsigned int I2C_TIMEOUT_US = 5000;       // 足够完成一块数据，异常及时退出。
const size_t CHUNK_BYTES = 16;
// ====================================================

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const size_t FRAME_BYTES = SCREEN_WIDTH * SCREEN_HEIGHT / 8;
Adafruit_SSD1306 screen(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1,
                       I2C_CLOCK_HZ, I2C_CLOCK_HZ);
Snapshot latest = {};
bool online = false;
bool hasFrame = false;
bool sending = false;
uint8_t address = 0;
size_t offset = 0;
unsigned long lastFrameAt = 0;

bool busIdle()
{
  // 没有其他主设备时，传输开始前 SDA/SCL 应均为高。
  // 线路被拉低时不启动下一次底层发送，避免持续撞击故障总线。
  return digitalRead(A4) == HIGH && digitalRead(A5) == HIGH;
}

void stopDisplay()
{
  online = false;
  sending = false;
  Wire.end();  // 关闭显示总线及其中断；恢复接线后复位重新初始化。
}

int clampAxis(int value)
{
  return value < -1000 ? -1000 : (value > 1000 ? 1000 : value);
}

void drawStick(int origin, const char *label, int x, int y)
{
  x = clampAxis(x);
  y = clampAxis(y);
  screen.setTextColor(SSD1306_WHITE);
  screen.setCursor(origin, 0);
  screen.print(label);

  // 25x25 位置框；圆点半径 2，满推时仍留在边框内。
  screen.drawRect(origin + 1, 11, 25, 25, SSD1306_WHITE);
  screen.drawFastHLine(origin + 11, 23, 5, SSD1306_WHITE);
  screen.drawFastVLine(origin + 13, 21, 5, SSD1306_WHITE);
  screen.fillCircle(origin + 13 + x * 8 / 1000,
                    23 + y * 8 / 1000, 2, SSD1306_WHITE);

  // 固定六个字符的区域：最坏情况为 X-1000，不越过屏幕或左右分区。
  char text[7];
  snprintf(text, sizeof(text), "X%+5d", x);
  screen.setCursor(origin + 28, 12);
  screen.print(text);
  snprintf(text, sizeof(text), "Y%+5d", y);
  screen.setCursor(origin + 28, 24);
  screen.print(text);
}

void drawButton(int x, int y, int width, const char *label, bool pressed)
{
  if (pressed) {
    screen.fillRect(x, y, width, 10, SSD1306_WHITE);
    screen.setTextColor(SSD1306_BLACK);
  } else {
    screen.drawRect(x, y, width, 10, SSD1306_WHITE);
    screen.setTextColor(SSD1306_WHITE);
  }
  const int labelWidth = label[1] == '\0' ? 6 : 12;
  screen.setCursor(x + (width - labelWidth) / 2, y + 1);
  screen.print(label);
}

void render()
{
  screen.clearDisplay();
  screen.setTextSize(1);
  screen.setTextWrap(false);
  drawStick(0, "LEFT", latest.leftX, latest.leftY);
  drawStick(64, "RIGHT", latest.rightX, latest.rightY);
  screen.drawFastHLine(0, 38, SCREEN_WIDTH, SSD1306_WHITE);
  drawButton(0, 42, 31, "A", latest.a);
  drawButton(32, 42, 31, "B", latest.b);
  drawButton(64, 42, 31, "X", latest.x);
  drawButton(96, 42, 31, "Y", latest.y);
  drawButton(0, 54, 63, "L3", latest.l3);
  drawButton(64, 54, 63, "R3", latest.r3);
}

bool sendWindow()
{
  if (!busIdle()) {
    return false;
  }
  // SSD1306 水平寻址：后续数据自动从第 0 列第 0 页写到第 127 列第 7 页。
  const uint8_t commands[] = {SSD1306_COLUMNADDR, 0, SCREEN_WIDTH - 1,
                              SSD1306_PAGEADDR, 0, SCREEN_HEIGHT / 8 - 1};
  Wire.beginTransmission(address);
  const size_t controlWritten = Wire.write(static_cast<uint8_t>(0x00));
  const size_t written = Wire.write(commands, sizeof(commands));
  const uint8_t result = Wire.endTransmission();
  return controlWritten == 1 && written == sizeof(commands) && result == 0;
}

bool sendChunk()
{
  if (!busIdle()) {
    return false;
  }
  const size_t remaining = FRAME_BYTES - offset;
  const size_t count = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
  Wire.beginTransmission(address);
  const size_t controlWritten = Wire.write(static_cast<uint8_t>(0x40));
  const size_t written = Wire.write(screen.getBuffer() + offset, count);
  const uint8_t result = Wire.endTransmission();
  if (controlWritten != 1 || written != count || result != 0) {
    return false;
  }
  offset += count;
  sending = offset < FRAME_BYTES;
  return true;
}

}  // namespace

bool begin()
{
  online = false;
  hasFrame = false;
  sending = false;
  offset = 0;
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setWireTimeout(I2C_TIMEOUT_US);
  if (!busIdle()) {
    stopDisplay();
    return false;
  }
  bool found = false;
  for (size_t i = 0; i < sizeof(OLED_ADDRESSES) / sizeof(OLED_ADDRESSES[0]); ++i) {
    Wire.beginTransmission(OLED_ADDRESSES[i]);
    if (Wire.endTransmission() == 0) {
      address = OLED_ADDRESSES[i];
      found = true;
      break;
    }
  }
  if (!found || !screen.begin(SSD1306_SWITCHCAPVCC, address, false, false)) {
    stopDisplay();
    return false;
  }
  screen.setRotation(OLED_ROTATION);
  // 库的 begin() 返回值只验证缓冲区分配；另行确认屏幕仍应答。
  Wire.beginTransmission(address);
  online = Wire.endTransmission() == 0;
  if (!online) {
    stopDisplay();
  }
  return online;
}

bool available()
{
  return online;
}

void submit(const Snapshot &state)
{
  latest = state;
}

void update(unsigned long now)
{
  if (!online) {
    return;
  }
  if (!sending) {
    if (hasFrame && now - lastFrameAt < REFRESH_INTERVAL_MS) {
      return;
    }
    // 只在上一帧发送完成后绘图；发送过程中 submit() 只更新 latest。
    render();
    offset = 0;
    hasFrame = true;
    lastFrameAt = now;
    sending = true;
    if (!sendWindow()) {
      stopDisplay();
      return;
    }
  }
  // 不调用 screen.display() 整屏阻塞发送；每次 loop 最多发 16 字节图像。
  if (!sendChunk()) {
    stopDisplay();
  }
}

}  // namespace ControllerDisplay
