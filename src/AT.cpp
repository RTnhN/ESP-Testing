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
#define VERSION "0.1"

//-------------------------//
// Global Server Variables //
//-------------------------//
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool bleInitialized = false;
bool bleAdvertising = false;

String clientName = "";

//-------------------------//
// Multi-Client Structures //
//-------------------------//

struct BLEClientConnection {
  BLEClient* client;
  String deviceAddress;
  // Cached pointers for reading
  String serviceUUID;
  String characteristicUUID;
  BLERemoteService* remoteServicePtr;
  BLERemoteCharacteristic* remoteCharacteristicPtr;
  // Cached pointers for writing
  String writeServiceUUID;
  String writeCharacteristicUUID;
  BLERemoteService* remoteWriteServicePtr;
  BLERemoteCharacteristic* remoteWriteCharacteristicPtr;
};

std::map<int, BLEClientConnection*> clientConnections;
int nextClientId = 1;

// Map to associate a remote characteristic with a client ID for notifications
std::map<BLERemoteCharacteristic*, int> notifyMap;

//-------------------------//
// Notification Callback   //
//-------------------------//

void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  
  int clientId = -1;
  if (notifyMap.find(pBLERemoteCharacteristic) != notifyMap.end()) {
    clientId = notifyMap[pBLERemoteCharacteristic];
  }
  Serial.print("0");
  Serial.print(clientId, HEX);
  Serial.print(" ");
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
    bleInitialized = true;
    Serial.println("BLE initialized");
  } else {
    Serial.println("BLE already initialized");
  }
}

void stopBLE() {
  if (bleInitialized) {
    Serial.println("OK");
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

void setClientName(String name) {
    clientName = name;
}

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
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    if ((device.haveName() && device.getName() == clientName.c_str()) || (clientName.length() == 0)) {
      Serial.printf("%s", device.getAddress().toString().c_str());
      Serial.println();
    }
  }
  pBLEScan->clearResults();
  Serial.println("Scan complete");
}

int connectToDeviceMulti(String deviceAddress) {
  BLEClient* newClient = BLEDevice::createClient();
  Serial.println("Created BLE client");
  BLEAddress addr(deviceAddress.c_str());
  if (newClient->connect(addr)) {
    Serial.println("Connected to device: " + deviceAddress);
    if(newClient->setMTU(128)) {
      Serial.println("MTU set to 128");
    } else {
      Serial.println("MTU negotiation failed or not supported");
    }
    BLEClientConnection* connection = new BLEClientConnection();
    connection->client = newClient;
    connection->deviceAddress = deviceAddress;
    connection->remoteServicePtr = nullptr;
    connection->remoteCharacteristicPtr = nullptr;
    connection->remoteWriteServicePtr = nullptr;
    connection->remoteWriteCharacteristicPtr = nullptr;
    int clientId = nextClientId++;
    clientConnections[clientId] = connection;
    Serial.print("Assigned Client ID: ");
    Serial.println(clientId);
    return clientId;
  } else {
    Serial.println("Failed to connect to device: " + deviceAddress);
    return -1;
  }
}

