#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionI2cScd4x.h>
#include <RotaryEncoder.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include "../secrets.h"

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================

// Hardware Pins
constexpr uint8_t PIN_LIGHT = 34;
constexpr uint8_t PIN_SOIL = 35;
constexpr uint8_t PIN_WATER_SIGNAL = 32;
constexpr uint8_t PIN_WATER_POWER = 25;
constexpr uint8_t PIN_IN1 = 27;
constexpr uint8_t PIN_IN2 = 26;
constexpr uint8_t PIN_BTN = 14;

// LCD Configuration
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLS = 20;
constexpr uint8_t LCD_ROWS = 4;

// Timing Constants
constexpr unsigned long SENSOR_UPDATE_INTERVAL = 2000;      // 2 seconds
constexpr unsigned long HISTORY_LOG_INTERVAL = 600000;      // 10 minutes
constexpr unsigned long DEBOUNCE_DELAY = 50;                // 50ms

// History Settings
constexpr uint8_t MAX_HISTORY = 144;  // 24 hours at 10-min intervals

// Sensor Calibration
constexpr int SOIL_DRY_RAW = 2933;
constexpr int SOIL_WET_RAW = 2019;
constexpr int TANK_EMPTY_RAW = 0;
constexpr int TANK_FULL_RAW = 1421;
constexpr int ADC_MAX = 4095;

// Sensor Validation Ranges
constexpr float MIN_VALID_TEMP = -10.0f;
constexpr float MAX_VALID_TEMP = 60.0f;
constexpr float MIN_VALID_HUMIDITY = 0.0f;
constexpr float MAX_VALID_HUMIDITY = 100.0f;
constexpr uint16_t MIN_VALID_CO2 = 300;
constexpr uint16_t MAX_VALID_CO2 = 5000;

// Default Fallback Values
constexpr uint16_t DEFAULT_CO2 = 400;
constexpr float DEFAULT_TEMP = 22.0f;
constexpr float DEFAULT_HUMIDITY = 50.0f;
constexpr float DEFAULT_PH = 7.0f;

// Graph Settings
constexpr uint8_t GRAPH_HEIGHT = 16;  // 2 rows * 8 pixels
constexpr uint8_t GRAPH_UPPER_ROW = 1;
constexpr uint8_t GRAPH_LOWER_ROW = 2;
constexpr uint8_t PIXELS_PER_ROW = 8;

// =============================================================================
// ENUMS & STRUCTURES
// =============================================================================

enum class PlantMood : uint8_t {
    HAPPY, THIRSTY, OVERWATERED, EMPTY_TANK, SUNBURNT, COLD, TOO_DARK, HOT
};

enum class Page : uint8_t {
    EMOJI = 0, DATA = 1, RECORDS = 2, GRAPH = 3, PLANT = 4, COUNT = 5
};

enum class AssetType : uint8_t { FACE, GRAPH };

struct PlantProfile {
    const char* name;
    float minSoil; float maxSoil;
    float minTemp; float maxTemp;
    float minLight; float maxLight;
};

struct PlantData {
    float soilMoisture; float waterLevel; float lightLevel; float pH;
    float temperature; float airHumidity; uint16_t co2;
    
    PlantData() : soilMoisture(0), waterLevel(0), lightLevel(0), 
                  pH(DEFAULT_PH), temperature(DEFAULT_TEMP), 
                  airHumidity(DEFAULT_HUMIDITY), co2(DEFAULT_CO2) {}
};

// =============================================================================
// PLANT DATABASE
// =============================================================================

const PlantProfile PLANT_DB[] PROGMEM = {
    {"Generic", 20.0f, 85.0f, 10.0f, 35.0f, 0.0f, 90.0f},
    {"Cactus",  5.0f,  40.0f, 15.0f, 40.0f, 40.0f, 100.0f},
    {"Fern",    50.0f, 90.0f, 18.0f, 28.0f, 10.0f, 60.0f},
    {"Basil",   30.0f, 80.0f, 20.0f, 30.0f, 50.0f, 100.0f},
    {"Orchid",  20.0f, 60.0f, 18.0f, 32.0f, 20.0f, 50.0f}
};
constexpr uint8_t PLANT_DB_SIZE = sizeof(PLANT_DB) / sizeof(PLANT_DB[0]);

