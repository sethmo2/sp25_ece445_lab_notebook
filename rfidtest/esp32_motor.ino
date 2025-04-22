#include <SPI.h>
#include <MFRC522.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// -------------------- RFID Setup --------------------
#define RST_PIN   22   // MFRC522 RST pin
#define SS_PIN     5   // MFRC522 SDA/CS pin

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

const int blockNumber = 4;
byte writeData[16];
byte readBuffer[18];
char message[17] = "Yay it works";
bool writeFlag = false;

// -------------------- Motor Control --------------------
// Logic: HIGH = motor ON, LOW = motor OFF
#define MOTOR_PIN   25
unsigned long motorStartTime = 0;
const unsigned long motorRunDuration = 3000;
bool motorRunning = false;

// Track card presence to fire only on new swipe
bool cardWasPresent = false;

// -------------------- BLE Setup --------------------
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLECharacteristic* pCharacteristic;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    Serial.println("BLE client connected!");
  }
  void onDisconnect(BLEServer* pServer) override {
    Serial.println("BLE client disconnected!");
    BLEDevice::startAdvertising();
  }
};

void prepareDataBlock(const char* in, byte* out, size_t size) {
  size_t i = 0;
  while (i < size && in[i]) out[i] = in[i++];
  while (i < size)       out[i++] = ' ';
}

int writeBlock(int block, byte data[]) {
  int sector = block / 4;
  int trailer = sector * 4 + 3;
  if ((block + 1) % 4 == 0) {
    Serial.println("Error: Cannot write to a trailer block!");
    return 2;
  }
  auto st = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailer, &key, &mfrc522.uid);
  if (st != MFRC522::STATUS_OK) {
    Serial.print("Auth failed: ");
    Serial.println(mfrc522.GetStatusCodeName(st));
    return 3;
  }
  st = mfrc522.MIFARE_Write(block, data, 16);
  if (st != MFRC522::STATUS_OK) {
    Serial.print("Write failed: ");
    Serial.println(mfrc522.GetStatusCodeName(st));
    return 4;
  }
  Serial.print("Data written to block ");
  Serial.println(block);
  return 0;
}

int readBlock(int block, byte buf[]) {
  int sector = block / 4;
  int trailer = sector * 4 + 3;
  auto st = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailer, &key, &mfrc522.uid);
  if (st != MFRC522::STATUS_OK) {
    Serial.print("Auth failed: ");
    Serial.println(mfrc522.GetStatusCodeName(st));
    return 3;
  }
  byte sz = 18;
  st = mfrc522.MIFARE_Read(block, buf, &sz);
  if (st != MFRC522::STATUS_OK) {
    Serial.print("Read failed: ");
    Serial.println(mfrc522.GetStatusCodeName(st));
    return 4;
  }
  return 0;
}

bool handleSerialInput() {
  static String in;
  bool got = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (in.length()) {
        in.trim();
        in.toCharArray(message, sizeof(message));
        got = true;
        in = "";
      }
    } else {
      in += c;
    }
  }
  return got;
}

void setupBLE() {
  BLEDevice::init("ESP32_BLE_Server");
  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new MyServerCallbacks());
  BLEService* svc = srv->createService(SERVICE_UUID);
  pCharacteristic = svc->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->setValue(message);
  pCharacteristic->addDescriptor(new BLE2902());
  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE started, waiting for connections...");
}

void setup() {
  Serial.begin(115200);
  Serial.println("System starting…");

  // Motor OFF
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  // RFID init
  SPI.begin(18, 19, 23, SS_PIN);
  mfrc522.PCD_Init();
  delay(200);
  Serial.println("RFID reader initialized.");
  Serial.println("Type text + ENTER to enter write-mode.");

  // Default key
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  setupBLE();
}

void loop() {
  // Handle serial → writeFlag
  if (handleSerialInput()) {
    Serial.print("DBG: New message ‘");
    Serial.print(message);
    Serial.println("’ → write-mode ON");
    prepareDataBlock(message, writeData, 16);
    writeFlag = true;
  }

  // Detect card presence & only act on new swipe
  bool present = mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial();

  if (present && !cardWasPresent) {
    Serial.println("DBG: New card detected");

    // Write if flagged
    if (writeFlag) {
      if (writeBlock(blockNumber, writeData) == 0) {
        Serial.println("DBG: Write successful → read-only mode");
        writeFlag = false;
      }
    }

    // Read data block
    if (readBlock(blockNumber, readBuffer) == 0) {
      Serial.print("RFID Block ");
      Serial.print(blockNumber);
      Serial.print(": ");
      for (int i = 0; i < 16; i++) Serial.write(readBuffer[i]);
      Serial.println();

      // Update BLE & notify
      memcpy(message, readBuffer, 16);
      message[16] = '\0';
      pCharacteristic->setValue(message);
      pCharacteristic->notify();
      Serial.print("BLE notify: ");
      Serial.println(message);

      // Motor ON
      Serial.println(">>> DEBUG: MOTOR ON");
      digitalWrite(MOTOR_PIN, HIGH);
      motorStartTime = millis();
      motorRunning = true;
    }

    // End crypto (do NOT halt — we want to keep card present)
    mfrc522.PCD_StopCrypto1();

    cardWasPresent = true;
  }
  else if (!present && cardWasPresent) {
    Serial.println("DBG: Card removed");
    cardWasPresent = false;
  }

  // Motor OFF timeout
  if (motorRunning && millis() - motorStartTime >= motorRunDuration) {
    Serial.println(">>> DEBUG: MOTOR OFF");
    digitalWrite(MOTOR_PIN, LOW);
    motorRunning = false;
  }
}
