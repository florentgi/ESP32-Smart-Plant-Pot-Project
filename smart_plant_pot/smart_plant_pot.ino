#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionI2cScd4x.h>
#include <RotaryEncoder.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include "../secrets.h"

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================
// Pins
constexpr uint8_t PIN_LIGHT        = 34;
constexpr uint8_t PIN_SOIL         = 35;
constexpr uint8_t PIN_WATER_SIGNAL = 32;
constexpr uint8_t PIN_WATER_POWER  = 25;
constexpr uint8_t PIN_IN1          = 27;
constexpr uint8_t PIN_IN2          = 26;
constexpr uint8_t PIN_BTN          = 14;

// LCD
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLS    = 20;
constexpr uint8_t LCD_ROWS    = 4;
constexpr uint8_t LCD_BUF     = LCD_COLS + 1; // Safe buffer size for snprintf

// Timing
constexpr unsigned long SENSOR_UPDATE_INTERVAL = 2000;
constexpr unsigned long HISTORY_LOG_INTERVAL   = 60000;  // 10 mins (cloud log)
constexpr unsigned long PHOTO_INTERVAL         = 86400000UL; // 24 hours
constexpr unsigned long DEBOUNCE_DELAY         = 50;
constexpr unsigned long WIFI_RETRY_INTERVAL    = 30000; // 30s between reconnect attempts

// Sensor calibration
constexpr int SOIL_DRY_RAW   = 2933;
constexpr int SOIL_WET_RAW   = 2019;
constexpr int TANK_EMPTY_RAW = 0;
constexpr int TANK_FULL_RAW  = 1421;
constexpr int ADC_MAX        = 4095;

// History
constexpr uint8_t MAX_HISTORY = 144;

// Defaults (used when sensor read fails)
constexpr uint16_t DEFAULT_CO2      = 400;
constexpr float    DEFAULT_TEMP     = 22.0f;
constexpr float    DEFAULT_HUMIDITY = 50.0f;
constexpr float    DEFAULT_PH       = 7.0f;  // NOTE: No pH sensor wired; placeholder only

// =============================================================================
// ENUMS & STRUCTURES
// =============================================================================
enum class PlantMood : uint8_t { HAPPY, THIRSTY, OVERWATERED, EMPTY_TANK, SUNBURNT, COLD, TOO_DARK, HOT };
enum class Page      : uint8_t { HOME = 0, DATA = 1, RECORDS = 2, GRAPH = 3, PLANT = 4, COUNT = 5 };
enum class AssetType : uint8_t { BATTERY, GRAPH };

struct PlantProfile {
    const char* name;
    float minSoil, maxSoil;
    float minTemp, maxTemp;
    float minLight, maxLight;
    const char* advice;
};

struct PlantData {
    float    soilMoisture;
    float    waterLevel;
    float    lightLevel;
    float    pH;
    float    temperature;
    float    airHumidity;
    uint16_t co2;

    PlantData()
        : soilMoisture(0), waterLevel(0), lightLevel(0),
          pH(DEFAULT_PH), temperature(DEFAULT_TEMP),
          airHumidity(DEFAULT_HUMIDITY), co2(DEFAULT_CO2) {}
};

// =============================================================================
// PLANT DATABASE
// NOTE: PROGMEM is a no-op on ESP32 — removed to avoid misleading memcpy_P calls.
// Direct array access is safe and avoids pointer aliasing bugs.
// =============================================================================
const PlantProfile PLANT_DB[] = {
    {"Monstera",     30.0f, 80.0f,  18.0f, 29.0f,  30.0f, 90.0f,  "Dry top 5cm"     },
    {"Snake-Plant",  10.0f, 50.0f,  15.0f, 29.0f,  10.0f, 60.0f,  "Dry completely"  },
    {"Spider-Plant", 40.0f, 80.0f,  15.0f, 27.0f,  25.0f, 75.0f,  "Keep moist"      },
    {"Peace-Lily",   50.0f, 90.0f,  18.0f, 27.0f,  15.0f, 65.0f,  "Never dry out"   },
    {"Pothos",       25.0f, 75.0f,  15.0f, 29.0f,  20.0f, 80.0f,  "Dry top 3cm"     }
};
constexpr uint8_t PLANT_DB_SIZE = sizeof(PLANT_DB) / sizeof(PLANT_DB[0]);

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
SensirionI2cScd4x scd4x;
RotaryEncoder      encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);
FirebaseData       fbdo;
FirebaseAuth       auth;
FirebaseConfig     config;
bool               firebaseReady = false;