// =============================================================================
// CUSTOM LCD CHARACTERS
// =============================================================================

// Graph Bar Characters (0-7)
const byte BAR_CHARS[8][8] PROGMEM = {
    {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,31}, {0,0,0,0,0,0,31,31}, {0,0,0,0,0,31,31,31},
    {0,0,0,0,31,31,31,31}, {0,0,0,31,31,31,31,31}, {0,0,31,31,31,31,31,31}, {31,31,31,31,31,31,31,31}
};

// Face Characters (Eyes, Mouth, and Cheeks for 3-row look)
const byte FACE_EYE_OPEN[8] PROGMEM    = {B00111, B01111, B11111, B11111, B11111, B11111, B01111, B00111};
const byte FACE_EYE_CLOSED[8] PROGMEM  = {B00000, B00000, B00000, B00000, B11111, B11111, B00000, B00000};
const byte FACE_MOUTH_HAPPY_L[8] PROGMEM = {B00000, B00000, B10000, B11000, B11100, B01110, B00111, B00011};
const byte FACE_MOUTH_HAPPY_R[8] PROGMEM = {B00000, B00000, B00001, B00011, B00111, B01110, B11100, B11000};
const byte FACE_MOUTH_SAD_L[8] PROGMEM   = {B00011, B00111, B01110, B11100, B11000, B10000, B00000, B00000};
const byte FACE_MOUTH_SAD_R[8] PROGMEM   = {B11000, B11100, B01110, B00111, B00011, B00001, B00000, B00000};
// NEW: Cheeks/Body to connect eyes and mouth
const byte FACE_BODY_L[8] PROGMEM        = {B11000, B11000, B11000, B11000, B11000, B11000, B11000, B11000};
const byte FACE_BODY_R[8] PROGMEM        = {B00011, B00011, B00011, B00011, B00011, B00011, B00011, B00011};

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
SensirionI2cScd4x scd4x;
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);

// Firebase Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;

// =============================================================================
// WIFI & FIREBASE HELPER FUNCTIONS
// =============================================================================

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
}

void initFirebase() {
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DATABASE_URL;
    
    // Anonymous authentication
    auth.user.email = "";
    auth.user.password = "";
    
    config.token_status_callback = tokenStatusCallback;
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    fbdo.setResponseSize(4096);
    firebaseReady = true;
    Serial.println("Firebase initialized!");
}

// =============================================================================
// ASSET MANAGER CLASS
// =============================================================================

class AssetManager {
private:
    AssetType currentAsset;
    
public:
    AssetManager() : currentAsset(AssetType::FACE) {}
    
    void loadAssets(AssetType type) {
        if (currentAsset == type) return; 
        currentAsset = type;
        if (type == AssetType::FACE) loadFaceAssets();
        else if (type == AssetType::GRAPH) loadGraphAssets();
    }
    
private:
    void loadFaceAssets() {
        byte temp[8];
        memcpy_P(temp, FACE_EYE_OPEN, 8);      lcd.createChar(0, temp);
        memcpy_P(temp, FACE_EYE_CLOSED, 8);    lcd.createChar(1, temp);
        memcpy_P(temp, FACE_MOUTH_HAPPY_L, 8); lcd.createChar(2, temp);
        memcpy_P(temp, FACE_MOUTH_HAPPY_R, 8); lcd.createChar(3, temp);
        memcpy_P(temp, FACE_MOUTH_SAD_L, 8);   lcd.createChar(4, temp);
        memcpy_P(temp, FACE_MOUTH_SAD_R, 8);   lcd.createChar(5, temp);
        memcpy_P(temp, FACE_BODY_L, 8);        lcd.createChar(6, temp); // Left Cheek
        memcpy_P(temp, FACE_BODY_R, 8);        lcd.createChar(7, temp); // Right Cheek
    }
    
