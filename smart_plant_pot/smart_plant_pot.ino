#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionI2cScd4x.h>
#include <RotaryEncoder.h>

// --- HARDWARE SETTINGS ---
#define PIN_LIGHT 34        
#define PIN_SOIL  35        
// #define PIN_PH    33     // Disabled (Defective)
#define PIN_WATER_SIGNAL 32 
#define PIN_WATER_POWER  25 

// --- ROTARY ENCODER PINS ---
#define PIN_IN1 27 // CLK
#define PIN_IN2 26 // DT
#define PIN_BTN 14 // SW

// --- MEMORY SETTINGS ---
#define HISTORY_INTERVAL 5000 // 600000 // 10 Minutes in ms
#define MAX_HISTORY 144         // 24 hours (6 logs/hr * 24h)

// --- OBJECTS ---
LiquidCrystal_I2C lcd(0x27, 20, 4);
SensirionI2cScd4x scd4x;
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);

// --- CALIBRATION ---
const int DRY_VALUE      = 2933;  
const int WATER_VALUE    = 2019;  
const int TANK_EMPTY_RAW = 0;     
const int TANK_FULL_RAW  = 1421;  

// --- PLANT DATABASE ---
struct PlantProfile {
    String name;
    float minSoil; float maxSoil;
    float minTemp; float maxTemp;
    float minLight; float maxLight;
};

PlantProfile plantDB[5] = {
    // Name      SoilMin SoilMax TempMin TempMax LightMin LightMax
    { "Generic", 20.0,   85.0,   10.0,   35.0,   0.0,     90.0 }, 
    { "Cactus",  5.0,    40.0,   15.0,   40.0,   40.0,    100.0}, 
    { "Fern",    50.0,   90.0,   18.0,   28.0,   10.0,    60.0 }, 
    { "Basil",   30.0,   80.0,   20.0,   30.0,   50.0,    100.0}, 
    { "Orchid",  20.0,   60.0,   18.0,   32.0,   20.0,    50.0 }  
};

int selectedPlantIndex = 0; 
bool isEditingPlant = false; 

// --- STATES ---
enum PlantMood { HAPPY, THIRSTY, OVERWATERED, EMPTY_TANK, SUNBURNT, COLD, TOO_DARK, HOT };
// UPDATED: Added PAGE_RECORDS
enum Page { PAGE_EMOJI, PAGE_DATA, PAGE_PLANT, PAGE_RECORDS }; 

struct PlantData {
    float soilMoisture; float waterLevel; float lightLevel; float pH;             
    float temperature; float airHumidity; uint16_t co2;         
};

// --- SMART POT CLASS ---
class SmartPot {
private:
    PlantData data;
    PlantMood currentMood;

    PlantData history[MAX_HISTORY]; 
    int historyIndex = 0;           
    int historyCount = 0;           

public:
    SmartPot() : currentMood(HAPPY) {
        data = {0, 0, 0, 7.0, 20.0, 50.0, 400};
    }

    // --- HISTORY LOGIC ---
    void logHistory() {
        history[historyIndex] = data; 
        historyIndex++;
        if (historyIndex >= MAX_HISTORY) historyIndex = 0; 
        if (historyCount < MAX_HISTORY) historyCount++;
        
        Serial.print(">> LOG SAVED. Total Records: ");
        Serial.println(historyCount);
    }

    PlantData getHistoryItem(int ago) {
        int targetIndex = historyIndex - 1 - ago;
        if (targetIndex < 0) targetIndex += MAX_HISTORY; 
        return history[targetIndex];
    }

    void printHistory() {
        Serial.println("\n====== 24H DATA HISTORY ======");
        if (historyCount == 0) {
            Serial.println("No records yet.");
            return;
        }
        for (int i = 0; i < historyCount; i++) {
            PlantData record = getHistoryItem(i);
            Serial.printf("-%3d min | Soil: %.0f%% | Temp: %.1fC | Light: %.0f%%\n", 
                          i * 10, record.soilMoisture, record.temperature, record.lightLevel);
        }
        Serial.println("==============================\n");
    }

    void updateSensors(float moisture, float water, float light, float ph, float temp, float hum, uint16_t co2Val) {
        data.soilMoisture = moisture;
        data.waterLevel = water;
        data.lightLevel = light;
        data.pH = ph;
        data.temperature = temp;
        data.airHumidity = hum;
        data.co2 = co2Val;
        evaluateMood();
    }

