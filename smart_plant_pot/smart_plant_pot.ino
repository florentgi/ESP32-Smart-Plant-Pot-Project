#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionI2cScd4x.h>
#include <RotaryEncoder.h>
#include <WiFi.h>
#include <FirebaseESP32.h>

// Helper to print token errors
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// INCLUDE SECRETS
#include "../secrets.h" 

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================
constexpr uint8_t PIN_LIGHT = 34;
constexpr uint8_t PIN_SOIL = 35;
constexpr uint8_t PIN_WATER_SIGNAL = 32;
constexpr uint8_t PIN_WATER_POWER = 25;
constexpr uint8_t PIN_IN1 = 27;
constexpr uint8_t PIN_IN2 = 26;
constexpr uint8_t PIN_BTN = 14;

constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLS = 20;
constexpr uint8_t LCD_ROWS = 4;

constexpr unsigned long SENSOR_UPDATE_INTERVAL = 2000;      
constexpr unsigned long HISTORY_LOG_INTERVAL = 10000; //600000     // 10 Mins
constexpr unsigned long DEBOUNCE_DELAY = 50;                

constexpr uint8_t MAX_HISTORY = 144;  

constexpr int SOIL_DRY_RAW = 2933;
constexpr int SOIL_WET_RAW = 2019;
constexpr int TANK_EMPTY_RAW = 0;
constexpr int TANK_FULL_RAW = 1421;
constexpr int ADC_MAX = 4095;

constexpr float MIN_VALID_TEMP = -10.0f;
constexpr float MAX_VALID_TEMP = 60.0f;
constexpr float MIN_VALID_HUMIDITY = 0.0f;
constexpr float MAX_VALID_HUMIDITY = 100.0f;
constexpr uint16_t MIN_VALID_CO2 = 300;
constexpr uint16_t MAX_VALID_CO2 = 5000;

constexpr uint16_t DEFAULT_CO2 = 400;
constexpr float DEFAULT_TEMP = 22.0f;
constexpr float DEFAULT_HUMIDITY = 50.0f;
constexpr float DEFAULT_PH = 7.0f;

constexpr uint8_t GRAPH_HEIGHT = 16;  
constexpr uint8_t GRAPH_UPPER_ROW = 1;
constexpr uint8_t GRAPH_LOWER_ROW = 2;
constexpr uint8_t PIXELS_PER_ROW = 8;

// =============================================================================
// ENUMS & STRUCTURES
// =============================================================================
enum class PlantMood : uint8_t { HAPPY, THIRSTY, OVERWATERED, EMPTY_TANK, SUNBURNT, COLD, TOO_DARK, HOT };
enum class Page : uint8_t { EMOJI = 0, DATA = 1, RECORDS = 2, GRAPH = 3, PLANT = 4, COUNT = 5 };
enum class AssetType : uint8_t { FACE, GRAPH };

struct PlantProfile {
    const char* name;
    float minSoil; float maxSoil; float minTemp; float maxTemp; float minLight; float maxLight;
};

struct PlantData {
    float soilMoisture; float waterLevel; float lightLevel; float pH;
    float temperature; float airHumidity; uint16_t co2;
    PlantData() : soilMoisture(0), waterLevel(0), lightLevel(0), pH(DEFAULT_PH), temperature(DEFAULT_TEMP), airHumidity(DEFAULT_HUMIDITY), co2(DEFAULT_CO2) {}
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
// GLOBAL OBJECTS
// =============================================================================
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
SensirionI2cScd4x scd4x;
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;

// =============================================================================
// WIFI & FIREBASE HELPERS
// =============================================================================
void connectWiFi() {
    Serial.print("\n[WiFi] Connecting to: ");
    Serial.println(WIFI_SSID);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
        Serial.print("[WiFi] Signal: "); Serial.println(WiFi.RSSI());
    } else {
        Serial.println("\n[WiFi] FAILED! Check credentials in secrets.h");
    }
}