// =============================================================================
// WIFI / FIREBASE HELPERS
// =============================================================================
void connectWiFi() {
    Serial.print("\n[WiFi] Connecting to: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
        Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\n[WiFi] Connected! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WiFi] FAILED — check secrets.h");
    }
}

// Call periodically in loop() to recover from dropped connections.
void maintainWiFi() {
    static unsigned long lastRetry = 0;
    if (WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastRetry < WIFI_RETRY_INTERVAL) return;
    lastRetry = millis();
    Serial.println("[WiFi] Lost connection — retrying...");
    WiFi.reconnect();
}

void initFirebase() {
    Serial.println("\n[Firebase] Initializing...");
    config.api_key        = FIREBASE_API_KEY;
    config.database_url   = FIREBASE_DATABASE_URL;
    auth.user.email       = FIREBASE_USER_EMAIL;
    auth.user.password    = FIREBASE_USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;
    fbdo.setBSSLBufferSize(4096, 1024);
    fbdo.setResponseSize(4096);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    firebaseReady = true;
    Serial.println("[Firebase] Setup complete.");
}

// =============================================================================
// ASSET MANAGER (LCD CUSTOM CHARACTERS)
// =============================================================================
const byte BAR_CHARS[8][8] = {
    {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,31}, {0,0,0,0,0,0,31,31},
    {0,0,0,0,0,31,31,31}, {0,0,0,0,31,31,31,31}, {0,0,0,31,31,31,31,31},
    {0,0,31,31,31,31,31,31}, {31,31,31,31,31,31,31,31}
};

const byte BAT_TL_E[8] = {B00011,B00011,B11111,B10000,B10000,B10000,B10000,B10000};
const byte BAT_TR_E[8] = {B11000,B11000,B11111,B00001,B00001,B00001,B00001,B00001};
const byte BAT_TL_F[8] = {B00011,B00011,B11111,B11111,B11111,B11111,B11111,B11111};
const byte BAT_TR_F[8] = {B11000,B11000,B11111,B11111,B11111,B11111,B11111,B11111};
const byte BAT_ML_E[8] = {B10000,B10000,B10000,B10000,B10000,B10000,B10000,B10000};
const byte BAT_MR_E[8] = {B00001,B00001,B00001,B00001,B00001,B00001,B00001,B00001};
const byte BAT_BL_F[8] = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111};
const byte BAT_BR_F[8] = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111};

class AssetManager {
    AssetType currentAsset;
public:
    AssetManager() : currentAsset(AssetType::BATTERY) {}

    void loadAssets(AssetType type) {
        if (currentAsset == type) return;
        currentAsset = type;
        if (type == AssetType::BATTERY) loadBatteryAssets();
        else                            loadGraphAssets();
    }

private:
    void loadBatteryAssets() {
        lcd.createChar(0, const_cast<byte*>(BAT_TL_E));
        lcd.createChar(1, const_cast<byte*>(BAT_TR_E));
        lcd.createChar(2, const_cast<byte*>(BAT_TL_F));
        lcd.createChar(3, const_cast<byte*>(BAT_TR_F));
        lcd.createChar(4, const_cast<byte*>(BAT_ML_E));
        lcd.createChar(5, const_cast<byte*>(BAT_MR_E));
        lcd.createChar(6, const_cast<byte*>(BAT_BL_F));
        lcd.createChar(7, const_cast<byte*>(BAT_BR_F));
    }