    void loadGraphAssets() {
        byte temp[8];
        for (uint8_t i = 0; i < 8; i++) {
            memcpy_P(temp, BAR_CHARS[i], 8);
            lcd.createChar(i, temp);
        }
    }
};

// =============================================================================
// BUTTON HANDLER CLASS
// =============================================================================

class ButtonHandler {
private:
    uint8_t pin;
    bool lastState;
    unsigned long lastDebounceTime;
    bool debouncedState;
    
public:
    ButtonHandler(uint8_t buttonPin) : pin(buttonPin), lastState(HIGH), lastDebounceTime(0), debouncedState(HIGH) {}
    
    void begin() {
        pinMode(pin, INPUT_PULLUP);
        debouncedState = digitalRead(pin);
        lastState = debouncedState;
    }
    
    bool wasPressed() {
        bool currentState = digitalRead(pin);
        bool pressed = false;
        
        if (currentState != lastState) lastDebounceTime = millis();
        
        if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
            if (currentState != debouncedState) {
                debouncedState = currentState;
                if (debouncedState == LOW) pressed = true;
            }
        }
        lastState = currentState;
        return pressed;
    }
};

// =============================================================================
// SENSOR MANAGER CLASS
// =============================================================================

class SensorManager {
public:
    static float getLightPercentage() {
        return constrain((analogRead(PIN_LIGHT) / (float)ADC_MAX) * 100.0f, 0.0f, 100.0f);
    }
    
    static float getSoilMoisture() {
        int raw = analogRead(PIN_SOIL);
        return constrain(map(raw, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100);
    }
    
    static float getWaterLevel() {
        digitalWrite(PIN_WATER_POWER, HIGH); delay(10);
        int raw = analogRead(PIN_WATER_SIGNAL); digitalWrite(PIN_WATER_POWER, LOW);
        return constrain(map(raw, TANK_EMPTY_RAW, TANK_FULL_RAW, 0, 100), 0, 100);
    }
    
    static bool readCO2Sensor(uint16_t &co2, float &temp, float &humidity) {
        bool isReady = false;
        if (scd4x.getDataReadyStatus(isReady) != 0) return false;
        if (!isReady) return false;
        if (scd4x.readMeasurement(co2, temp, humidity) != 0) return false;
        
        if (temp < MIN_VALID_TEMP || temp > MAX_VALID_TEMP) return false;
        if (humidity < MIN_VALID_HUMIDITY || humidity > MAX_VALID_HUMIDITY) return false;
        if (co2 < MIN_VALID_CO2 || co2 > MAX_VALID_CO2) return false;
        return true;
    }
};

// =============================================================================
// LCD HELPER CLASS
// =============================================================================

class LCDHelper {
public:
    static void printCentered(uint8_t row, const char* text) {
        uint8_t len = strlen(text);
        uint8_t pos = (len < LCD_COLS) ? (LCD_COLS - len) / 2 : 0;
        lcd.setCursor(0, row);
        for (uint8_t i = 0; i < LCD_COLS; i++) lcd.print(' ');
        lcd.setCursor(pos, row);
        lcd.print(text);
    }
    
    static void clearLine(uint8_t row) {
        lcd.setCursor(0, row);
        for (uint8_t i = 0; i < LCD_COLS; i++) lcd.print(' ');
    }
};

// =============================================================================
// SMART POT CLASS
// =============================================================================

class SmartPot {
private:
    PlantData currentData;
    PlantMood currentMood;
    PlantData history[MAX_HISTORY];
    uint8_t historyIndex;
    uint8_t historyCount;
    
public:
    SmartPot() : currentMood(PlantMood::HAPPY), historyIndex(0), historyCount(0) {}
    
