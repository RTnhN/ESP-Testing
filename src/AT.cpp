#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLE2902.h>
#include <map>

// Server mode default UUIDs (for example)
#define SERVER_SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define SERVER_CHARACTERISTIC_UUID "abcdefab-cdef-abcd-efab-cdefabcdefab"

// Global flags and pointers for server mode
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool bleInitialized = false;
bool bleAdvertising = false;

// Global pointer for client mode
BLEClient* pClient = nullptr;

// Global variables for caching remote pointers (for client mode)
String globalServiceUUID = "";
String globalCharacteristicUUID = "";
BLERemoteService* remoteServicePtr = nullptr;
BLERemoteCharacteristic* remoteCharacteristicPtr = nullptr;

# define VERSION "0.1"

//-------------------------//
// Notify Callback         //
//-------------------------//

void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  Serial.print("Notification received (hex): ");
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = pData[i];
    if (byte < 0x10) Serial.print("0");
    Serial.print(byte, HEX);
    Serial.print(" ");
  }
  Serial.println();
}

//-------------------------//
// Server Mode Functions   //
//-------------------------//

void startBLE() {
  if (!bleInitialized) {
    BLEDevice::init("ESP32-AT");
    pServer = BLEDevice::createServer();
    pService = pServer->createService(SERVER_SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                        SERVER_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_WRITE
                      );
    pCharacteristic->setValue("Hello World");
    pService->start();
    bleInitialized = true;
    Serial.println("BLE initialized (server mode)");
  } else {
    Serial.println("BLE already initialized");
  }
}

void stopBLE() {
  if (bleInitialized) {
    if (bleAdvertising) {
      BLEDevice::getAdvertising()->stop();
      bleAdvertising = false;
      Serial.println("BLE advertising stopped");
    }
    bleInitialized = false;
    Serial.println("BLE deinitialized (simulated)");
  } else {
    Serial.println("BLE not initialized");
  }
}

void startAdvertising() {
  if (bleInitialized) {
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!bleAdvertising) {
      pAdvertising->start();
      bleAdvertising = true;
      Serial.println("BLE advertising started");
    } else {
      Serial.println("BLE already advertising");
    }
  } else {
    Serial.println("BLE not initialized");
  }
}

void stopAdvertising() {
  if (bleInitialized) {
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (bleAdvertising) {
      pAdvertising->stop();
      bleAdvertising = false;
      Serial.println("BLE advertising stopped");
    } else {
      Serial.println("BLE not advertising");
    }
  } else {
    Serial.println("BLE not initialized");
  }
}

//-------------------------//
// Client Mode Functions   //
//-------------------------//

void scanBLEDevices() {
  if (!bleInitialized) {
    BLEDevice::init("ESP32-AT");
    bleInitialized = true;
  }
  Serial.println("Starting BLE scan...");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  BLEScanResults foundDevices = pBLEScan->start(5, false);
  int count = foundDevices.getCount();
  Serial.printf("Devices found: %d\n", count);
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    Serial.printf("Device %d: %s, RSSI: %d\n", i + 1, device.getAddress().toString().c_str(), device.getRSSI());
    if (device.haveName()) {
      Serial.printf("   Name: %s\n", device.getName().c_str());
    }
    if (device.haveServiceUUID()) {
      Serial.printf("   Service UUID: %s\n", device.getServiceUUID().toString().c_str());
    }
  }
  pBLEScan->clearResults();
  Serial.println("Scan complete");
}

void connectToDevice(String deviceAddress) {
  if (pClient != nullptr && pClient->isConnected()) {
    Serial.println("Already connected to a device.");
    return;
  }
  if (!bleInitialized) {
    BLEDevice::init("ESP32-AT");
    bleInitialized = true;
  }
  pClient = BLEDevice::createClient();
  Serial.println("Created BLE client");
  BLEAddress addr(deviceAddress.c_str());
  if (pClient->connect(addr)) {
    Serial.println("Connected to device: " + deviceAddress);
  } else {
    Serial.println("Failed to connect to device: " + deviceAddress);
  }
}

