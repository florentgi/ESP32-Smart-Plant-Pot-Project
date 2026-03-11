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
constexpr uint8_t PIN_LIGHT = 34;
constexpr uint8_t PIN_SOIL = 35;
constexpr uint8_t PIN_WATER_SIGNAL = 32;
constexpr uint8_t PIN_WATER_POWER = 25;
constexpr uint8_t PIN_IN1 = 27;
constexpr uint8_t PIN_IN2 = 26;
constexpr uint8_t PIN_BTN = 14;

// LCD
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLS = 20;
constexpr uint8_t LCD_ROWS = 4;

// Timing
constexpr unsigned long SENSOR_UPDATE_INTERVAL = 2000;      
constexpr unsigned long HISTORY_LOG_INTERVAL = 60000; // 600000;       // 10 Mins (Cloud Log)
constexpr unsigned long PHOTO_INTERVAL = 86400000;           // 24 Hours (Camera Auto-Trigger)
constexpr unsigned long DEBOUNCE_DELAY = 50;                

// Settings
constexpr uint8_t MAX_HISTORY = 144;  
constexpr int SOIL_DRY_RAW = 2933;
constexpr int SOIL_WET_RAW = 2019;
constexpr int TANK_EMPTY_RAW = 0;
constexpr int TANK_FULL_RAW = 1421;
constexpr int ADC_MAX = 4095;

// Defaults
constexpr uint16_t DEFAULT_CO2 = 400; constexpr float DEFAULT_TEMP = 22.0f; 
constexpr float DEFAULT_HUMIDITY = 50.0f; constexpr float DEFAULT_PH = 7.0f;

// =============================================================================
// ENUMS & STRUCTURES
// =============================================================================
enum class PlantMood : uint8_t { HAPPY, THIRSTY, OVERWATERED, EMPTY_TANK, SUNBURNT, COLD, TOO_DARK, HOT };
enum class Page : uint8_t { HOME = 0, DATA = 1, RECORDS = 2, GRAPH = 3, PLANT = 4, COUNT = 5 };
enum class AssetType : uint8_t { BATTERY, GRAPH };

struct PlantProfile {
    const char* name;
    float minSoil; float maxSoil;
    float minTemp; float maxTemp;
    float minLight; float maxLight;
    const char* advice; 
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
    {"Monstera",     30.0, 80.0,  18.0, 29.0,  30.0, 90.0,  "Dry top 5cm"},
    {"Snake-Plant",  10.0, 50.0,  15.0, 29.0,  10.0, 60.0,  "Dry completely"},
    {"Spider-Plant", 40.0, 80.0,  15.0, 27.0,  25.0, 75.0,  "Keep moist"},
    {"Peace-Lily",   50.0, 90.0,  18.0, 27.0,  15.0, 65.0,  "Never dry out"},
    {"Pothos",       25.0, 75.0,  15.0, 29.0,  20.0, 80.0,  "Dry top 3cm"}
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
// HELPERS
// =============================================================================
void connectWiFi() {
    Serial.print("\n[WiFi] Connecting to: "); Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); Serial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\n[WiFi] FAILED! Check credentials in secrets.h");
    }
}

void initFirebase() {
    Serial.println("\n[Firebase] Initializing...");
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DATABASE_URL;
    auth.user.email = FIREBASE_USER_EMAIL;
    auth.user.password = FIREBASE_USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback; 
    fbdo.setBSSLBufferSize(4096, 1024);
    fbdo.setResponseSize(4096);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    firebaseReady = true;
    Serial.println("[Firebase] Setup Complete.");
}

// =============================================================================
// ASSET MANAGER (GRAPHICS)
// =============================================================================
const byte BAR_CHARS[8][8] PROGMEM = { {0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,31}, {0,0,0,0,0,0,31,31}, {0,0,0,0,0,31,31,31}, {0,0,0,0,31,31,31,31}, {0,0,0,31,31,31,31,31}, {0,0,31,31,31,31,31,31}, {31,31,31,31,31,31,31,31} };

// WIDE BATTERY ASSETS
const byte BAT_TL_E[8] PROGMEM = {B00011, B00011, B11111, B10000, B10000, B10000, B10000, B10000};
const byte BAT_TR_E[8] PROGMEM = {B11000, B11000, B11111, B00001, B00001, B00001, B00001, B00001};
const byte BAT_TL_F[8] PROGMEM = {B00011, B00011, B11111, B11111, B11111, B11111, B11111, B11111};
const byte BAT_TR_F[8] PROGMEM = {B11000, B11000, B11111, B11111, B11111, B11111, B11111, B11111};
const byte BAT_ML_E[8] PROGMEM = {B10000, B10000, B10000, B10000, B10000, B10000, B10000, B10000};
const byte BAT_MR_E[8] PROGMEM = {B00001, B00001, B00001, B00001, B00001, B00001, B00001, B00001};
const byte BAT_BL_F[8] PROGMEM = {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111}; 
const byte BAT_BR_F[8] PROGMEM = {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111};