void discoverServicesMulti(BLEClientConnection* connection) {
  if (connection == nullptr || connection->client == nullptr || !connection->client->isConnected()) {
    Serial.println("Client not connected.");
    return;
  }
  Serial.println("Discovering services and characteristics...");
  auto servicesMap = connection->client->getServices();
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

void readCachedCharacteristicMulti(BLEClientConnection* connection) {
  if (connection == nullptr || connection->client == nullptr || !connection->client->isConnected()) {
    Serial.println("Client not connected.");
    return;
  }
  if (connection->remoteCharacteristicPtr == nullptr) {
    Serial.println("Characteristic pointer not set. Use AT+BLESETSERVICE and AT+BLESETCHAR.");
    return;
  }
  std::string value = connection->remoteCharacteristicPtr->readValue();
  Serial.print("Read value (hex): ");
  for (size_t i = 0; i < value.size(); i++) {
    uint8_t byte = value[i];
    if (byte < 0x10) Serial.print("0");
    Serial.print(byte, HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void readCharacteristicMulti(BLEClientConnection* connection, String serviceUuid, String charUuid) {
  if (connection == nullptr || connection->client == nullptr || !connection->client->isConnected()) {
    Serial.println("Client not connected.");
    return;
  }
  BLERemoteService* remoteService = connection->client->getService(BLEUUID(serviceUuid.c_str()));
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
    Serial.print("ESP32-S3-AT Firmware Version ");
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
  else if (cmd.startsWith("AT+BLESETCLIENTNAME=")) {
    String name = cmd.substring(String("AT+BLESETCLIENTNAME=").length());
    name.trim();
    setClientName(name);
    Serial.println("OK");
  }
  else if (cmd == "AT+BLESCAN") {
    scanBLEDevices();
    Serial.println("OK");
  }
  // Connect to device: AT+BLECONNECT=<device_address>
  else if (cmd.startsWith("AT+BLECONNECT=")) {
    String addr = cmd.substring(String("AT+BLECONNECT=").length());
    addr.trim();
    if (!bleInitialized) {
      Serial.println("ERROR: BLE not initialized.");
      return;
    }
    int clientId = connectToDeviceMulti(addr);
    if (clientId != -1) {
      Serial.print("OK, Client ID: ");
      Serial.println(clientId);
    } else {
      Serial.println("ERROR: Connection failed.");
    }
  }
  // Discover services: AT+BLEDISCOVER=<clientId>
  else if (cmd.startsWith("AT+BLEDISCOVER=")) {
    String param = cmd.substring(String("AT+BLEDISCOVER=").length());
    param.trim();
    int clientId = param.toInt();
    if (clientConnections.find(clientId) == clientConnections.end()) {
      Serial.println("ERROR: Client ID not found.");
    } else {
      discoverServicesMulti(clientConnections[clientId]);
      Serial.println("OK");
    }
  }
  // Set and cache remote service UUID for reading: AT+BLESETSERVICE=<clientId>,<service_uuid>
  else if (cmd.startsWith("AT+BLESETSERVICE=")) {
    String params = cmd.substring(String("AT+BLESETSERVICE=").length());
    int commaIndex = params.indexOf(",");
    if (commaIndex == -1) {
      Serial.println("ERROR: Invalid parameters. Use AT+BLESETSERVICE=<clientId>,<service_uuid>");
    } else {
      String idStr = params.substring(0, commaIndex);
      String svcUuid = params.substring(commaIndex + 1);
      idStr.trim();
      svcUuid.trim();
      int clientId = idStr.toInt();
      if (clientConnections.find(clientId) == clientConnections.end()) {
        Serial.println("ERROR: Client ID not found.");
      } else {
        BLEClientConnection* connection = clientConnections[clientId];
        connection->serviceUUID = svcUuid;
        Serial.print("Service UUID set to: ");
        Serial.println(svcUuid);
        if (connection->client != nullptr && connection->client->isConnected()) {
          connection->remoteServicePtr = connection->client->getService(BLEUUID(svcUuid.c_str()));
          if (connection->remoteServicePtr != nullptr) {
            Serial.println("Service pointer acquired.");
            if (connection->characteristicUUID.length() > 0) {
              connection->remoteCharacteristicPtr = connection->remoteServicePtr->getCharacteristic(BLEUUID(connection->characteristicUUID.c_str()));
              if (connection->remoteCharacteristicPtr != nullptr) {
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
        Serial.println("OK");
      }
    }
  }
  // Set and cache remote characteristic UUID for reading: AT+BLESETCHAR=<clientId>,<char_uuid>
  else if (cmd.startsWith("AT+BLESETCHAR=")) {
    String params = cmd.substring(String("AT+BLESETCHAR=").length());
    int commaIndex = params.indexOf(",");
    if (commaIndex == -1) {
      Serial.println("ERROR: Invalid parameters. Use AT+BLESETCHAR=<clientId>,<char_uuid>");
    } else {
      String idStr = params.substring(0, commaIndex);
      String charUuid = params.substring(commaIndex + 1);
      idStr.trim();
      charUuid.trim();
      int clientId = idStr.toInt();
      if (clientConnections.find(clientId) == clientConnections.end()) {
        Serial.println("ERROR: Client ID not found.");
      } else {
        BLEClientConnection* connection = clientConnections[clientId];
        connection->characteristicUUID = charUuid;
        Serial.print("Characteristic UUID set to: ");
        Serial.println(charUuid);
        if (connection->remoteServicePtr != nullptr) {
          connection->remoteCharacteristicPtr = connection->remoteServicePtr->getCharacteristic(BLEUUID(charUuid.c_str()));
          if (connection->remoteCharacteristicPtr != nullptr) {
            Serial.println("Characteristic pointer acquired.");
          } else {
            Serial.println("Characteristic not found in cached service.");
          }
        } else {
          Serial.println("Service pointer not set. Set service first.");
        }
        Serial.println("OK");
      }
    }
  }
  // Read using cached pointers or fallback read:
  // AT+BLEREAD=<clientId> or AT+BLEREAD=<clientId>,<service_uuid>,<char_uuid>
  else if (cmd.startsWith("AT+BLEREAD=")) {
    String params = cmd.substring(String("AT+BLEREAD=").length());
    int firstComma = params.indexOf(",");
    if (firstComma == -1) {
      int clientId = params.toInt();
      if (clientConnections.find(clientId) == clientConnections.end()) {
        Serial.println("ERROR: Client ID not found.");
      } else {
        readCachedCharacteristicMulti(clientConnections[clientId]);
      }
    } else {
      int secondComma = params.indexOf(",", firstComma + 1);
      if (secondComma == -1) {
        Serial.println("ERROR: Invalid parameters. Use AT+BLEREAD=<clientId>,<service_uuid>,<char_uuid>");
      } else {
        String idStr = params.substring(0, firstComma);
        String svcUuid = params.substring(firstComma + 1, secondComma);
        String charUuid = params.substring(secondComma + 1);
        idStr.trim();
        svcUuid.trim();
        charUuid.trim();
        int clientId = idStr.toInt();
        if (clientConnections.find(clientId) == clientConnections.end()) {
          Serial.println("ERROR: Client ID not found.");
        } else {
          readCharacteristicMulti(clientConnections[clientId], svcUuid, charUuid);
        }
      }
    }
    Serial.println("OK");
  }
  // Enable notifications: AT+BLENOTIFY=<clientId>
  else if (cmd.startsWith("AT+BLENOTIFY=")) {
    String idStr = cmd.substring(String("AT+BLENOTIFY=").length());
    idStr.trim();
    int clientId = idStr.toInt();
    if (clientConnections.find(clientId) == clientConnections.end()) {
      Serial.println("ERROR: Client ID not found.");
    } else {
      BLEClientConnection* connection = clientConnections[clientId];
      if (connection->remoteCharacteristicPtr == nullptr) {
        Serial.println("ERROR: Characteristic pointer not set. Use AT+BLESETSERVICE and AT+BLESETCHAR first.");
      } else {
        connection->remoteCharacteristicPtr->registerForNotify(notifyCallback);
        notifyMap[connection->remoteCharacteristicPtr] = clientId;
        Serial.println("Notifications enabled");
        Serial.println("OK");  
      }
    }
  }
  // Disable notifications: AT+BLENOTIFYOFF=<clientId>
  else if (cmd.startsWith("AT+BLENOTIFYOFF=")) {
    String idStr = cmd.substring(String("AT+BLENOTIFYOFF=").length());
    idStr.trim();
    int clientId = idStr.toInt();
    if (clientConnections.find(clientId) == clientConnections.end()) {
      Serial.println("ERROR: Client ID not found.");
    } else {
      BLEClientConnection* connection = clientConnections[clientId];
      if (connection->remoteCharacteristicPtr == nullptr) {
        Serial.println("ERROR: Characteristic pointer not set.");
      } else {
        connection->remoteCharacteristicPtr->registerForNotify(nullptr);
        notifyMap.erase(connection->remoteCharacteristicPtr);
        Serial.println("Notifications disabled");
        Serial.println("OK");
      }
    }
  }
  // Set and cache remote service UUID for writing: AT+BLESETWRITESERVICE=<clientId>,<service_uuid>
  else if (cmd.startsWith("AT+BLESETWRITESERVICE=")) {
    String params = cmd.substring(String("AT+BLESETWRITESERVICE=").length());
    int commaIndex = params.indexOf(",");
    if (commaIndex == -1) {
      Serial.println("ERROR: Invalid parameters. Use AT+BLESETWRITESERVICE=<clientId>,<service_uuid>");
    } else {
      String idStr = params.substring(0, commaIndex);
      String svcUuid = params.substring(commaIndex + 1);
      idStr.trim();
      svcUuid.trim();
      int clientId = idStr.toInt();
      if (clientConnections.find(clientId) == clientConnections.end()) {
        Serial.println("ERROR: Client ID not found.");
      } else {
        BLEClientConnection* connection = clientConnections[clientId];
        connection->writeServiceUUID = svcUuid;
        Serial.print("Write Service UUID set to: ");
        Serial.println(svcUuid);
        if (connection->client != nullptr && connection->client->isConnected()) {
          connection->remoteWriteServicePtr = connection->client->getService(BLEUUID(svcUuid.c_str()));
          if (connection->remoteWriteServicePtr != nullptr) {
            Serial.println("Write Service pointer acquired.");
            if (connection->writeCharacteristicUUID.length() > 0) {
              connection->remoteWriteCharacteristicPtr = connection->remoteWriteServicePtr->getCharacteristic(BLEUUID(connection->writeCharacteristicUUID.c_str()));
              if (connection->remoteWriteCharacteristicPtr != nullptr) {
                Serial.println("Write Characteristic pointer acquired.");
              } else {
                Serial.println("Write Characteristic pointer not found.");
              }
            }
          } else {
            Serial.println("Write Service not found on remote device.");
          }
        } else {
          Serial.println("Not connected to any device. Write pointer caching deferred.");
        }
        Serial.println("OK");
      }
    }
  }
  // Set and cache remote characteristic UUID for writing: AT+BLESETWRITECHAR=<clientId>,<char_uuid>
  else if (cmd.startsWith("AT+BLESETWRITECHAR=")) {
    String params = cmd.substring(String("AT+BLESETWRITECHAR=").length());
    int commaIndex = params.indexOf(",");
    if (commaIndex == -1) {
      Serial.println("ERROR: Invalid parameters. Use AT+BLESETWRITECHAR=<clientId>,<char_uuid>");
    } else {
      String idStr = params.substring(0, commaIndex);
      String charUuid = params.substring(commaIndex + 1);
      idStr.trim();
      charUuid.trim();
      int clientId = idStr.toInt();
      if (clientConnections.find(clientId) == clientConnections.end()) {
        Serial.println("ERROR: Client ID not found.");
      } else {
        BLEClientConnection* connection = clientConnections[clientId];
        connection->writeCharacteristicUUID = charUuid;
        Serial.print("Write Characteristic UUID set to: ");
        Serial.println(charUuid);
        if (connection->remoteWriteServicePtr != nullptr) {
          connection->remoteWriteCharacteristicPtr = connection->remoteWriteServicePtr->getCharacteristic(BLEUUID(charUuid.c_str()));
          if (connection->remoteWriteCharacteristicPtr != nullptr) {
            Serial.println("Write Characteristic pointer acquired.");
          } else {
            Serial.println("Write Characteristic not found in cached write service.");
          }
        } else {
          Serial.println("Write Service pointer not set. Set write service first.");
        }
        Serial.println("OK");
      }
    }
  }
  // Write data to the cached write characteristic: AT+BLEWRITE=<clientId>,<data>
  else if (cmd.startsWith("AT+BLEWRITE=")) {
    String params = cmd.substring(String("AT+BLEWRITE=").length());
    int commaIndex = params.indexOf(",");
    if (commaIndex == -1) {
      Serial.println("ERROR: Invalid parameters. Use AT+BLEWRITE=<clientId>,<data>");
    } else {
      String idStr = params.substring(0, commaIndex);
      String data = params.substring(commaIndex + 1);
      idStr.trim();
      data.trim();
      int clientId = idStr.toInt();
      if (clientConnections.find(clientId) == clientConnections.end()) {
        Serial.println("ERROR: Client ID not found.");
      } else {
        BLEClientConnection* connection = clientConnections[clientId];
        if (connection->remoteWriteCharacteristicPtr == nullptr) {
          Serial.println("ERROR: Write Characteristic pointer not set. Use AT+BLESETWRITESERVICE and AT+BLESETWRITECHAR first.");
        } else {
          connection->remoteWriteCharacteristicPtr->writeValue(data.c_str(), data.length());
          Serial.println("Data written");
        }
      }
    }
    Serial.println("OK");
  }
  else {
    Serial.println("ERROR: Unknown Command");
  }
}

void setup() {
  Serial.begin(921600);
  while (!Serial) { ; }  // Wait for serial port
  Serial.println("AT Command Firmware Starting");
}

void loop() {
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