    void evaluateMood() {
        PlantProfile p = plantDB[selectedPlantIndex];

        if (data.waterLevel < 10.0) currentMood = EMPTY_TANK; 
        else if (data.soilMoisture < p.minSoil) currentMood = THIRSTY;
        else if (data.soilMoisture > p.maxSoil) currentMood = OVERWATERED;
        else if (data.temperature < p.minTemp)  currentMood = COLD;
        else if (data.temperature > p.maxTemp)  currentMood = HOT;
        else if (data.lightLevel > p.maxLight)  currentMood = SUNBURNT;
        else if (data.lightLevel < p.minLight)  currentMood = TOO_DARK;
        else currentMood = HAPPY;
    }

    String getEmoji() {
        switch (currentMood) {
            case HAPPY:        return "( ^_^) ";
            case THIRSTY:      return "( >_<) ";
            case OVERWATERED:  return "( ~_~) ";
            case EMPTY_TANK:   return "( O_O) ";
            case SUNBURNT:     return "( x_x) ";
            case HOT:          return "( @.@) ";
            case COLD:         return "(*_* ) ";
            case TOO_DARK:     return "( -_-) ";
            default:           return "( o_o) ";
        }
    }

    String getMoodText() {
        switch (currentMood) {
            case HAPPY:        return "Happy";
            case THIRSTY:      return "Thirsty!";
            case OVERWATERED:  return "Too Wet!";
            case EMPTY_TANK:   return "Refill!";
            case SUNBURNT:     return "Sunburn!";
            case HOT:          return "Too Hot!";
            case COLD:         return "Too Cold!";
            case TOO_DARK:     return "Need Sun";
            default:           return "Unknown";
        }
    }

    // --- PAGE 1: EMOJI ---
    void drawPageEmoji() {
        lcd.setCursor(0, 0); lcd.print(" The Smart Gardener "); 
        lcd.setCursor(5, 1); lcd.print(getEmoji()); 
        lcd.setCursor(5, 2); lcd.print(getMoodText()); 
        
        String name = plantDB[selectedPlantIndex].name;
        int centerPos = (20 - name.length()) / 2;
        lcd.setCursor(0, 3); lcd.print("                    "); 
        lcd.setCursor(centerPos, 3); lcd.print(name);
    }

    // --- PAGE 2: DASHBOARD ---
    void drawPageData() {
        char buffer[21]; 
        lcd.setCursor(0, 0);
        snprintf(buffer, 21, "Soil:%3.0f%%  H2O:%3.0f%%", data.soilMoisture, data.waterLevel);
        lcd.print(buffer);
        lcd.setCursor(0, 1);
        snprintf(buffer, 21, "Sun :%3.0f%%  pH :%3.1f", data.lightLevel, data.pH);
        lcd.print(buffer);
        lcd.setCursor(0, 2);
        snprintf(buffer, 21, "Temp:%2.0fC   Hum:%3.0f%%", data.temperature, data.airHumidity);
        lcd.print(buffer);
        lcd.setCursor(0, 3);
        snprintf(buffer, 21, "CO2 :%4d ppm", data.co2);
        lcd.print(buffer);
    }

    // --- PAGE 3: PLANT SELECT ---
    void drawPageSelect() {
        lcd.setCursor(0, 0); lcd.print("--- SELECT PLANT ---");
        if (isEditingPlant) {
            lcd.setCursor(0, 1); lcd.print("> "); 
            lcd.print(plantDB[selectedPlantIndex].name);
            lcd.print(" <  "); 
            lcd.setCursor(0, 3); lcd.print("[CLICK TO SAVE]     ");
        } else {
            lcd.setCursor(0, 1); lcd.print("  "); 
            lcd.print(plantDB[selectedPlantIndex].name);
            lcd.print("    ");
            lcd.setCursor(0, 3); lcd.print("[CLICK TO EDIT]     ");
        }
        lcd.setCursor(0, 2); 
        char buff[21];
        snprintf(buff, 21, "W:%2.0f-%2.0f%% T:%2.0f-%2.0fC", 
            plantDB[selectedPlantIndex].minSoil, plantDB[selectedPlantIndex].maxSoil,
            plantDB[selectedPlantIndex].minTemp, plantDB[selectedPlantIndex].maxTemp);
        lcd.print(buff);
    }

    // --- PAGE 4: 24H RECORDS (NEW!) ---
    void drawPageRecords() {
        lcd.setCursor(0, 0); lcd.print("--- 24H RECORDS --- ");

        // Handle case with no data
        if (historyCount == 0) {
            lcd.setCursor(0, 1); lcd.print(" collecting data... ");
            lcd.setCursor(0, 2); lcd.print(" wait 10 mins...    ");
            return;
        }

        // Calculate Min/Max for Temp and Humidity
        PlantData first = getHistoryItem(0);
        float minT = first.temperature; float maxT = first.temperature;
        float minH = first.airHumidity; float maxH = first.airHumidity;

        // Loop through valid history to find extremes
        for(int i = 0; i < historyCount; i++) {
            PlantData p = getHistoryItem(i);
            if(p.temperature < minT) minT = p.temperature;
            if(p.temperature > maxT) maxT = p.temperature;
            if(p.airHumidity < minH) minH = p.airHumidity;
            if(p.airHumidity > maxH) maxH = p.airHumidity;
        }

        // Draw Results
        char buf[21];
        lcd.setCursor(0, 1);
        snprintf(buf, 21, "Temp:%2.0fC - %2.0fC    ", minT, maxT);
        lcd.print(buf);

        lcd.setCursor(0, 2);
        snprintf(buf, 21, "Hum :%2.0f%% - %2.0f%%    ", minH, maxH);
        lcd.print(buf);

        lcd.setCursor(0, 3);
        lcd.print("Samples: "); lcd.print(historyCount);
    }
};

