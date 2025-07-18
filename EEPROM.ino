#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>
#include <WiFi.h>
#include <time.h>
#include <EEPROM.h>
#include <string>

using std::string;

#define SCAN_INTERVAL_MS 5000
#define ID_REFRESH_INTERVAL_MS 600000 // 10 mins
#define RSSI_THRESHOLD -50
#define EEPROM_SIZE 4096
#define MAX_LOGS 30
#define EXPOSURE_WINDOW_MS 6000

BLEAdvertising* pAdvertising;
BLEScan* pBLEScan;
string currentID;
unsigned long lastIDRefresh = 0;
unsigned long bootTime = 0;  
bool timeInitialized = false;
bool printed = false;
uint32_t duration = 0;

struct ContactLog {
    uint32_t relativeTimestamp;
    string peerID;
    int8_t rssi;
    uint32_t duration;
};

ContactLog logs[MAX_LOGS];
int logIndex = 0;

// === Time Management ===
void initializeTime() {
    WiFi.begin("Sid", "siddik67");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        configTime(-14400, 0, "pool.ntp.org");
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            timeInitialized = true;
            bootTime = time(nullptr) - (millis() / 1000);
        }
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    } else {
        Serial.println("\nWiFi connection failed, using relative timestamps");
    }
}

// === Generate Random ID ===
string generateRandomID() {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    string id;
    for (int i = 0; i < 8; i++) {
        id += chars[random(0, 36)];
    }
    return id;
}

// === BLE Advertisement ===
void advertiseID(const string& id) {
    BLEAdvertisementData adData;
    adData.setName("DATA");
    adData.setManufacturerData(id.c_str());
    pAdvertising->setAdvertisementData(adData);
    pAdvertising->start();
}

// === EEPROM Save ===
void saveLogsToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, logIndex);
    EEPROM.put(sizeof(int), logs);
    EEPROM.commit();
}

// === EEPROM Load ===
void loadLogsFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, logIndex);
    EEPROM.get(sizeof(int), logs);
    Serial.printf("Loaded %d logs from EEPROM.\n", logIndex);
}

// === BLE Scan Callback ===
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        int rssi = advertisedDevice.getRSSI();
        if (rssi < RSSI_THRESHOLD) return;
        if (!advertisedDevice.haveName()) return;

        string device_name = advertisedDevice.getName().c_str();
        if (device_name.rfind("GRP2", 0) != 0) return; 

        string peerID = advertisedDevice.getManufacturerData().c_str();

        duration += SCAN_INTERVAL_MS;

        if (logIndex < MAX_LOGS) {
            
            unsigned long timestamp = timeInitialized ? time(nullptr) : millis();

            if (duration >= EXPOSURE_WINDOW_MS && rssi >= RSSI_THRESHOLD) {

                digitalWrite(LED_BUILTIN, HIGH);

                logs[logIndex++] = {
                    .relativeTimestamp = timestamp,
                    .peerID = peerID,
                    .rssi = rssi,
                    .duration = duration
                };

                if (timeInitialized) {
                    time_t logTime = timestamp;
                    struct tm timeinfo;
                    localtime_r(&logTime, &timeinfo);
                    char timeStr[30];
                    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

                    Serial.printf("%s,%s,%d,%lu\n",
                        timeStr,
                        peerID.c_str(),
                        rssi,
                        duration
                    );
                }
            }
        } else {
            if(!printed) {
                saveLogsToEEPROM();
                printed = true;
                Serial.println("Done.");
            } else {
                digitalWrite(LED_BUILTIN, LOW);
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);

    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    
    initializeTime();
    
    logIndex = 0;
    
    BLEDevice::init("GRP2MAIN_CONTACT");
    BLEDevice::startAdvertising();
    
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    pAdvertising = BLEDevice::getAdvertising();
    currentID = generateRandomID();
    advertiseID(currentID);
    lastIDRefresh = millis();

    Serial.println("timestamp,peer_id,rssi,duration_ms");
}

void loop() {
    unsigned long now = millis();    
    
    if (now - lastIDRefresh >= ID_REFRESH_INTERVAL_MS) {
        currentID = generateRandomID();
        pAdvertising->stop();
        advertiseID(currentID);
        lastIDRefresh = now;
        Serial.println(("ID refreshed: " + currentID).c_str());
    }
    
    pBLEScan->start(SCAN_INTERVAL_MS / 1000, false);
    pBLEScan->clearResults();

    delay(5000);
}