    // --- History Management ---
    void logHistory() {
        history[historyIndex] = currentData;
        historyIndex = (historyIndex + 1) % MAX_HISTORY;
        if (historyCount < MAX_HISTORY) historyCount++;
        Serial.printf(">> LOG SAVED. Total Records: %d\n", historyCount);
    }
    
    PlantData getHistoryItem(uint8_t ago) const {
        if (ago >= historyCount) return PlantData();
        int targetIndex = historyIndex - 1 - ago;
        if (targetIndex < 0) targetIndex += MAX_HISTORY;
        return history[targetIndex];
    }
    
    void printHistory() const {
        Serial.println("\n====== 24H DATA HISTORY ======");
        for (uint8_t i = 0; i < historyCount; i++) {
            PlantData record = getHistoryItem(i);
            Serial.printf("-%3d min | Soil: %.0f%% | Temp: %.1fC\n", i * 10, record.soilMoisture, record.temperature);
        }
        Serial.println("==============================\n");
    }
    
    // --- Firebase Data Upload ---
    bool sendToFirebase() {
        if (!firebaseReady || WiFi.status() != WL_CONNECTED) {
            Serial.println("Firebase not ready or WiFi disconnected");
            return false;
        }
        
        // Get plant name
        PlantProfile profile;
        memcpy_P(&profile, &PLANT_DB[getSelectedPlantIndex()], sizeof(PlantProfile));
        
        // Create JSON object
        FirebaseJson json;
        json.set("timestamp/.sv", "timestamp");  // Server timestamp
        json.set("soilMoisture", currentData.soilMoisture);
        json.set("waterLevel", currentData.waterLevel);
        json.set("lightLevel", currentData.lightLevel);
        json.set("pH", currentData.pH);
        json.set("temperature", currentData.temperature);
        json.set("airHumidity", currentData.airHumidity);
        json.set("co2", currentData.co2);
        json.set("plantType", profile.name);
        json.set("mood", getMoodText());
        
        // Push to Firebase (creates unique ID for each reading)
        String path = "/plant_readings";
        if (Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &json)) {
            Serial.println(">> Data sent to Firebase successfully!");
            Serial.print("   Path: ");
            Serial.println(fbdo.dataPath());
            return true;
        } else {
            Serial.print("Firebase error: ");
            Serial.println(fbdo.errorReason());
            return false;
        }
    }
    
    // --- Sensor & Logic ---
    void updateSensors(float moisture, float water, float light, float ph, float temp, float hum, uint16_t co2Val) {
        currentData.soilMoisture = moisture;
        currentData.waterLevel = water;
        currentData.lightLevel = light;
        currentData.pH = ph;
        currentData.temperature = temp;
        currentData.airHumidity = hum;
        currentData.co2 = co2Val;
        evaluateMood();
    }
    
    void evaluateMood() {
        PlantProfile profile;
        memcpy_P(&profile, &PLANT_DB[getSelectedPlantIndex()], sizeof(PlantProfile));
        
        if (currentData.waterLevel < 10.0f) currentMood = PlantMood::EMPTY_TANK;
        else if (currentData.soilMoisture < profile.minSoil) currentMood = PlantMood::THIRSTY;
        else if (currentData.soilMoisture > profile.maxSoil) currentMood = PlantMood::OVERWATERED;
        else if (currentData.temperature < profile.minTemp) currentMood = PlantMood::COLD;
        else if (currentData.temperature > profile.maxTemp) currentMood = PlantMood::HOT;
        else if (currentData.lightLevel > profile.maxLight) currentMood = PlantMood::SUNBURNT;
        else if (currentData.lightLevel < profile.minLight) currentMood = PlantMood::TOO_DARK;
        else currentMood = PlantMood::HAPPY;
    }
    