    void loadGraphAssets() {
        for (uint8_t i = 0; i < 8; i++)
            lcd.createChar(i, const_cast<byte*>(BAR_CHARS[i]));
    }
};

// =============================================================================
// BUTTON HANDLER
// =============================================================================
class ButtonHandler {
    uint8_t       pin;
    bool          lastState;
    unsigned long lastDebounceTime;
    bool          debouncedState;
public:
    ButtonHandler(uint8_t p) : pin(p), lastState(HIGH), lastDebounceTime(0), debouncedState(HIGH) {}

    void begin() {
        pinMode(pin, INPUT_PULLUP);
        debouncedState = digitalRead(pin);
        lastState      = debouncedState;
    }

    bool wasPressed() {
        bool current = digitalRead(pin);
        bool pressed = false;
        if (current != lastState) lastDebounceTime = millis();
        if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
            if (current != debouncedState) {
                debouncedState = current;
                if (debouncedState == LOW) pressed = true;
            }
        }
        lastState = current;
        return pressed;
    }
};

// =============================================================================
// SENSOR MANAGER
// =============================================================================
class SensorManager {
public:
    static float getLightPercentage() {
        return constrain((analogRead(PIN_LIGHT) / (float)ADC_MAX) * 100.0f, 0.0f, 100.0f);
    }

    static float getSoilMoisture() {
        return constrain(map(analogRead(PIN_SOIL), SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100);
    }

    static float getWaterLevel() {
        digitalWrite(PIN_WATER_POWER, HIGH);
        delay(10);
        int raw = analogRead(PIN_WATER_SIGNAL);
        digitalWrite(PIN_WATER_POWER, LOW);
        return constrain(map(raw, TANK_EMPTY_RAW, TANK_FULL_RAW, 0, 100), 0, 100);
    }

    // Returns false if data is not ready or read fails; caller should use defaults.
    static bool readCO2Sensor(uint16_t& co2, float& temp, float& humidity) {
        bool isReady = false;
        if (scd4x.getDataReadyStatus(isReady) != 0 || !isReady) return false;
        if (scd4x.readMeasurement(co2, temp, humidity) != 0)     return false;
        return true;
    }
};

// =============================================================================
// LCD HELPER
// =============================================================================
class LCDHelper {
public:
    static void printCentered(uint8_t row, const char* text) {
        uint8_t len = strlen(text);
        uint8_t pos = (len < LCD_COLS) ? (LCD_COLS - len) / 2 : 0;
        clearLine(row);
        lcd.setCursor(pos, row);
        lcd.print(text);
    }

    static void clearLine(uint8_t row) {
        lcd.setCursor(0, row);
        for (uint8_t i = 0; i < LCD_COLS; i++) lcd.print(' ');
    }
};

// =============================================================================
// SMART POT — Core logic
// =============================================================================
class SmartPot {
    PlantData  currentData;
    PlantMood  currentMood;
    PlantData  history[MAX_HISTORY];
    uint8_t    historyIndex;
    uint8_t    historyCount;
    bool       lastUploadOk;         // Track last Firebase result for UI indicator

    static uint8_t selectedPlantIndex;

public:
    SmartPot()
        : currentMood(PlantMood::HAPPY),
          historyIndex(0), historyCount(0), lastUploadOk(false) {}

    // --- Plant selection ---
    static uint8_t getSelectedPlantIndex() { return selectedPlantIndex; }

    static void selectNextPlant() {
        selectedPlantIndex = (selectedPlantIndex + 1) % PLANT_DB_SIZE;
    }
    static void selectPreviousPlant() {
        selectedPlantIndex = (selectedPlantIndex == 0) ? PLANT_DB_SIZE - 1 : selectedPlantIndex - 1;
    }

