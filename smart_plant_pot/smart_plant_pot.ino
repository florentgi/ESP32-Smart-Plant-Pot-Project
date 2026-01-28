#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionI2cScd4x.h>
#include <RotaryEncoder.h>

// --- Hardware Settings ---
#define PIN_LIGHT 34        // Analog pin for Light Sensor
#define PIN_WATER_SIGNAL 32 // ADC Pin to read data
#define PIN_WATER_POWER  25 // Digital Pin to power sensor
#define PIN_SOIL 35
// #define PIN_PH 33 // pH sensor

// --- ROTARY ENCODER PINS ---
#define PIN_IN1 27 // CLK (Wire to GPIO 27)
#define PIN_IN2 26 // DT  (Wire to GPIO 26)
#define PIN_BTN 14 // SW  (Wire to GPIO 14)

// --- LCD Display ---
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- SCD41 Sensor Object ---
SensirionI2cScd4x scd4x;

// --- ROTARY ENCODER OBJECT (This was missing!) ---
// using LatchMode::TWO03 is common for KY-040
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);

// --- SENSOR CALIBRATION ---
const int DRY_VALUE    = 2933; 
const int WATER_VALUE  = 2019; 
const int TANK_EMPTY_RAW = 0;   
const int TANK_FULL_RAW  = 1421; 

// --- Configuration & Constants ---
enum PlantMood { 
    HAPPY, THIRSTY, OVERWATERED, EMPTY_TANK, SUNBURNT, COLD, PH_IMBALANCE 
};

// UI Pages
enum Page { PAGE_EMOJI, PAGE_DATA };

// Struct to hold all sensor values
struct PlantData {
    float soilMoisture;   
    float waterLevel;     
    float lightLevel;     
    float pH;             
    float temperature;    
    float airHumidity;    
    uint16_t co2;         
};

// --- SmartPot Class ---
class SmartPot {
private:
    PlantData data;
    PlantMood currentMood;

public:
    SmartPot() : currentMood(HAPPY) {
        data = {0, 0, 0, 7.0, 20.0, 50.0, 400};
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
        if (data.waterLevel < 10.0) currentMood = EMPTY_TANK; 
        else if (data.soilMoisture < 20.0) currentMood = THIRSTY;
        else if (data.soilMoisture > 85.0) currentMood = OVERWATERED;
        else if (data.temperature < 10.0) currentMood = COLD;
        else if (data.pH < 5.5 || data.pH > 7.5) currentMood = PH_IMBALANCE;
        else if (data.lightLevel > 90.0) currentMood = SUNBURNT;
        else currentMood = HAPPY;
    }

    String getEmoji() {
        switch (currentMood) {
            case HAPPY:        return "( ^_^) ";
            case THIRSTY:      return "( >_<) ";
            case OVERWATERED:  return "( ~_~) ";
            case EMPTY_TANK:   return "( O_O) ";
            case SUNBURNT:     return "( x_x) ";
            case COLD:         return "(*_* ) ";
            case PH_IMBALANCE: return "( ?_?) ";
            default:           return "( o_o) ";
        }
    }

    String getMoodText() {
        switch (currentMood) {
            case HAPPY:        return "Happy";
            case THIRSTY:      return "Thirsty!";
            case OVERWATERED:  return "Too Wet!";
            case EMPTY_TANK:   return "Refill!";
            case SUNBURNT:     return "Too Hot!";
            case COLD:         return "Too Cold!";
            case PH_IMBALANCE: return "Bad pH";
            default:           return "Unknown";
        }
    }

    void printStatus() {
        Serial.println("\n--- Plant Status Report ---");
        Serial.printf("Mood: %s\n", getMoodText().c_str());
        Serial.printf("Soil: %.1f%% | Water: %.1f%%\n", data.soilMoisture, data.waterLevel);
    }

    // --- PAGE 1: THE FACE ---
    void drawPageEmoji() {
        lcd.setCursor(0, 0);
        lcd.print("    SMART POT OS    "); 
        
        lcd.setCursor(5, 1);
        lcd.print(getEmoji()); 
        
        lcd.setCursor(5, 2);
        lcd.print(getMoodText()); 

        lcd.setCursor(0, 3);
        lcd.print(" < Turn for Stats > ");
    }