    const char* getMoodText() const {
        switch (currentMood) {
            case PlantMood::HAPPY: return "Happy"; case PlantMood::THIRSTY: return "Thirsty!";
            case PlantMood::OVERWATERED: return "Too Wet!"; case PlantMood::EMPTY_TANK: return "Refill Tank!";
            case PlantMood::SUNBURNT: return "Sunburnt!"; case PlantMood::HOT: return "Too Hot!";
            case PlantMood::COLD: return "Too Cold!"; case PlantMood::TOO_DARK: return "Need Light!";
            default: return "Unknown";
        }
    }
    
    bool isHappy() const { return currentMood == PlantMood::HAPPY; }
    
    // --- Drawing Methods ---
    
    void drawPageEmoji() const {
        PlantProfile profile;
        memcpy_P(&profile, &PLANT_DB[getSelectedPlantIndex()], sizeof(PlantProfile));
        bool happy = isHappy();

        // --- LEFT SIDE: TEXT INFO ---
        
        // Row 0: Plant Name
        // We use %-16s to print the name and pad the rest with spaces to clear old text
        char buf[17]; 
        snprintf(buf, 17, "%-16s", profile.name); 
        lcd.setCursor(0, 0); 
        lcd.print(buf);

        // Row 1 & 2: Clear the middle left area (Optional: could put Temp/Hum here later)
        lcd.setCursor(0, 1); lcd.print("                ");
        lcd.setCursor(0, 2); lcd.print("                ");

        // Row 3: Status Message
        // We also use %-16s to ensure "Happy" clears out "Refill Tank!"
        const char* status = getMoodText();
        snprintf(buf, 17, "%-16s", status);
        lcd.setCursor(0, 3); 
        lcd.print(buf);

        // --- RIGHT SIDE: THE 4-ROW TOTEM FACE ---
        
        // Row 0: Eyes
        lcd.setCursor(17, 0); lcd.write(happy ? 0 : 1);
        lcd.setCursor(18, 0); lcd.write(happy ? 0 : 1);

        // Row 1: Upper Body (Connector)
        lcd.setCursor(17, 1); lcd.write(6); 
        lcd.setCursor(18, 1); lcd.write(7); 

        // Row 2: Lower Body (Connector extension)
        lcd.setCursor(17, 2); lcd.write(6); 
        lcd.setCursor(18, 2); lcd.write(7); 

        // Row 3: Mouth
        lcd.setCursor(17, 3); lcd.write(happy ? 2 : 4);
        lcd.setCursor(18, 3); lcd.write(happy ? 3 : 5);
    }
    
    void drawPageData() const {
        char buf[LCD_COLS + 1];
        lcd.setCursor(0, 0); snprintf(buf, sizeof(buf), "Soil:%3.0f%%  H2O:%3.0f%%", currentData.soilMoisture, currentData.waterLevel); lcd.print(buf);
        lcd.setCursor(0, 1); snprintf(buf, sizeof(buf), "Sun :%3.0f%%  pH :%3.1f", currentData.lightLevel, currentData.pH); lcd.print(buf);
        lcd.setCursor(0, 2); snprintf(buf, sizeof(buf), "Temp:%2.0fC   Hum:%3.0f%%", currentData.temperature, currentData.airHumidity); lcd.print(buf);
        lcd.setCursor(0, 3); snprintf(buf, sizeof(buf), "CO2 :%4d ppm         ", currentData.co2); lcd.print(buf);
    }
    
