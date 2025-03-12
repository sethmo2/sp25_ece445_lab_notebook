#include <SPI.h>
#include <MFRC522.h>

// RFID module pins
#define RST_PIN 22    // MFRC522 RST
#define SS_PIN  5     // MFRC522 SDA/CS

MFRC522 mfrc522(SS_PIN, RST_PIN); 
MFRC522::MIFARE_Key key;

// Control flag for write mode (false = read-only)
bool writeFlag = false;

// NFC memory block to write/read (must not be a trailer block)
const int blockNumber = 4;

// Buffers for data
byte writeData[16];
byte readBuffer[18];

// Global message storage (max 16 characters + null terminator)
char message[17] = "Yay it works";

// Helper function: copy a string (up to 16 chars) into a 16-byte buffer,
// padding with spaces if necessary.
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

void setup() {
	Serial.begin(115200);

	// Initialize hardware SPI on ESP32 (SCK=18, MISO=19, MOSI=23, CS=SS_PIN)
	SPI.begin(18, 19, 23, SS_PIN);
	mfrc522.PCD_Init();
	delay(200);

	Serial.println("Ready.");
	Serial.println("To enable write mode, type a new message (max 16 characters) and press ENTER.");

	// Set the default key for MIFARE authentication to 0xFF
	for (byte i = 0; i < 6; i++) {
		key.keyByte[i] = 0xFF;
	}
}

void loop() {
	// Check for terminal input; if a new message is received, update the message
	// and enable write mode.
	if (handleSerialInput()) {
		prepareDataBlock(message, writeData, 16);
		writeFlag = true;
		Serial.println("Write mode enabled. Present an NFC tag to write the new message.");
	}

	// Proceed with NFC scanning only if a tag is present.
	if (!mfrc522.PICC_IsNewCardPresent()) {
		return;
	}
	if (!mfrc522.PICC_ReadCardSerial()) {
		return;
	}

	// If in write mode, attempt a one-time write and then revert to read-only.
	if (writeFlag) {
	if (writeBlock(blockNumber, writeData) == 0) {
		Serial.println("Write successful! Reverting to read-only mode.");
		writeFlag = false;
	}
	}

	// Always read and print the block data.
	if (readBlock(blockNumber, readBuffer) == 0) {
		Serial.print("Block ");
		Serial.print(blockNumber);
		Serial.print(": ");
	for (int i = 0; i < 16; i++) {
		Serial.write(readBuffer[i]);
	}
		Serial.println();
	}

	// Halt the card and stop encryption.
	mfrc522.PICC_HaltA();
	mfrc522.PCD_StopCrypto1();

	delay(1000); // Delay to avoid reading the same tag repeatedly.
}

	// --- Console Input Handling ---
	// This function checks for user input via Serial. When the user presses ENTER,
	// the input is copied into the global 'message' buffer and the function returns true.
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
	} 
	else {
		userInput += c;
	}
	}

	return newMessage;
}

// --- Write 16 bytes to a given block number ---
int writeBlock(int block, byte data[]) {
	int sector = block / 4;
	int trailerBlock = sector * 4 + 3;

	// Prevent writing to a trailer block
	if ((block + 1) % 4 == 0) {
		Serial.println("Error: Cannot write to a trailer block!");
		return 2;
	}

	MFRC522::StatusCode status;

	// Authenticate using Key A
	status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
	if (status != MFRC522::STATUS_OK) {
		Serial.print("Authentication failed: ");
		Serial.println(mfrc522.GetStatusCodeName(status));
		return 3;
	}

	// Write data to the specified block
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

	// Authenticate using Key A
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
