#include <SPI.h>
#include <MFRC522.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// -------------------- RFID Setup --------------------
#define RST_PIN 22    // MFRC522 RST pin
#define SS_PIN  5     // MFRC522 SDA/CS pin

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;  // Default key for authentication

// RFID memory block to read/write (must not be a trailer block)
const int blockNumber = 4;

// Buffers for RFID data
byte writeData[16];
byte readBuffer[18];

// Global message storage (max 16 characters + null terminator)
// This is the default message that will be written to or read from the tag.
char message[17] = "Yay it works";

// Flag that enables write mode when a new message is received via Serial.
bool writeFlag = false;

// Helper: Copy string into a 16-byte block, padding with spaces.
void prepareDataBlock(const char* input, byte* output, size_t outputSize) {
  size_t i = 0;
  while (input[i] != '\0' && i < outputSize) {
    output[i] = input[i];
    i++;
  }
  while (i < outputSize) {
    output[i++] = ' ';
  }
}

// --- Write 16 bytes to a given block number ---
int writeBlock(int block, byte data[]) {
  int sector = block / 4;
  int trailerBlock = sector * 4 + 3;

  // Prevent writing to a trailer block.
  if ((block + 1) % 4 == 0) {
    Serial.println("Error: Cannot write to a trailer block!");
    return 2;
  }

  MFRC522::StatusCode status;
  
  // Authenticate using Key A.
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Authentication failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 3;
  }
  
  // Write data to the specified block.
  status = mfrc522.MIFARE_Write(block, data, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Data write failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 4;
  }
  
  Serial.print("Data written to block ");
  Serial.println(block);
  return 0;
}

// --- Read 16 bytes from a given block number ---
int readBlock(int block, byte buffer[]) {
  int sector = block / 4;
  int trailerBlock = sector * 4 + 3;
  
  MFRC522::StatusCode status;
  
  // Authenticate using Key A.
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Authentication failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 3;
  }
  
  byte bufferSize = 18;
  status = mfrc522.MIFARE_Read(block, buffer, &bufferSize);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Data read failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 4;
  }
  
  return 0;
}

// --- Handle Serial Input ---
// Checks for user input via Serial. When ENTER is pressed,
// the input is stored in 'message' and write mode is enabled.
bool handleSerialInput() {
  static String userInput = "";
  bool newMessage = false;
  
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (userInput.length() > 0) {
        userInput.trim();
        userInput.toCharArray(message, sizeof(message));
        newMessage = true;
        userInput = "";
      }
    } else {
      userInput += c;
    }
  }
  return newMessage;
}

// -------------------- BLE Setup --------------------
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Custom callbacks to log BLE connections.
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    Serial.println("BLE client connected!");
  }
  void onDisconnect(BLEServer* pServer) override {
    Serial.println("BLE client disconnected!");
    // Restart advertising so another client can connect.
    BLEDevice::startAdvertising();
  }
};

BLECharacteristic *pCharacteristic;

void setupBLE() {
  BLEDevice::init("ESP32_BLE_Server");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Create a BLE Characteristic with read, write, and notify properties.
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
                    
  // Set an initial value.
  pCharacteristic->setValue(message);
  
  // Add descriptor so clients can enable notifications.
  pCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE service started. Connect and subscribe to notifications to receive updates.");
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);
  Serial.println("System starting...");

  // Initialize RFID
  SPI.begin(18, 19, 23, SS_PIN);  // ESP32 SPI pins: SCK=18, MISO=19, MOSI=23, CS=SS_PIN
  mfrc522.PCD_Init();
  delay(200);
  Serial.println("RFID reader initialized.");
  Serial.println("Type a new message (max 16 characters) and press ENTER to update the message.");
  
  // Set default key for MIFARE authentication (0xFF)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  // Initialize BLE
  setupBLE();
}

// -------------------- Main Loop --------------------
void loop() {
  // Check for new Serial input to update the message.
  if (handleSerialInput()) {
    prepareDataBlock(message, writeData, 16);
    writeFlag = true;
    Serial.println("Write mode enabled. Present an RFID tag to update its data.");
  }

  // Process RFID card if one is present.
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    
    // If write mode is enabled, write the new message.
    if (writeFlag) {
      if (writeBlock(blockNumber, writeData) == 0) {
        Serial.println("Write successful! Reverting to read-only mode.");
        writeFlag = false;
      }
    }
    
    // Read the block data from the RFID tag.
    if (readBlock(blockNumber, readBuffer) == 0) {
      Serial.print("RFID Block ");
      Serial.print(blockNumber);
      Serial.print(": ");
      for (int i = 0; i < 16; i++) {
        Serial.write(readBuffer[i]);
      }
      Serial.println();
      
      // Update the global message with the new RFID data.
      memcpy(message, readBuffer, 16);
      message[16] = '\0';
      
      // Send the updated message via BLE notification.
      pCharacteristic->setValue(message);
      pCharacteristic->notify();
      Serial.print("BLE Notification sent with message: ");
      Serial.println(message);
    }
    
    // Halt the RFID tag and stop encryption.
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    
    // Small delay to prevent reading the same tag repeatedly.
    delay(1000);
  }
  
  // No BLE notifications or Serial prints occur here if no RFID tag is read.
}
