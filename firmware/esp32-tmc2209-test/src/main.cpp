#include <Arduino.h>
#include <TMCStepper.h>

// UART wiring for TMC2209:
// GPIO17 -> PDN_UART
// GPIO16 -> PDN_UART through 1k
// GND     -> GND
// 3V3     -> VIO
//
// Motion wiring:
// GPIO32 -> ENN (active-low)
// GPIO12 -> STEP
// GPIO13 -> DIR

namespace {

constexpr uint32_t HOST_BAUD = 115200;
constexpr uint32_t DRIVER_BAUD = 115200;

constexpr int TMC_UART_RX_PIN = 17;
constexpr int TMC_UART_TX_PIN = 16;

constexpr int EN_PIN = 32;
constexpr int STEP_PIN = 12;
constexpr int DIR_PIN = 13;

constexpr float R_SENSE = 0.11f;
constexpr uint8_t DRIVER_ADDRESS = 0b00;  // MS1=GND, MS2=GND

HardwareSerial &tmcSerial = Serial2;
TMC2209Stepper driver(&tmcSerial, R_SENSE, DRIVER_ADDRESS);

void pulseSteps(uint32_t steps, bool dir_high, uint32_t delay_us) {
  digitalWrite(DIR_PIN, dir_high ? HIGH : LOW);
  for (uint32_t i = 0; i < steps; ++i) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(delay_us);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(delay_us);
  }
}

void printDriverState(const char *label) {
  const uint8_t ifcnt = driver.IFCNT();
  const uint32_t ioin = driver.IOIN();

  Serial.printf("[%s] IFCNT=%u IOIN=0x%08lX\n", label, static_cast<unsigned>(ifcnt), static_cast<unsigned long>(ioin));
}

void configureDriver() {
  driver.begin();
  driver.toff(3);
  driver.blank_time(24);
  driver.I_scale_analog(false);
  driver.rms_current(500);
  driver.microsteps(16);
  driver.en_spreadCycle(false);
  driver.pdn_disable(true);
}

}  // namespace

void setup() {
  Serial.begin(HOST_BAUD);
  delay(300);

  pinMode(EN_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);

  digitalWrite(EN_PIN, LOW);   // enable driver
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);

  tmcSerial.begin(DRIVER_BAUD, SERIAL_8N1, TMC_UART_RX_PIN, TMC_UART_TX_PIN);

  Serial.println();
  Serial.println("ESP32 TMC2209 UART test");
  Serial.printf("UART RX=%d TX=%d EN=%d STEP=%d DIR=%d ADDR=%u\n",
                TMC_UART_RX_PIN, TMC_UART_TX_PIN, EN_PIN, STEP_PIN, DIR_PIN, DRIVER_ADDRESS);

  configureDriver();

  printDriverState("boot");

  Serial.println("Writing test settings...");
  const uint8_t ifcnt_before = driver.IFCNT();
  driver.microsteps(8);
  const uint8_t ifcnt_after = driver.IFCNT();
  Serial.printf("IFCNT before=%u after=%u\n", static_cast<unsigned>(ifcnt_before), static_cast<unsigned>(ifcnt_after));

  printDriverState("after_write");
  Serial.println("If IFCNT changes and IOIN is non-zero, UART is working.");
  Serial.println("Type 's' for status or 'r' to rotate 400 steps forward/back.");
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }

  const int c = Serial.read();
  if (c == 's' || c == 'S') {
    printDriverState("manual_status");
    return;
  }

  if (c == 'r' || c == 'R') {
    Serial.println("Rotate forward 400 steps...");
    pulseSteps(400, true, 500);
    delay(300);
    Serial.println("Rotate backward 400 steps...");
    pulseSteps(400, false, 500);
    Serial.println("Done.");
    return;
  }
}

