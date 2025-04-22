#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;
const int MOTOR_PIN = 12;    // your chosen GPIO

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  // I2C for the MPU
  Wire.begin(21, 22);       // SDA = GPIO21, SCL = GPIO22

  // Motor output
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  // MPU init
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) delay(10);
  }
  Serial.println("MPU6050 Found!");

  // sensor config
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);
  delay(100);
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // check your flags
  bool trigger = false;
  if (a.acceleration.y > 9.0f)               trigger = true;
  if (fabs(g.gyro.x) > 0.5f)                  trigger = true;
  if (fabs(g.gyro.y) > 0.5f)                  trigger = true;
  if (fabs(g.gyro.z) > 0.5f)                  trigger = true;

  if (trigger) {
    Serial.println("⚡ Trigger! Running motor 10s on, then 10s off.");
    digitalWrite(MOTOR_PIN, HIGH);
    delay(10000);              // motor ON for 10 s
    digitalWrite(MOTOR_PIN, LOW);
    delay(10000);              // motor OFF for 10 s
  } else {
    // no trigger: keep motor off and poll again soon
    digitalWrite(MOTOR_PIN, LOW);
    delay(100);                // small debounce/poll delay
  }
}