void initFirebase() {
    Serial.println("\n[Firebase] Initializing...");

    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DATABASE_URL;

    // Email/Password Auth ---
    auth.user.email = FIREBASE_USER_EMAIL;
    auth.user.password = FIREBASE_USER_PASSWORD;

    // Assign the callback to print debug info
    config.token_status_callback = tokenStatusCallback; 
    
    // Critical fix for ESP32 memory stability
    fbdo.setBSSLBufferSize(4096, 1024);
    fbdo.setResponseSize(4096);

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    firebaseReady = true;
    Serial.println("[Firebase] Setup Complete. Connecting...");
}

// =============================================================================
// LCD ASSETS & SENSORS
// =============================================================================
const byte BAR_CHARS[8][8] PROGMEM = { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,31}, {0,0,0,0,0,0,31,31}, {0,0,0,0,0,31,31,31}, {0,0,0,0,31,31,31,31}, {0,0,0,31,31,31,31,31}, {0,0,31,31,31,31,31,31}, {31,31,31,31,31,31,31,31} };
const byte FACE_EYE_OPEN[8] PROGMEM    = {B00111, B01111, B11111, B11111, B11111, B11111, B01111, B00111};
const byte FACE_EYE_CLOSED[8] PROGMEM  = {B00000, B00000, B00000, B00000, B11111, B11111, B00000, B00000};
const byte FACE_MOUTH_HAPPY_L[8] PROGMEM = {B00000, B00000, B10000, B11000, B11100, B01110, B00111, B00011};
const byte FACE_MOUTH_HAPPY_R[8] PROGMEM = {B00000, B00000, B00001, B00011, B00111, B01110, B11100, B11000};
const byte FACE_MOUTH_SAD_L[8] PROGMEM   = {B00011, B00111, B01110, B11100, B11000, B10000, B00000, B00000};
const byte FACE_MOUTH_SAD_R[8] PROGMEM   = {B11000, B11100, B01110, B00111, B00011, B00001, B00000, B00000};
const byte FACE_BODY_L[8] PROGMEM        = {B11000, B11000, B11000, B11000, B11000, B11000, B11000, B11000};
const byte FACE_BODY_R[8] PROGMEM        = {B00011, B00011, B00011, B00011, B00011, B00011, B00011, B00011};

class AssetManager {
private: AssetType currentAsset;
public:
    AssetManager() : currentAsset(AssetType::FACE) {}
    void loadAssets(AssetType type) {
        if (currentAsset == type) return; 
        currentAsset = type;
        if (type == AssetType::FACE) loadFaceAssets(); else loadGraphAssets();
    }
private:
    void loadFaceAssets() {
        byte temp[8];
        memcpy_P(temp, FACE_EYE_OPEN, 8); lcd.createChar(0, temp);
        memcpy_P(temp, FACE_EYE_CLOSED, 8); lcd.createChar(1, temp);
        memcpy_P(temp, FACE_MOUTH_HAPPY_L, 8); lcd.createChar(2, temp);
        memcpy_P(temp, FACE_MOUTH_HAPPY_R, 8); lcd.createChar(3, temp);
        memcpy_P(temp, FACE_MOUTH_SAD_L, 8); lcd.createChar(4, temp);
        memcpy_P(temp, FACE_MOUTH_SAD_R, 8); lcd.createChar(5, temp);
        memcpy_P(temp, FACE_BODY_L, 8); lcd.createChar(6, temp);
        memcpy_P(temp, FACE_BODY_R, 8); lcd.createChar(7, temp);
    }
    void loadGraphAssets() {
        byte temp[8];
        for (uint8_t i = 0; i < 8; i++) { memcpy_P(temp, BAR_CHARS[i], 8); lcd.createChar(i, temp); }
    }
};