    // --- History ---
    void logHistory() {
        history[historyIndex] = currentData;
        historyIndex = (historyIndex + 1) % MAX_HISTORY;
        if (historyCount < MAX_HISTORY) historyCount++;
    }

    // Returns false if the requested item doesn't exist yet.
    bool getHistoryItem(uint8_t ago, PlantData& out) const {
        if (ago >= historyCount) return false;
        int idx = historyIndex - 1 - (int)ago;
        if (idx < 0) idx += MAX_HISTORY;
        out = history[idx];
        return true;
    }

    void printHistory() const {
        Serial.printf(">> Local records: %d | Last soil: %.0f%%\n",
                      historyCount, currentData.soilMoisture);
    }

    // --- Firebase upload ---
    // Avoids String heap allocation by using stack char buffers throughout.
    bool sendToFirebase() {
        if (!firebaseReady || WiFi.status() != WL_CONNECTED) {
            Serial.println("[Firebase] Skipped: no WiFi.");
            lastUploadOk = false;
            return false;
        }

        const PlantProfile& profile = PLANT_DB[selectedPlantIndex];

        // Stack buffer — no heap allocation
        char path[48];
        snprintf(path, sizeof(path), "/history/%s", profile.name);

        FirebaseJson json;
        json.set("timestamp/.sv", "timestamp");
        json.set("plant_name",    profile.name);
        json.set("plant_id",      (int)selectedPlantIndex);
        json.set("status",        getMoodText());
        json.set("soil",          currentData.soilMoisture);
        json.set("sun",           currentData.lightLevel);
        json.set("temp",          currentData.temperature);
        json.set("co2",           (int)currentData.co2);
        json.set("h2o",           currentData.waterLevel);
        json.set("ph",            currentData.pH);
        json.set("hum",           currentData.airHumidity);

        Serial.print(">> Uploading to: ");
        Serial.println(path);

        if (Firebase.pushJSON(fbdo, path, json)) {
            Serial.println(">> SUCCESS");
            lastUploadOk = true;
            return true;
        }

        // Avoids String concatenation with +
        Serial.print(">> ERROR: ");
        Serial.println(fbdo.errorReason());
        lastUploadOk = false;
        return false;
    }

    bool getLastUploadOk() const { return lastUploadOk; }

    // --- Sensor update & mood ---
    void updateSensors(float m, float w, float l, float p, float t, float h, uint16_t c) {
        currentData.soilMoisture = m;
        currentData.waterLevel   = w;
        currentData.lightLevel   = l;
        currentData.pH           = p;
        currentData.temperature  = t;
        currentData.airHumidity  = h;
        currentData.co2          = c;
        evaluateMood();
    }

    void evaluateMood() {
        const PlantProfile& p = PLANT_DB[selectedPlantIndex];

        // Priority order: safety-critical first, then environment
        if (currentData.waterLevel   < 10.0f)  { currentMood = PlantMood::EMPTY_TANK;   return; }
        if (currentData.soilMoisture < p.minSoil) { currentMood = PlantMood::THIRSTY;   return; }
        if (currentData.soilMoisture > p.maxSoil) { currentMood = PlantMood::OVERWATERED; return; }
        if (currentData.temperature  < p.minTemp) { currentMood = PlantMood::COLD;       return; }
        if (currentData.temperature  > p.maxTemp) { currentMood = PlantMood::HOT;        return; }
        if (currentData.lightLevel   > p.maxLight){ currentMood = PlantMood::SUNBURNT;   return; }
        if (currentData.lightLevel   < p.minLight){ currentMood = PlantMood::TOO_DARK;   return; }
        currentMood = PlantMood::HAPPY;
    }

