#include <Arduino.h>
#include "Ultrasonic.h"

namespace Ultrasonic {
namespace {

const int TRIG_PIN = 9;
// UNO R4 的 D8 支持外部中断，D9 不支持；不要直接对调两根信号线。
const int ECHO_PIN = 8;
const int BRAKE_ON_CM = 30;
const int BRAKE_OFF_CM = 35;  // 释放阈值要大于触发阈值。
const uint8_t REQUIRED_NEAR_SAMPLES = 2;
const unsigned long MEASURE_INTERVAL_US = 60000;
const unsigned long ECHO_TIMEOUT_US = 25000;
const unsigned long MIN_ECHO_US = 116;    // HC-SR04 约 2cm 的最小测距。
const unsigned long MAX_ECHO_US = 23200;  // 约 400cm。
static_assert(BRAKE_ON_CM >= 2 && BRAKE_OFF_CM > BRAKE_ON_CM &&
              BRAKE_OFF_CM <= 400, "Invalid brake distance thresholds");
static_assert(REQUIRED_NEAR_SAMPLES > 0, "At least one near sample is required");

enum EchoPhase : uint8_t { IDLE, WAIT_RISE, WAIT_FALL, READY };
volatile EchoPhase echoPhase = IDLE;
volatile unsigned long echoRiseAt = 0;
volatile unsigned long echoWidth = 0;
unsigned long lastTriggerAt = 0;
bool measuring = false;
bool hasTriggered = false;
int lastDistance = -1;
bool brake = false;
uint8_t nearSamples = 0;

void onEchoChange()
{
  const unsigned long now = micros();
  const bool high = digitalRead(ECHO_PIN) == HIGH;
  if (echoPhase == WAIT_RISE && high) {
    echoRiseAt = now;
    echoPhase = WAIT_FALL;
  } else if (echoPhase == WAIT_FALL && !high) {
    echoWidth = now - echoRiseAt;
    echoPhase = READY;
  }
}

void acceptMeasurement(unsigned long width)
{
  if (width < MIN_ECHO_US || width > MAX_ECHO_US) {
    lastDistance = -1;
    brake = false;
    nearSamples = 0;
    return;
  }
  lastDistance = static_cast<int>((width + 29UL) / 58UL);
  if (lastDistance <= BRAKE_ON_CM) {
    if (nearSamples < REQUIRED_NEAR_SAMPLES) {
      ++nearSamples;
    }
    if (nearSamples >= REQUIRED_NEAR_SAMPLES) {
      brake = true;
    }
  } else {
    nearSamples = 0;
    // 30～35cm 之间保持原状态；超出释放阈值时松开刹车。
    if (lastDistance >= BRAKE_OFF_CM) {
      brake = false;
    }
  }
}

}  // namespace

void begin()
{
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  noInterrupts();
  echoPhase = IDLE;
  echoRiseAt = 0;
  echoWidth = 0;
  interrupts();
  measuring = false;
  hasTriggered = false;
  lastDistance = -1;
  brake = false;
  nearSamples = 0;
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), onEchoChange, CHANGE);
}

void update()
{
  const unsigned long now = micros();
  if (measuring) {
    unsigned long width = 0;
    bool finished = false;
    // 在短临界区中读取 ISR 状态并结束本次测量，避免回波与超时竞争。
    noInterrupts();
    if (echoPhase == READY) {
      width = echoWidth;
      finished = true;
    } else if (now - lastTriggerAt >= ECHO_TIMEOUT_US) {
      finished = true;
    }
    if (finished) {
      echoPhase = IDLE;
    }
    interrupts();
    if (!finished) {
      return;
    }
    measuring = false;
    acceptMeasurement(width);  // 超时按无效回波处理，释放刹车。
  }

  if (hasTriggered && now - lastTriggerAt < MEASURE_INTERVAL_US) {
    return;
  }
  lastTriggerAt = now;
  hasTriggered = true;
  measuring = true;
  noInterrupts();
  echoPhase = WAIT_RISE;
  interrupts();
  // 仅触发脉冲等待 12us；不使用 pulseIn() 等待毫秒级回波。
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
}

int distanceCm()
{
  return lastDistance;
}

bool brakePressed()
{
  return brake;
}

}  // namespace Ultrasonic