// --- GLOBALS ---
SmartPot myPlant;
Page currentPage = PAGE_EMOJI; 
unsigned long lastUpdate = 0;
unsigned long lastHistoryTime = 0; 
const long interval = 2000;

// --- SENSOR HELPERS ---
float getLightPercentage() {
    return (analogRead(PIN_LIGHT) / 4095.0) * 100.0;
}
float getSoilMoisture() {
    int raw = analogRead(PIN_SOIL); 
    return constrain(map(raw, DRY_VALUE, WATER_VALUE, 0, 100), 0, 100);
}
float getWaterLevel() {
    digitalWrite(PIN_WATER_POWER, HIGH); delay(10); 
    int raw = analogRead(PIN_WATER_SIGNAL);
    digitalWrite(PIN_WATER_POWER, LOW);
    return constrain(map(raw, TANK_EMPTY_RAW, TANK_FULL_RAW, 0, 100), 0, 100);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(); 
    lcd.init(); lcd.backlight(); 
    
    scd4x.begin(Wire, 0x62); 
    scd4x.stopPeriodicMeasurement(); scd4x.startPeriodicMeasurement();
    
    pinMode(PIN_LIGHT, INPUT);
    pinMode(PIN_SOIL, INPUT);
    pinMode(PIN_WATER_SIGNAL, INPUT);
    pinMode(PIN_WATER_POWER, OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW);
    pinMode(PIN_BTN, INPUT_PULLUP); 

    lcd.clear();
}

void loop() {
    // 1. INPUT HANDLING
    encoder.tick(); 
    int newPos = encoder.getPosition();
    static int lastPos = -1;

    // Button Logic
    if (digitalRead(PIN_BTN) == LOW) {
        delay(200); 
        while(digitalRead(PIN_BTN) == LOW); 

        if (currentPage == PAGE_PLANT) {
            isEditingPlant = !isEditingPlant; 
            lcd.clear();
            lastUpdate = 0; 
        }
    }

    // Rotation Logic
    if (lastPos != newPos) {
        if (isEditingPlant) {
            if (newPos > lastPos) selectedPlantIndex++;
            else selectedPlantIndex--;

            if (selectedPlantIndex > 4) selectedPlantIndex = 0;
            if (selectedPlantIndex < 0) selectedPlantIndex = 4;
        } else {
            // Page Logic (Now 0-3 for 4 pages)
            int pageIndex = abs(newPos) % 4; 
            if (pageIndex == 0) currentPage = PAGE_EMOJI;
            else if (pageIndex == 1) currentPage = PAGE_DATA;
            else if (pageIndex == 2) currentPage = PAGE_PLANT;
            else currentPage = PAGE_RECORDS;
        }
        lcd.clear(); 
        lastUpdate = 0; 
        lastPos = newPos;
    }

    // 2. PERIODIC UPDATE
    if (millis() - lastUpdate >= interval) {
        lastUpdate = millis();

        float realLight = getLightPercentage();
        float realSoil = getSoilMoisture();
        float realWater = getWaterLevel();
        float mockPH = 7.0; 

        uint16_t co2 = 0; float temp = 0.0; float hum = 0.0; 
        bool isReady = false;
        if (scd4x.getDataReadyStatus(isReady) == 0 && isReady) {
             scd4x.readMeasurement(co2, temp, hum);
        }
        if (co2 == 0) { co2 = 400; temp = 22.0; } 

        myPlant.updateSensors(realSoil, realWater, realLight, mockPH, temp, hum, co2);
        
        if (currentPage == PAGE_EMOJI) myPlant.drawPageEmoji();
        else if (currentPage == PAGE_DATA) myPlant.drawPageData();
        else if (currentPage == PAGE_PLANT) myPlant.drawPageSelect();
        else myPlant.drawPageRecords(); // New Page!
    }

    // 3. HISTORY LOGGING
    if (millis() - lastHistoryTime >= HISTORY_INTERVAL) {
        lastHistoryTime = millis();
        myPlant.logHistory();    
        myPlant.printHistory();  
    }
}
