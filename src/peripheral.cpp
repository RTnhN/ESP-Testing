#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *pCharacteristic;
uint32_t sequenceNumber = 0; // Sequence number
const int DATA_SIZE = 80;    // Define data block size
uint8_t data[DATA_SIZE];
bool deviceConnected = false; // Flag to track client connection

// Custom server callbacks to manage connection status
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("Client connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("Client disconnected");
    // Restart advertising so new clients can connect
    BLEDevice::startAdvertising();
  }
};

// Custom characteristic callbacks to handle write events
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.print("Received Value: ");
      for (int i = 0; i < rxValue.length(); i++) {
        Serial.print(rxValue[i]);
      }
      Serial.println();
    }
  }
};

void sendData() {
  // Add the sequence number (stored in bytes 2-5)
  data[2] = (sequenceNumber >> 24) & 0xFF; // Most significant byte
  data[3] = (sequenceNumber >> 16) & 0xFF;
  data[4] = (sequenceNumber >> 8) & 0xFF;
  data[5] = sequenceNumber & 0xFF; // Least significant byte

  // Fill the middle part with incremental data as an example
  for (int i = 6; i < DATA_SIZE - 2; i++) {
    data[i] = (uint8_t)(i - 6);
  }

  data[DATA_SIZE - 2] = 0xFE; // Footer byte 1
  data[DATA_SIZE - 1] = 0xFE; // Footer byte 2

  // Only send notification if a client is connected
  if (deviceConnected) {
    pCharacteristic->setValue(data, sizeof(data));
    pCharacteristic->notify(); // Notify the client that data has been updated
    sequenceNumber++; // Increase the sequence number
  }
}

// Timer callback to periodically send data
void onTimer(TimerHandle_t xTimer) {
  sendData();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE work!");

  // Create the start of the data packet (Header bytes)
  data[0] = 0xFF; // Header byte 1
  data[1] = 0xFF; // Header byte 2

  BLEDevice::init("Device1");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY
  );

  // Set the custom callback for write events
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Add the Client Characteristic Configuration Descriptor to allow notifications
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE Server started, waiting for clients...");

  TimerHandle_t timer = xTimerCreate("DataTimer", pdMS_TO_TICKS(10), pdTRUE, (void *)0, onTimer);
  xTimerStart(timer, 0);
}

void loop() {
  // put your main code here, to run repeatedly:
}