    void drawPageRecords() const {
        lcd.setCursor(0, 0); lcd.print("--- 24H RECORDS --- ");
        if (historyCount == 0) {
            lcd.setCursor(0, 1); lcd.print(" Collecting data... ");
            lcd.setCursor(0, 2); lcd.print(" Wait 10 minutes... ");
            return;
        }
        PlantData first = getHistoryItem(0);
        float minT = first.temperature, maxT = first.temperature;
        float minH = first.airHumidity, maxH = first.airHumidity;
        float minS = first.soilMoisture, maxS = first.soilMoisture;
        
        for (uint8_t i = 1; i < historyCount; i++) {
            PlantData r = getHistoryItem(i);
            if (r.temperature < minT) minT = r.temperature; if (r.temperature > maxT) maxT = r.temperature;
            if (r.airHumidity < minH) minH = r.airHumidity; if (r.airHumidity > maxH) maxH = r.airHumidity;
            if (r.soilMoisture < minS) minS = r.soilMoisture; if (r.soilMoisture > maxS) maxS = r.soilMoisture;
        }
        char buf[LCD_COLS + 1];
        lcd.setCursor(0, 1); snprintf(buf, sizeof(buf), "Temp:%2.0fC - %2.0fC    ", minT, maxT); lcd.print(buf);
        lcd.setCursor(0, 2); snprintf(buf, sizeof(buf), "Hum :%2.0f%% - %2.0f%%    ", minH, maxH); lcd.print(buf);
        lcd.setCursor(0, 3); snprintf(buf, sizeof(buf), "Soil:%2.0f%% - %2.0f%%    ", minS, maxS); lcd.print(buf);
    }
    
    void drawPageGraph() const {
        lcd.setCursor(0, 0); lcd.print("100%   Soil Trend   ");
        lcd.setCursor(0, 3); lcd.print("0%   (Last 2.5 Hrs) ");
        for (uint8_t i = 0; i < LCD_COLS; i++) {
            uint8_t col = LCD_COLS - 1 - i;
            if (i < historyCount) {
                PlantData r = getHistoryItem(i);
                drawGraphColumn(col, r.soilMoisture);
            } else {
                lcd.setCursor(col, GRAPH_UPPER_ROW); lcd.print(" ");
                lcd.setCursor(col, GRAPH_LOWER_ROW); lcd.print(" ");
            }
        }
    }
    
    void drawPagePlant(bool isEditing) const {
        PlantProfile profile;
        memcpy_P(&profile, &PLANT_DB[getSelectedPlantIndex()], sizeof(PlantProfile));
        
        lcd.setCursor(0, 0); lcd.print("--- SELECT PLANT ---");
        
        // Clear line 1 first to avoid flickering when toggling arrows
        LCDHelper::clearLine(1); 
        lcd.setCursor(0, 1);
        
        if (isEditing) {
            lcd.print("> "); lcd.print(profile.name); lcd.print(" <");
        } else {
            lcd.print("  "); lcd.print(profile.name); lcd.print("    ");
        }
        
        char buf[LCD_COLS + 1];
        lcd.setCursor(0, 2);
        snprintf(buf, sizeof(buf), "W:%2.0f-%2.0f%% T:%2.0f-%2.0fC", profile.minSoil, profile.maxSoil, profile.minTemp, profile.maxTemp);
        lcd.print(buf);
        lcd.setCursor(0, 3);
        lcd.print(isEditing ? "[CLICK TO SAVE]     " : "[CLICK TO EDIT]     ");
    }
    
private:
    void drawGraphColumn(uint8_t col, float soilPercent) const {
        int totalHeight = map(constrain((int)soilPercent, 0, 100), 0, 100, 0, GRAPH_HEIGHT - 1);
        uint8_t upper = 0, lower = 0;
        if (totalHeight >= PIXELS_PER_ROW) {
            lower = 7; 
            upper = min(totalHeight - PIXELS_PER_ROW, 7);
        } else {
            lower = totalHeight; 
            upper = 0;
        }
        lcd.setCursor(col, GRAPH_UPPER_ROW); if (upper > 0) lcd.write(upper); else lcd.print(" ");
        lcd.setCursor(col, GRAPH_LOWER_ROW); if (lower > 0) lcd.write(lower); else lcd.print("_");
    }
    
    static uint8_t selectedPlantIndex;
public:
    static uint8_t getSelectedPlantIndex() { return selectedPlantIndex; }
    static void selectNextPlant() { selectedPlantIndex = (selectedPlantIndex + 1) % PLANT_DB_SIZE; }
    static void selectPreviousPlant() { selectedPlantIndex = (selectedPlantIndex == 0) ? PLANT_DB_SIZE - 1 : selectedPlantIndex - 1; }
};