    // --- PAGE 2: THE DATA ---
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
};

// --- GLOBALS ---
SmartPot myPlant;
Page currentPage = PAGE_EMOJI; 
unsigned long lastUpdate = 0;
const long interval = 2000;

// --- SENSOR FUNCTIONS ---
float getLightPercentage() {
    int rawValue = analogRead(PIN_LIGHT);
    return (rawValue / 4095.0) * 100.0;
}

float getSoilMoisture() {
    int rawValue = analogRead(PIN_SOIL); // using GPIO 35
    int percentage = map(rawValue, DRY_VALUE, WATER_VALUE, 0, 100);
    return constrain(percentage, 0, 100);
}

// NOTE: You must have PIN_SOIL defined. I added '#define PIN_SOIL 35' below implicitly?
// No, let's fix that. Your snippet was missing PIN_SOIL in the #define section.
// I will assume GPIO 35 based on history.

float getWaterLevel() {
    digitalWrite(PIN_WATER_POWER, HIGH);
    delay(10); 
    int raw = analogRead(PIN_WATER_SIGNAL);
    digitalWrite(PIN_WATER_POWER, LOW);
    int percent = map(raw, TANK_EMPTY_RAW, TANK_FULL_RAW, 0, 100);
    return constrain(percent, 0, 100);
}

// ph sensor
/*float getPHLevel() {
    int raw = analogRead(PIN_PH);
    float voltage = raw * (3.3 / 4095.0);
    
    // Standard estimation for Freenove modules
    float phValue = 3.5 * voltage; 
    
    // --- CALIBRATION FIX ---
    // Change "0.0" to the difference you calculated in Step 3.
    // Example: If it read 7.8 but should be 8.3, put +0.5
    float calibrationOffset = 0.0; // <--- CHANGE THIS NUMBER
    
    return phValue + calibrationOffset; 
}*/

void setup() {
    Serial.begin(115200);
    Wire.begin(); 
    
    // LCD
    lcd.init();      
    lcd.backlight(); 
    
    // SCD41 (Fixed Address Here!)
    scd4x.begin(Wire, 0x62); 
    scd4x.stopPeriodicMeasurement(); 
    scd4x.startPeriodicMeasurement();
    
    // Pins
    pinMode(PIN_LIGHT, INPUT);
    // Missing PIN_SOIL definition in your provided snippet, ensuring it works:
    pinMode(35, INPUT); // Hardcoded 35 if define is missing
    
    pinMode(PIN_WATER_SIGNAL, INPUT);
    pinMode(PIN_WATER_POWER, OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW);

    //pinMode(PIN_PH, INPUT); //pH sensor
    
    // Button
    pinMode(PIN_BTN, INPUT_PULLUP); 

    lcd.setCursor(0,0);
    lcd.print("System Ready!");
    delay(1000);
    lcd.clear();
}

void loop() {
    // 1. ROTARY ENCODER CHECK
    encoder.tick(); // Now 'encoder' exists!
    
    int newPos = encoder.getPosition();
    static int lastPos = -1;

    if (lastPos != newPos) {
        if (newPos % 2 == 0) currentPage = PAGE_EMOJI;
        else currentPage = PAGE_DATA;
        
        lcd.clear(); 
        lastUpdate = 0; 
        lastPos = newPos;
    }

    // 2. SENSOR UPDATE
    if (millis() - lastUpdate >= interval) {
        lastUpdate = millis();

        float realLight = getLightPercentage();
        float realSoil = getSoilMoisture();
        float realWater = getWaterLevel();
        //float realPH = getPHLevel(); // pH sensor
        float mockPH = 6.5;

        uint16_t co2 = 0; float temp = 0.0; float hum = 0.0; 
        bool isReady = false;
        
        // Fixed bool reference logic
        if (scd4x.getDataReadyStatus(isReady) == 0 && isReady) {
             scd4x.readMeasurement(co2, temp, hum);
        }
        if (co2 == 0) { co2 = 400; temp = 22.0; } 

        myPlant.updateSensors(realSoil, realWater, realLight, mockPH, temp, hum, co2);
        
        if (currentPage == PAGE_EMOJI) myPlant.drawPageEmoji();
        else myPlant.drawPageData();
        
        myPlant.printStatus();
    }
}