class AssetManager {
private: AssetType currentAsset;
public:
    AssetManager() : currentAsset(AssetType::BATTERY) {}
    void loadAssets(AssetType type) {
        if (currentAsset == type) return; 
        currentAsset = type;
        if (type == AssetType::BATTERY) loadBatteryAssets(); else loadGraphAssets();
    }
private:
    void loadBatteryAssets() {
        byte temp[8];
        memcpy_P(temp, BAT_TL_E, 8); lcd.createChar(0, temp);
        memcpy_P(temp, BAT_TR_E, 8); lcd.createChar(1, temp);
        memcpy_P(temp, BAT_TL_F, 8); lcd.createChar(2, temp);
        memcpy_P(temp, BAT_TR_F, 8); lcd.createChar(3, temp);
        memcpy_P(temp, BAT_ML_E, 8); lcd.createChar(4, temp);
        memcpy_P(temp, BAT_MR_E, 8); lcd.createChar(5, temp);
        memcpy_P(temp, BAT_BL_F, 8); lcd.createChar(6, temp);
        memcpy_P(temp, BAT_BR_F, 8); lcd.createChar(7, temp);
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
    }
    
    PlantData getHistoryItem(uint8_t ago) const {
        if (ago >= historyCount) return PlantData();
        int targetIndex = historyIndex - 1 - ago; if (targetIndex < 0) targetIndex += MAX_HISTORY;
        return history[targetIndex];
    }
    
    void printHistory() const {
        Serial.printf(">> Local Records: %d | Last Soil: %.0f%%\n", historyCount, currentData.soilMoisture);
    }