    const char* getMoodText() const {
        switch (currentMood) {
            case PlantMood::HAPPY:       return "Happy";
            case PlantMood::THIRSTY:     return "Thirsty!";
            case PlantMood::OVERWATERED: return "Too Wet!";
            case PlantMood::EMPTY_TANK:  return "Refill Tank!";
            case PlantMood::SUNBURNT:    return "Sunburnt!";
            case PlantMood::HOT:         return "Too Hot!";
            case PlantMood::COLD:        return "Too Cold!";
            case PlantMood::TOO_DARK:    return "Need Light!";
            default:                     return "Unknown";
        }
    }

    int getHealthLevel() const {
        switch (currentMood) {
            case PlantMood::HAPPY:                                              return 3;
            case PlantMood::THIRSTY: case PlantMood::COLD:
            case PlantMood::HOT:     case PlantMood::TOO_DARK:                 return 2;
            default:                                                            return 1;
        }
    }

    // ==========================================================================
    // PAGE DRAWING
    // ==========================================================================

    // HOME — battery icon + sync indicator in top-right corner
    void drawPageHome() const {
        const PlantProfile& profile = PLANT_DB[selectedPlantIndex];
        int health = getHealthLevel();
        char buf[LCD_BUF];

        // Row 0: title + sync dot ('+' = ok, '!' = failed/pending)
        snprintf(buf, LCD_BUF, "The Smart Gardener %c", lastUploadOk ? '+' : '!');
        lcd.setCursor(0, 0); lcd.print(buf);

        snprintf(buf, LCD_BUF, "%-16s", profile.name);
        lcd.setCursor(0, 1); lcd.print(buf);

        snprintf(buf, LCD_BUF, "%-16s", getMoodText());
        lcd.setCursor(0, 2); lcd.print(buf);

        snprintf(buf, LCD_BUF, "Soil: %.0f%%       ", currentData.soilMoisture);
        lcd.setCursor(0, 3); lcd.print(buf);

        // Battery icon (cols 18–19)
        lcd.setCursor(18, 1); lcd.write(health >= 3 ? 2 : 0); lcd.write(health >= 3 ? 3 : 1);
        lcd.setCursor(18, 2); lcd.write(health >= 2 ? (byte)255 : 4); lcd.write(health >= 2 ? (byte)255 : 5);
        lcd.setCursor(18, 3); lcd.write(6); lcd.write(7);
    }

    void drawPageData() const {
        char buf[LCD_BUF];
        lcd.setCursor(0, 0); snprintf(buf, LCD_BUF, "Soil:%3.0f%%  H2O:%3.0f%%", currentData.soilMoisture, currentData.waterLevel); lcd.print(buf);
        lcd.setCursor(0, 1); snprintf(buf, LCD_BUF, "Sun :%3.0f%%  pH : N/A ", currentData.lightLevel);                             lcd.print(buf);
        lcd.setCursor(0, 2); snprintf(buf, LCD_BUF, "Temp:%2.0fC   Hum:%3.0f%%", currentData.temperature, currentData.airHumidity); lcd.print(buf);
        lcd.setCursor(0, 3); snprintf(buf, LCD_BUF, "CO2 :%4d ppm        ",  currentData.co2);                                      lcd.print(buf);
    }

    void drawPageRecords() const {
        lcd.setCursor(0, 0); lcd.print("--- 24H RECORDS --- ");
        if (historyCount == 0) {
            lcd.setCursor(0, 1); lcd.print(" Collecting data... ");
            return;
        }

        PlantData first;
        getHistoryItem(0, first);
        float minT = first.temperature, maxT = first.temperature;
        float minS = first.soilMoisture, maxS = first.soilMoisture;

        PlantData r;
        for (uint8_t i = 1; i < historyCount; i++) {
            if (!getHistoryItem(i, r)) continue;
            if (r.temperature < minT) minT = r.temperature;
            if (r.temperature > maxT) maxT = r.temperature;
            if (r.soilMoisture < minS) minS = r.soilMoisture;
            if (r.soilMoisture > maxS) maxS = r.soilMoisture;
        }

        char buf[LCD_BUF];
        lcd.setCursor(0, 1); snprintf(buf, LCD_BUF, "Temp:%2.0fC - %2.0fC    ", minT, maxT); lcd.print(buf);
        lcd.setCursor(0, 2); snprintf(buf, LCD_BUF, "Soil:%2.0f%% - %2.0f%%   ", minS, maxS); lcd.print(buf);
        lcd.setCursor(0, 3); lcd.print("Samples: "); lcd.print(historyCount);
    }