void discoverServices() {
  if (pClient == nullptr || !pClient->isConnected()) {
    Serial.println("Not connected to any device.");
    return;
  }
  Serial.println("Discovering services and characteristics...");
  auto servicesMap = pClient->getServices();
  if (servicesMap == nullptr || servicesMap->empty()) {
    Serial.println("No services found.");
  } else {
    for (auto const& servicePair : *servicesMap) {
      BLERemoteService* service = servicePair.second;
      Serial.print("Service: ");
      Serial.println(service->getUUID().toString().c_str());
      auto characteristicsMap = service->getCharacteristics();
      if (characteristicsMap != nullptr && !characteristicsMap->empty()) {
        for (auto const& charPair : *characteristicsMap) {
          BLERemoteCharacteristic* characteristic = charPair.second;
          Serial.print("  Characteristic: ");
          Serial.println(characteristic->getUUID().toString().c_str());
        }
      }
    }
  }
  Serial.println("Service discovery complete.");
}

// Standard read using cached pointers that prints full binary data in hex
void readCachedCharacteristic() {
  if (pClient == nullptr || !pClient->isConnected()) {
    Serial.println("Not connected to any device.");
    return;
  }
  if (remoteCharacteristicPtr == nullptr) {
    Serial.println("Characteristic pointer not set. Use AT+BLESETSERVICE and AT+BLESETCHAR.");
    return;
  }
  std::string value = remoteCharacteristicPtr->readValue();
  Serial.print("Read value (hex): ");
  for (size_t i = 0; i < value.size(); i++) {
    uint8_t byte = value[i];
    if (byte < 0x10) Serial.print("0");
    Serial.print(byte, HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// Fallback read using UUIDs if caching pointers are not used
void readCharacteristic(String serviceUuid, String charUuid) {
  if (pClient == nullptr || !pClient->isConnected()) {
    Serial.println("Not connected to any device.");
    return;
  }
  BLERemoteService* remoteService = pClient->getService(BLEUUID(serviceUuid.c_str()));
  if (remoteService == nullptr) {
    Serial.println("Service not found: " + serviceUuid);
    return;
  }
  BLERemoteCharacteristic* remoteCharacteristic = remoteService->getCharacteristic(BLEUUID(charUuid.c_str()));
  if (remoteCharacteristic == nullptr) {
    Serial.println("Characteristic not found: " + charUuid);
    return;
  }
  std::string value = remoteCharacteristic->readValue();
  Serial.print("Read value (hex): ");
  for (size_t i = 0; i < value.size(); i++) {
    uint8_t byte = value[i];
    if (byte < 0x10) Serial.print("0");
    Serial.print(byte, HEX);
    Serial.print(" ");
  }
  Serial.println();
}

//-------------------------//
// AT Command Processing   //
//-------------------------//

String inputBuffer = "";

void processATCommand(String cmd) {
  cmd.trim();  // Remove extra whitespace
  if (cmd == "AT") {
    Serial.println("OK");
  }
  else if (cmd == "AT+VERSION?") {
    Serial.print("ESP32-S3-AT Firmware Version");
    Serial.println(VERSION);
  }
  else if (cmd == "AT+BLESTART") {
    startBLE();
    Serial.println("OK");
  }
  else if (cmd == "AT+BLESTOP") {
    stopBLE();
    Serial.println("OK");
  }
  else if (cmd == "AT+BLEADVERTISE=ON") {
    startAdvertising();
    Serial.println("OK");
  }
  else if (cmd == "AT+BLEADVERTISE=OFF") {
    stopAdvertising();
    Serial.println("OK");
  }
  else if (cmd == "AT+BLESCAN") {
    scanBLEDevices();
    Serial.println("OK");
  }
  else if (cmd.startsWith("AT+BLECONNECT=")) {
    String addr = cmd.substring(14);
    addr.trim();
    connectToDevice(addr);
    Serial.println("OK");
  }
  else if (cmd == "AT+BLEDISCOVER") {
    discoverServices();
    Serial.println("OK");
  }
  // Set and cache remote service UUID
  else if (cmd.startsWith("AT+BLESETSERVICE=")) {
    String svcUuid = cmd.substring(String("AT+BLESETSERVICE=").length());
    svcUuid.trim();
    globalServiceUUID = svcUuid;
    Serial.print("Service UUID set to: ");
    Serial.println(globalServiceUUID);
    if (pClient != nullptr && pClient->isConnected()) {
      remoteServicePtr = pClient->getService(BLEUUID(globalServiceUUID.c_str()));
      if (remoteServicePtr != nullptr) {
        Serial.println("Service pointer acquired.");
        if (globalCharacteristicUUID.length() > 0) {
          remoteCharacteristicPtr = remoteServicePtr->getCharacteristic(BLEUUID(globalCharacteristicUUID.c_str()));
          if (remoteCharacteristicPtr != nullptr) {
            Serial.println("Characteristic pointer acquired.");
          } else {
            Serial.println("Characteristic pointer not found.");
          }
        }
      } else {
        Serial.println("Service not found on remote device.");
      }
    } else {
      Serial.println("Not connected to any device. Pointer caching deferred.");
    }
  }
  // Set and cache remote characteristic UUID
  else if (cmd.startsWith("AT+BLESETCHAR=")) {
    String charUuid = cmd.substring(String("AT+BLESETCHAR=").length());
    charUuid.trim();
    globalCharacteristicUUID = charUuid;
    Serial.print("Characteristic UUID set to: ");
    Serial.println(globalCharacteristicUUID);
    if (remoteServicePtr != nullptr) {
      remoteCharacteristicPtr = remoteServicePtr->getCharacteristic(BLEUUID(globalCharacteristicUUID.c_str()));
      if (remoteCharacteristicPtr != nullptr) {
        Serial.println("Characteristic pointer acquired.");
      } else {
        Serial.println("Characteristic not found in cached service.");
      }
    } else {
      Serial.println("Service pointer not set. Set service first.");
    }
  }
  // Standard read command using cached pointers
  else if (cmd == "AT+BLEREAD") {
    readCachedCharacteristic();
    Serial.println("OK");
  }
  else if (cmd.startsWith("AT+BLEREAD=")) {
    String params = cmd.substring(11);
    int commaIndex = params.indexOf(",");
    if (commaIndex == -1) {
      Serial.println("ERROR: Invalid parameters. Use AT+BLEREAD=<service_uuid>,<characteristic_uuid>");
    } else {
      String svcUuid = params.substring(0, commaIndex);
      String charUuid = params.substring(commaIndex + 1);
      svcUuid.trim();
      charUuid.trim();
      readCharacteristic(svcUuid, charUuid);
      Serial.println("OK");
    }
  }
  // Enable notifications using the notify API
  else if (cmd == "AT+BLENOTIFY") {
    if (remoteCharacteristicPtr == nullptr) {
      Serial.println("ERROR: Characteristic pointer not set. Use AT+BLESETSERVICE and AT+BLESETCHAR first.");
    } else {
      remoteCharacteristicPtr->registerForNotify(notifyCallback);
      Serial.println("Notifications enabled");
    }
  }
  // Disable notifications
  else if (cmd == "AT+BLENOTIFYOFF") {
    if (remoteCharacteristicPtr == nullptr) {
      Serial.println("ERROR: Characteristic pointer not set.");
    } else {
      remoteCharacteristicPtr->registerForNotify(nullptr);
      Serial.println("Notifications disabled");
    }
  }
  else {
    Serial.println("ERROR: Unknown Command");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }  // Wait for serial port
  Serial.print("AT Command Firmware Starting ");
  Serial.println(VERSION);
}

void loop() {
  // Read incoming characters from Serial and process complete commands.
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      if (inputBuffer.length() > 0) {
        processATCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += inChar;
    }
  }
}