uint8_t SmartPot::selectedPlantIndex = 0;

// =============================================================================
// GLOBAL STATE & MAIN LOOPS
// =============================================================================

SmartPot myPlant;
AssetManager assetManager;
ButtonHandler button(PIN_BTN);
Page currentPage = Page::EMOJI;
bool isEditingPlant = false;
unsigned long lastSensorUpdate = 0;
unsigned long lastHistoryLog = 0;

void setup() {
    Serial.begin(115200);
    Wire.begin();
    lcd.init(); lcd.backlight(); lcd.clear();
    
    // Show connecting message
    LCDHelper::printCentered(1, "Connecting WiFi...");
    connectWiFi();
    
    LCDHelper::printCentered(1, "Init Firebase...");
    initFirebase();
    
    lcd.clear();
    assetManager.loadAssets(AssetType::FACE);
    
    scd4x.begin(Wire, 0x62); 
    scd4x.stopPeriodicMeasurement(); delay(500); scd4x.startPeriodicMeasurement();
    
    pinMode(PIN_LIGHT, INPUT); pinMode(PIN_SOIL, INPUT);
    pinMode(PIN_WATER_SIGNAL, INPUT); pinMode(PIN_WATER_POWER, OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW);
    button.begin();
}

void loop() {
    static int lastEncoderPos = 0;
    encoder.tick();
    int currentEncoderPos = encoder.getPosition();
    
    if (button.wasPressed()) {
        if (currentPage == Page::PLANT) {
            isEditingPlant = !isEditingPlant;
            lcd.clear(); lastSensorUpdate = 0;
        }
    }
    
    if (currentEncoderPos != lastEncoderPos) {
        if (isEditingPlant) {
            if (currentEncoderPos > lastEncoderPos) SmartPot::selectNextPlant();
            else SmartPot::selectPreviousPlant();
        } else {
            int pageIndex = abs(currentEncoderPos) % (int)Page::COUNT;
            Page newPage = static_cast<Page>(pageIndex);
            
            if (newPage == Page::GRAPH && currentPage != Page::GRAPH) assetManager.loadAssets(AssetType::GRAPH);
            else if (newPage != Page::GRAPH && currentPage == Page::GRAPH) assetManager.loadAssets(AssetType::FACE);
            currentPage = newPage;
        }
        lcd.clear(); lastSensorUpdate = 0; lastEncoderPos = currentEncoderPos;
    }
    
    if (millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdate = millis();
        float light = SensorManager::getLightPercentage();
        float soil = SensorManager::getSoilMoisture();
        float water = SensorManager::getWaterLevel();
        uint16_t co2 = DEFAULT_CO2; float temp = DEFAULT_TEMP; float hum = DEFAULT_HUMIDITY;
        
        if (!SensorManager::readCO2Sensor(co2, temp, hum)) { co2=DEFAULT_CO2; temp=DEFAULT_TEMP; hum=DEFAULT_HUMIDITY; }
        myPlant.updateSensors(soil, water, light, DEFAULT_PH, temp, hum, co2);
        
        switch (currentPage) {
            case Page::EMOJI: myPlant.drawPageEmoji(); break;
            case Page::DATA: myPlant.drawPageData(); break;
            case Page::RECORDS: myPlant.drawPageRecords(); break;
            case Page::GRAPH: myPlant.drawPageGraph(); break;
            case Page::PLANT: myPlant.drawPagePlant(isEditingPlant); break;
            default: break;
        }
    }
    
    if (millis() - lastHistoryLog >= HISTORY_LOG_INTERVAL) {
        lastHistoryLog = millis();
        myPlant.logHistory(); 
        myPlant.printHistory();
        myPlant.sendToFirebase();  // Send to Firebase every 10 minutes
    }
}