    void drawPageGraph() const {
        lcd.setCursor(0, 0); lcd.print("100%   Soil Trend   ");
        lcd.setCursor(0, 3); lcd.print("0%   (Last 2.5 Hrs) ");

        PlantData item;
        for (uint8_t i = 0; i < LCD_COLS; i++) {
            uint8_t col = LCD_COLS - 1 - i;
            if (getHistoryItem(i, item)) drawGraphColumn(col, item.soilMoisture);
            else { lcd.setCursor(col, 1); lcd.print(' '); lcd.setCursor(col, 2); lcd.print(' '); }
        }
    }

    void drawPagePlant(bool isEditing) const {
        const PlantProfile& p = PLANT_DB[selectedPlantIndex];
        char buf[LCD_BUF];

        lcd.setCursor(0, 0); lcd.print("--- SELECT PLANT ---");
        LCDHelper::clearLine(1);
        lcd.setCursor(0, 1);
        if (isEditing) { lcd.print("> "); lcd.print(p.name); lcd.print(" <"); }
        else           { lcd.print("  "); lcd.print(p.name); lcd.print("    "); }

        lcd.setCursor(0, 2);
        snprintf(buf, LCD_BUF, "S:%.0f-%.0f%% T:%.0f-%.0fC",
                 p.minSoil, p.maxSoil, p.minTemp, p.maxTemp);
        lcd.print(buf);

        lcd.setCursor(0, 3);
        lcd.print(isEditing ? p.advice : "[CLICK TO EDIT]     ");
    }

private:
    void drawGraphColumn(uint8_t col, float val) const {
        int h = map(constrain((int)val, 0, 100), 0, 100, 0, 15);
        uint8_t upper = (h >= 8) ? min(h - 8, 7) : 0;
        uint8_t lower = (h >= 8) ? 7 : h;
        lcd.setCursor(col, 1); if (upper > 0) lcd.write(upper); else lcd.print(' ');
        lcd.setCursor(col, 2); if (lower > 0) lcd.write(lower); else lcd.print('_');
    }
};
uint8_t SmartPot::selectedPlantIndex = 0;

// =============================================================================
// GLOBALS
// =============================================================================
SmartPot     myPlant;
AssetManager assetManager;
ButtonHandler button(PIN_BTN);

Page currentPage   = Page::HOME;
bool isEditingPlant = false;

unsigned long lastSensorUpdate = 0;
unsigned long lastHistoryLog   = 0;
unsigned long lastPhotoTime    = 0;

// Pending upload flag — retries on next loop if WiFi was down
bool pendingUpload = false;

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.clear();

    Serial.println("\n--- DEBUG CONFIG ---");
    Serial.print("API KEY length: "); Serial.println(strlen(FIREBASE_API_KEY));
    Serial.print("DB URL: ");         Serial.println(FIREBASE_DATABASE_URL);

    assetManager.loadAssets(AssetType::BATTERY);

    scd4x.begin(Wire, 0x62);
    scd4x.stopPeriodicMeasurement();
    delay(500);
    scd4x.startPeriodicMeasurement();

    pinMode(PIN_LIGHT,        INPUT);
    pinMode(PIN_SOIL,         INPUT);
    pinMode(PIN_WATER_SIGNAL, INPUT);
    pinMode(PIN_WATER_POWER,  OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW);

    lastPhotoTime = millis();
    button.begin();