class ButtonHandler {
private: uint8_t pin; bool lastState; unsigned long lastDebounceTime; bool debouncedState;
public:
    ButtonHandler(uint8_t buttonPin) : pin(buttonPin), lastState(HIGH), lastDebounceTime(0), debouncedState(HIGH) {}
    void begin() { pinMode(pin, INPUT_PULLUP); debouncedState = digitalRead(pin); lastState = debouncedState; }
    bool wasPressed() {
        bool currentState = digitalRead(pin); bool pressed = false;
        if (currentState != lastState) lastDebounceTime = millis();
        if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
            if (currentState != debouncedState) { debouncedState = currentState; if (debouncedState == LOW) pressed = true; }
        }
        lastState = currentState; return pressed;
    }
};

class SensorManager {
public:
    static float getLightPercentage() { return constrain((analogRead(PIN_LIGHT) / (float)ADC_MAX) * 100.0f, 0.0f, 100.0f); }
    static float getSoilMoisture() { return constrain(map(analogRead(PIN_SOIL), SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100); }
    static float getWaterLevel() {
        digitalWrite(PIN_WATER_POWER, HIGH); delay(10);
        int raw = analogRead(PIN_WATER_SIGNAL); digitalWrite(PIN_WATER_POWER, LOW);
        return constrain(map(raw, TANK_EMPTY_RAW, TANK_FULL_RAW, 0, 100), 0, 100);
    }
    static bool readCO2Sensor(uint16_t &co2, float &temp, float &humidity) {
        bool isReady = false;
        if (scd4x.getDataReadyStatus(isReady) != 0 || !isReady) return false;
        if (scd4x.readMeasurement(co2, temp, humidity) != 0) return false;
        return true;
    }
};

class LCDHelper {
public:
    static void printCentered(uint8_t row, const char* text) {
        uint8_t len = strlen(text); uint8_t pos = (len < LCD_COLS) ? (LCD_COLS - len) / 2 : 0;
        lcd.setCursor(0, row); for (uint8_t i = 0; i < LCD_COLS; i++) lcd.print(' ');
        lcd.setCursor(pos, row); lcd.print(text);
    }
    static void clearLine(uint8_t row) { lcd.setCursor(0, row); for (uint8_t i = 0; i < LCD_COLS; i++) lcd.print(' '); }
};

// =============================================================================
// SMART POT CLASS (Logic)
// =============================================================================
class SmartPot {
private:
    PlantData currentData; PlantMood currentMood;
    PlantData history[MAX_HISTORY]; uint8_t historyIndex; uint8_t historyCount;
    static uint8_t selectedPlantIndex;
public:
    SmartPot() : currentMood(PlantMood::HAPPY), historyIndex(0), historyCount(0) {}
    static uint8_t getSelectedPlantIndex() { return selectedPlantIndex; }
    static void selectNextPlant() { selectedPlantIndex = (selectedPlantIndex + 1) % PLANT_DB_SIZE; }
    static void selectPreviousPlant() { selectedPlantIndex = (selectedPlantIndex == 0) ? PLANT_DB_SIZE - 1 : selectedPlantIndex - 1; }

    void logHistory() {
        history[historyIndex] = currentData;
        historyIndex = (historyIndex + 1) % MAX_HISTORY;
        if (historyCount < MAX_HISTORY) historyCount++;
        Serial.printf(">> LOG SAVED. Total Records: %d\n", historyCount);
    }
    
    PlantData getHistoryItem(uint8_t ago) const {
        if (ago >= historyCount) return PlantData();
        int targetIndex = historyIndex - 1 - ago; if (targetIndex < 0) targetIndex += MAX_HISTORY;
        return history[targetIndex];
    }
    
    void printHistory() const {
        Serial.println("\n====== 24H DATA HISTORY ======");
        for (uint8_t i = 0; i < historyCount; i++) {
            PlantData r = getHistoryItem(i);
            Serial.printf("-%3d min | Soil: %.0f%%\n", i * 10, r.soilMoisture);
        }
        Serial.println("==============================\n");
    }