    // --- FIREBASE UPLOAD ---
    bool sendToFirebase() {
        if (!firebaseReady || WiFi.status() != WL_CONNECTED) {
            Serial.println("[Error] Cannot upload: No WiFi or Firebase."); return false;
        }

        PlantProfile profile; memcpy_P(&profile, &PLANT_DB[selectedPlantIndex], sizeof(PlantProfile));
        String plantName = String(profile.name);
        
        FirebaseJson json;
        json.set("timestamp/.sv", "timestamp");
        json.set("plant_name", plantName);
        json.set("plant_id", getSelectedPlantIndex());
        json.set("status", getMoodText());
        
        json.set("soil", currentData.soilMoisture);
        json.set("sun", currentData.lightLevel);
        json.set("temp", currentData.temperature);
        json.set("co2", currentData.co2);
        json.set("h2o", currentData.waterLevel);
        json.set("ph", currentData.pH);
        json.set("hum", currentData.airHumidity);
        
        String path = "/history/" + plantName;
        Serial.print(">> Uploading to: "); Serial.println(path);
        
        if (Firebase.pushJSON(fbdo, path.c_str(), json)) {
            Serial.println(">> SUCCESS! Data saved.");
            return true;
        } else {
            Serial.print(">> ERROR: "); Serial.println(fbdo.errorReason());
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

    int getHealthLevel() const {
        if (currentMood == PlantMood::HAPPY) return 3; // Full Health
        if (currentMood == PlantMood::THIRSTY || currentMood == PlantMood::COLD || currentMood == PlantMood::HOT || currentMood == PlantMood::TOO_DARK) return 2; // Warning
        return 1; // Critical
    }

    // --- HOME SCREEN (BATTERY + TITLE) ---
    void drawPageHome() const {
        PlantProfile profile; memcpy_P(&profile, &PLANT_DB[selectedPlantIndex], sizeof(PlantProfile));
        int health = getHealthLevel();
        char buf[17]; 
        
        lcd.setCursor(0, 0); lcd.print("The Smart Gardener  "); 

        snprintf(buf, 17, "%-16s", profile.name); lcd.setCursor(0, 1); lcd.print(buf);
        snprintf(buf, 17, "%-16s", getMoodText()); lcd.setCursor(0, 2); lcd.print(buf);
        snprintf(buf, 17, "Soil: %.0f%%       ", currentData.soilMoisture); lcd.setCursor(0, 3); lcd.print(buf);

        lcd.setCursor(18, 1); if(health >= 3) { lcd.write(2); lcd.write(3); } else { lcd.write(0); lcd.write(1); }
        lcd.setCursor(18, 2); if(health >= 2) { lcd.write(255); lcd.write(255); } else { lcd.write(4); lcd.write(5); }
        lcd.setCursor(18, 3); lcd.write(6); lcd.write(7); 
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
        char buf[21]; lcd.setCursor(0,2); 
        snprintf(buf,21,"S:%.0f-%.0f%% T:%.0f-%.0fC", p.minSoil, p.maxSoil, p.minTemp, p.maxTemp); 
        lcd.print(buf);
        lcd.setCursor(0,3); if(isEditing) lcd.print(p.advice); else lcd.print("[CLICK TO EDIT]     ");
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
// MAIN LOOPS
// =============================================================================
SmartPot myPlant;
AssetManager assetManager;
ButtonHandler button(PIN_BTN);
Page currentPage = Page::HOME;
bool isEditingPlant = false;
unsigned long lastSensorUpdate = 0;
unsigned long lastHistoryLog = 0;
unsigned long lastPhotoTime = 0; 

void setup() {
    Serial.begin(115200);
    Wire.begin();
    lcd.init(); lcd.backlight(); lcd.clear();
    
    // Debug Secrets
    Serial.println("\n--- DEBUG CONFIG ---");
    Serial.print("API KEY Len: "); Serial.println(String(FIREBASE_API_KEY).length());
    Serial.print("DB URL: "); Serial.println(FIREBASE_DATABASE_URL);

    assetManager.loadAssets(AssetType::BATTERY);
    scd4x.begin(Wire, 0x62); scd4x.stopPeriodicMeasurement(); delay(500); scd4x.startPeriodicMeasurement();
    pinMode(PIN_LIGHT, INPUT); pinMode(PIN_SOIL, INPUT); pinMode(PIN_WATER_SIGNAL, INPUT); pinMode(PIN_WATER_POWER, OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW);
    
    lastPhotoTime = millis(); // Initialize camera timer

    button.begin();
    
    LCDHelper::printCentered(1, "Connecting WiFi...");
    connectWiFi();
    LCDHelper::printCentered(1, "Init Firebase...");
    initFirebase();
    lcd.clear();
}

void loop() {
    static int lastEncoderPos = 0;
    encoder.tick(); int currentEncoderPos = encoder.getPosition();
    
    // --- UPDATED BUTTON LOGIC (WITH CLOUD TRIGGER) ---
    if (button.wasPressed()) {
        if (currentPage == Page::PLANT) { 
            isEditingPlant = !isEditingPlant; 
            lcd.clear(); 
            lastSensorUpdate = 0; 
        } 
        // Force Photo when on Home Screen
        else if (currentPage == Page::HOME) {
            Serial.println(">> CLOUD COMMAND: Triggering Camera...");
            
            // Visual Feedback
            lcd.setCursor(0,0); lcd.print(">> SAY CHEESE! <<   ");
            
            // --- NEW: Trigger Camera via Firebase ---
            if (Firebase.setBool(fbdo, "/camera/trigger", true)) {
                Serial.println(">> Trigger sent to cloud!");
            } else {
                Serial.println(">> Failed to send trigger. Error: " + fbdo.errorReason());
            }
            
            delay(2000); // Wait a bit for feedback to be read
            lcd.clear(); lastSensorUpdate = 0; // Refresh screen
        }
    }
    
    // --- ENCODER LOGIC ---
    if (currentEncoderPos != lastEncoderPos) {
        if (isEditingPlant) {
            if (currentEncoderPos > lastEncoderPos) SmartPot::selectNextPlant(); else SmartPot::selectPreviousPlant();
        } else {
            int p = abs(currentEncoderPos) % (int)Page::COUNT;
            Page newPage = static_cast<Page>(p);
            if(newPage == Page::GRAPH && currentPage != Page::GRAPH) assetManager.loadAssets(AssetType::GRAPH);
            else if(newPage != Page::GRAPH && currentPage == Page::GRAPH) assetManager.loadAssets(AssetType::BATTERY);
            currentPage = newPage;
        }
        lcd.clear(); lastSensorUpdate = 0; lastEncoderPos = currentEncoderPos;
    }
    
    // --- SENSOR UPDATES ---
    if (millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdate = millis();
        float l = SensorManager::getLightPercentage(); float s = SensorManager::getSoilMoisture(); float w = SensorManager::getWaterLevel();
        uint16_t c; float t, h;
        if (!SensorManager::readCO2Sensor(c, t, h)) { c=DEFAULT_CO2; t=DEFAULT_TEMP; h=DEFAULT_HUMIDITY; }
        myPlant.updateSensors(s, w, l, DEFAULT_PH, t, h, c);
        
        switch (currentPage) {
            case Page::HOME: myPlant.drawPageHome(); break;
            case Page::DATA: myPlant.drawPageData(); break;
            case Page::RECORDS: myPlant.drawPageRecords(); break;
            case Page::GRAPH: myPlant.drawPageGraph(); break;
            case Page::PLANT: myPlant.drawPagePlant(isEditingPlant); break;
        }
    }
    
    // --- FIREBASE LOGGING ---
    if (millis() - lastHistoryLog >= HISTORY_LOG_INTERVAL) {
        lastHistoryLog = millis();
        myPlant.logHistory(); 
        myPlant.printHistory();
        myPlant.sendToFirebase();
    }

    // --- AUTO CAMERA CLOUD TRIGGER (24 HOURS) ---
    if (millis() - lastPhotoTime >= PHOTO_INTERVAL) {
        lastPhotoTime = millis();
        Serial.println(">> AUTO CLOUD COMMAND: Daily Photo...");
        
        // --- NEW: Send auto-trigger to Firebase ---
        Firebase.setBool(fbdo, "/camera/trigger", true);
    }
}