    LCDHelper::printCentered(1, "Connecting WiFi...");
    connectWiFi();
    LCDHelper::printCentered(1, "Init Firebase...");
    initFirebase();
    lcd.clear();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    // --- WiFi watchdog ---
    maintainWiFi();

    // --- Encoder ---
    static int lastEncoderPos = 0;
    encoder.tick();
    int pos = encoder.getPosition();

    if (pos != lastEncoderPos) {
        if (isEditingPlant) {
            if (pos > lastEncoderPos) SmartPot::selectNextPlant();
            else                      SmartPot::selectPreviousPlant();
        } else {
            int p = abs(pos) % (int)Page::COUNT;
            Page newPage = static_cast<Page>(p);
            if (newPage == Page::GRAPH && currentPage != Page::GRAPH) assetManager.loadAssets(AssetType::GRAPH);
            else if (newPage != Page::GRAPH && currentPage == Page::GRAPH) assetManager.loadAssets(AssetType::BATTERY);
            currentPage = newPage;
        }
        lcd.clear();
        lastSensorUpdate = 0;
        lastEncoderPos   = pos;
    }

    // --- Button ---
    if (button.wasPressed()) {
        if (currentPage == Page::PLANT) {
            isEditingPlant = !isEditingPlant;
            lcd.clear();
            lastSensorUpdate = 0;
        } else if (currentPage == Page::HOME) {
            // Manual camera trigger
            Serial.println(">> CLOUD COMMAND: Triggering camera...");
            lcd.setCursor(0, 0); lcd.print(">> SAY CHEESE! <<   ");

            if (Firebase.setBool(fbdo, "/camera/trigger", true)) {
                Serial.println(">> Trigger sent.");
                // Schedule reset so a reboot won't retrigger
                Firebase.setBool(fbdo, "/camera/trigger", false);
            } else {
                Serial.print(">> Trigger failed: ");
                Serial.println(fbdo.errorReason());
            }

            delay(2000);
            lcd.clear();
            lastSensorUpdate = 0;
        }
    }

    // --- Sensor update & display ---
    if (millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdate = millis();

        float    l = SensorManager::getLightPercentage();
        float    s = SensorManager::getSoilMoisture();
        float    w = SensorManager::getWaterLevel();
        uint16_t c; float t, h;
        if (!SensorManager::readCO2Sensor(c, t, h)) { c = DEFAULT_CO2; t = DEFAULT_TEMP; h = DEFAULT_HUMIDITY; }

        myPlant.updateSensors(s, w, l, DEFAULT_PH, t, h, c);

        switch (currentPage) {
            case Page::HOME:    myPlant.drawPageHome();               break;
            case Page::DATA:    myPlant.drawPageData();               break;
            case Page::RECORDS: myPlant.drawPageRecords();            break;
            case Page::GRAPH:   myPlant.drawPageGraph();              break;
            case Page::PLANT:   myPlant.drawPagePlant(isEditingPlant); break;
        }
    }

    // --- Firebase logging + retry ---
    if (millis() - lastHistoryLog >= HISTORY_LOG_INTERVAL) {
        lastHistoryLog = millis();
        myPlant.logHistory();
        myPlant.printHistory();
        pendingUpload = true; // Mark for upload
    }

    // Attempt upload separately so a retry doesn't reset the log timer
    if (pendingUpload && WiFi.status() == WL_CONNECTED) {
        if (myPlant.sendToFirebase()) pendingUpload = false;
        // If it fails, pendingUpload stays true and retries next loop iteration
    }

    // --- Auto daily photo ---
    if (millis() - lastPhotoTime >= PHOTO_INTERVAL) {
        lastPhotoTime = millis();
        Serial.println(">> AUTO CLOUD COMMAND: Daily photo trigger.");
        if (Firebase.setBool(fbdo, "/camera/trigger", true)) {
            Firebase.setBool(fbdo, "/camera/trigger", false); // Reset after set
        }
    }
}