    // --- FIREBASE DEBUGGING UPLOAD FUNCTION ---
    bool sendToFirebase() {
        Serial.println("\n--- UPLOADING TO FIREBASE ---");

        // 1. Safety Checks
        if (!firebaseReady) {
            Serial.println("[Error] Firebase not initialized.");
            return false;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Error] WiFi disconnected.");
            return false;
        }
        
        // 2. Get Plant Identity
        // We retrieve the name from the database based on which plant you selected on the LCD.
        PlantProfile profile;
        memcpy_P(&profile, &PLANT_DB[getSelectedPlantIndex()], sizeof(PlantProfile));
        String plantName = String(profile.name);
        int plantID = getSelectedPlantIndex();
        
        // 3. Prepare the JSON Packet
        FirebaseJson json;
        
        // --- IDENTITY & TIME ---
        json.set("timestamp/.sv", "timestamp"); // Server-side Timestamp
        json.set("plant_name", plantName);      // e.g., "Fern"
        json.set("plant_id", plantID);          // e.g., 2
        
        // --- STATUS ---
        json.set("status", getMoodText());      // e.g., "Happy", "Thirsty!"
        
        // --- SENSORS (The 7 Data Points) ---
        json.set("soil", currentData.soilMoisture);
        json.set("sun", currentData.lightLevel);
        json.set("temp", currentData.temperature);
        json.set("co2", currentData.co2);
        json.set("h2o", currentData.waterLevel);
        json.set("ph", currentData.pH);
        json.set("hum", currentData.airHumidity);
        
        // 4. Define the Path
        // We group data by Plant Name. This creates a neat folder for each plant.
        String path = "/history/" + plantName;
        
        Serial.print(">> Target Path: "); Serial.println(path);
        
        // 5. Send Data
        // pushJSON automatically creates a unique ID (like -Nky...) for this specific entry
        if (Firebase.pushJSON(fbdo, path.c_str(), json)) {
            Serial.println(">> SUCCESS! Data saved to cloud.");
            Serial.print(">> Database Path: "); Serial.println(fbdo.dataPath());
            return true;
        } else {
            Serial.print(">> ERROR: ");
            Serial.println(fbdo.errorReason());
            return false;
        }
    }
    
    // --- Logic & Display ---
    void updateSensors(float m, float w, float l, float p, float t, float h, uint16_t c) {
        currentData.soilMoisture = m; currentData.waterLevel = w; currentData.lightLevel = l;
        currentData.pH = p; currentData.temperature = t; currentData.airHumidity = h; currentData.co2 = c;
        evaluateMood();
    }
    void evaluateMood() {
        PlantProfile p; memcpy_P(&p, &PLANT_DB[selectedPlantIndex], sizeof(PlantProfile));
        if (currentData.waterLevel < 10.0f) currentMood = PlantMood::EMPTY_TANK;
        else if (currentData.soilMoisture < p.minSoil) currentMood = PlantMood::THIRSTY;
        else if (currentData.soilMoisture > p.maxSoil) currentMood = PlantMood::OVERWATERED;
        else if (currentData.temperature < p.minTemp) currentMood = PlantMood::COLD;
        else if (currentData.temperature > p.maxTemp) currentMood = PlantMood::HOT;
        else if (currentData.lightLevel > p.maxLight) currentMood = PlantMood::SUNBURNT;
        else if (currentData.lightLevel < p.minLight) currentMood = PlantMood::TOO_DARK;
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

    void drawPageEmoji() const {
        PlantProfile profile; memcpy_P(&profile, &PLANT_DB[selectedPlantIndex], sizeof(PlantProfile));
        bool happy = isHappy();
        char buf[17]; snprintf(buf, 17, "%-16s", profile.name); 
        lcd.setCursor(0, 0); lcd.print(buf);
        lcd.setCursor(0, 1); lcd.print("                ");
        lcd.setCursor(0, 2); lcd.print("                ");
        snprintf(buf, 17, "%-16s", getMoodText());
        lcd.setCursor(0, 3); lcd.print(buf);
        lcd.setCursor(17, 0); lcd.write(happy ? 0 : 1); lcd.setCursor(18, 0); lcd.write(happy ? 0 : 1);
        lcd.setCursor(17, 1); lcd.write(6); lcd.setCursor(18, 1); lcd.write(7);
        lcd.setCursor(17, 2); lcd.write(6); lcd.setCursor(18, 2); lcd.write(7);
        lcd.setCursor(17, 3); lcd.write(happy ? 2 : 4); lcd.setCursor(18, 3); lcd.write(happy ? 3 : 5);
    }
    void drawPageData() const {
        char buf[21];
        lcd.setCursor(0,0); snprintf(buf, 21, "Soil:%3.0f%%  H2O:%3.0f%%", currentData.soilMoisture, currentData.waterLevel); lcd.print(buf);
        lcd.setCursor(0,1); snprintf(buf, 21, "Sun :%3.0f%%  pH :%3.1f", currentData.lightLevel, currentData.pH); lcd.print(buf);
        lcd.setCursor(0,2); snprintf(buf, 21, "Temp:%2.0fC   Hum:%3.0f%%", currentData.temperature, currentData.airHumidity); lcd.print(buf);
        lcd.setCursor(0,3); snprintf(buf, 21, "CO2 :%4d ppm         ", currentData.co2); lcd.print(buf);
    }
    void drawPageRecords() const {
        lcd.setCursor(0,0); lcd.print("--- 24H RECORDS --- ");
        if(historyCount==0){ lcd.setCursor(0,1); lcd.print(" Collecting data... "); return; }
        PlantData f = getHistoryItem(0);
        float minT=f.temperature,maxT=f.temperature, minS=f.soilMoisture,maxS=f.soilMoisture;
        for(uint8_t i=1; i<historyCount; i++){
            PlantData r=getHistoryItem(i);
            if(r.temperature<minT)minT=r.temperature; if(r.temperature>maxT)maxT=r.temperature;
            if(r.soilMoisture<minS)minS=r.soilMoisture; if(r.soilMoisture>maxS)maxS=r.soilMoisture;
        }
        char buf[21];
        lcd.setCursor(0,1); snprintf(buf,21,"Temp:%2.0fC - %2.0fC    ",minT,maxT); lcd.print(buf);
        lcd.setCursor(0,2); snprintf(buf,21,"Soil:%2.0f%% - %2.0f%%    ",minS,maxS); lcd.print(buf);
        lcd.setCursor(0,3); lcd.print("Samples: "); lcd.print(historyCount);
    }
    void drawPageGraph() const {
        lcd.setCursor(0,0); lcd.print("100%   Soil Trend   ");
        lcd.setCursor(0,3); lcd.print("0%   (Last 2.5 Hrs) ");
        for(uint8_t i=0; i<LCD_COLS; i++){
            uint8_t col = LCD_COLS-1-i;
            if(i<historyCount){ drawGraphColumn(col, getHistoryItem(i).soilMoisture); }
            else{ lcd.setCursor(col,1); lcd.print(" "); lcd.setCursor(col,2); lcd.print(" "); }
        }
    }
    void drawPagePlant(bool isEditing) const {
        PlantProfile p; memcpy_P(&p, &PLANT_DB[selectedPlantIndex], sizeof(PlantProfile));
        lcd.setCursor(0,0); lcd.print("--- SELECT PLANT ---");
        LCDHelper::clearLine(1); lcd.setCursor(0,1);
        if(isEditing){ lcd.print("> "); lcd.print(p.name); lcd.print(" <"); }
        else{ lcd.print("  "); lcd.print(p.name); lcd.print("    "); }
        char buf[21]; lcd.setCursor(0,2); snprintf(buf,21,"W:%2.0f-%2.0f T:%2.0f-%2.0f",p.minSoil,p.maxSoil,p.minTemp,p.maxTemp); lcd.print(buf);
        lcd.setCursor(0,3); lcd.print(isEditing ? "[CLICK TO SAVE]     " : "[CLICK TO EDIT]     ");
    }
private:
    void drawGraphColumn(uint8_t col, float val) const {
        int h = map(constrain((int)val,0,100),0,100,0,15);
        uint8_t u=0, l=0;
        if(h>=8){ l=7; u=min(h-8,7); } else { l=h; u=0; }
        lcd.setCursor(col,1); if(u>0)lcd.write(u); else lcd.print(" ");
        lcd.setCursor(col,2); if(l>0)lcd.write(l); else lcd.print("_");
    }
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
    
    // DEBUG: CHECK SECRETS
    Serial.println("\n--- DEBUG: Checking Config ---");
    Serial.print("API KEY Len: "); Serial.println(String(FIREBASE_API_KEY).length());
    Serial.print("DB URL: "); Serial.println(FIREBASE_DATABASE_URL);

    // Init Logic
    assetManager.loadAssets(AssetType::FACE);
    scd4x.begin(Wire, 0x62); scd4x.stopPeriodicMeasurement(); delay(500); scd4x.startPeriodicMeasurement();
    pinMode(PIN_LIGHT, INPUT); pinMode(PIN_SOIL, INPUT); pinMode(PIN_WATER_SIGNAL, INPUT); pinMode(PIN_WATER_POWER, OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW);
    button.begin();
    
    // Connect
    LCDHelper::printCentered(1, "Connecting WiFi...");
    connectWiFi();
    LCDHelper::printCentered(1, "Init Firebase...");
    initFirebase();
    lcd.clear();
}

void loop() {
    static int lastEncoderPos = 0;
    encoder.tick(); int currentEncoderPos = encoder.getPosition();
    
    if (button.wasPressed()) {
        if (currentPage == Page::PLANT) { isEditingPlant = !isEditingPlant; lcd.clear(); lastSensorUpdate = 0; }
    }
    
    if (currentEncoderPos != lastEncoderPos) {
        if (isEditingPlant) {
            if (currentEncoderPos > lastEncoderPos) SmartPot::selectNextPlant(); else SmartPot::selectPreviousPlant();
        } else {
            int p = abs(currentEncoderPos) % (int)Page::COUNT;
            Page newPage = static_cast<Page>(p);
            if(newPage == Page::GRAPH && currentPage != Page::GRAPH) assetManager.loadAssets(AssetType::GRAPH);
            else if(newPage != Page::GRAPH && currentPage == Page::GRAPH) assetManager.loadAssets(AssetType::FACE);
            currentPage = newPage;
        }
        lcd.clear(); lastSensorUpdate = 0; lastEncoderPos = currentEncoderPos;
    }
    
    if (millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdate = millis();
        float l = SensorManager::getLightPercentage(); float s = SensorManager::getSoilMoisture(); float w = SensorManager::getWaterLevel();
        uint16_t c; float t, h;
        if (!SensorManager::readCO2Sensor(c, t, h)) { c=DEFAULT_CO2; t=DEFAULT_TEMP; h=DEFAULT_HUMIDITY; }
        myPlant.updateSensors(s, w, l, DEFAULT_PH, t, h, c);
        
        switch (currentPage) {
            case Page::EMOJI: myPlant.drawPageEmoji(); break;
            case Page::DATA: myPlant.drawPageData(); break;
            case Page::RECORDS: myPlant.drawPageRecords(); break;
            case Page::GRAPH: myPlant.drawPageGraph(); break;
            case Page::PLANT: myPlant.drawPagePlant(isEditingPlant); break;
        }
    }
    
    if (millis() - lastHistoryLog >= HISTORY_LOG_INTERVAL) {
        lastHistoryLog = millis();
        myPlant.logHistory(); 
        myPlant.printHistory();
        myPlant.sendToFirebase(); // <--- This function now prints extensive debug info
    }
}
