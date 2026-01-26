#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionI2cScd4x.h>

// --- Hardware Settings ---
#define PIN_LIGHT 34        // Analog pin for Light Sensor
#define PIN_WATER_SIGNAL 32 // ADC Pin to read data
#define PIN_WATER_POWER  25 // Digital Pin to power sensor

// --- LCD Display ---
// Set address to 0x27. Columns = 20, Rows = 4
LiquidCrystal_I2C lcd(0x27, 20, 4);

// SCD41 Sensor Object 
SensirionI2cScd4x scd4x;

// --- SENSOR CALIBRATION ---
const int DRY_VALUE   = 2933.33;  // Replace with your "Dry" number
const int WATER_VALUE = 2019.28;  // Replace with your "Wet" number
const int TANK_EMPTY_RAW = 0;    // Reading in air
const int TANK_FULL_RAW  = 1421; // Reading fully submerged


// --- Configuration & Constants ---
// Define the possible states for our Tamagotchi interface
enum PlantMood { 
    HAPPY, 
    THIRSTY,       // Soil moisture low
    OVERWATERED,   // Soil moisture too high
    EMPTY_TANK,    // Reservoir needs refill
    SUNBURNT,      // Light too high
    COLD,          // Temperature too low
    PH_IMBALANCE   // pH out of range
};

// Struct to hold all sensor values in one place
struct PlantData {
    float soilMoisture;   // 0 - 100%
    float waterLevel;     // 0 - 100% (Reservoir level)
    float lightLevel;     // 0 - 100%
    float pH;             // 0.0 - 14.0
    float temperature;    // Celsius (from SCD41)
    float airHumidity;    // 0 - 100% (from SCD41)
    uint16_t co2;         // CO2 in ppm (from SCD41)
};

// --- SmartPot Class ---
class SmartPot {
private:
    PlantData data;
    PlantMood currentMood;

public:
    // Constructor
    SmartPot() : currentMood(HAPPY) {
        // Initialize data to safe defaults
        data.soilMoisture = 0.0;
        data.waterLevel = 0.0;
        data.lightLevel = 0.0;
        data.pH = 7.0;
        data.temperature = 20.0;
        data.airHumidity = 0.0;
        data.co2 = 400;
    }

    // Method to update sensor data with all 7 parameters
    void updateSensors(float moisture, float water, float light, float ph, float temp, float humidity, uint16_t co2Val) {
        data.soilMoisture = moisture;
        data.waterLevel = water;
        data.lightLevel = light;
        data.pH = ph;
        data.temperature = temp;
        data.airHumidity = humidity;
        data.co2 = co2Val;
        
        // Recalculate mood immediately after new data arrives
        evaluateMood();
    }

    // The "Tamagotchi" Logic Engine
    void evaluateMood() {
        if (data.waterLevel < 10.0) {
            currentMood = EMPTY_TANK; 
        } else if (data.soilMoisture < 20.0) {
            currentMood = THIRSTY;
        } else if (data.soilMoisture > 85.0) {
            currentMood = OVERWATERED;
        } else if (data.temperature < 10.0) {
            currentMood = COLD;
        } else if (data.pH < 5.5 || data.pH > 7.5) {
            currentMood = PH_IMBALANCE;
        } else if (data.lightLevel > 90.0) {
            currentMood = SUNBURNT;
        } else {
            currentMood = HAPPY;
        }
    }

    // Returns a string representation of the emoji
    String getEmoji() {
        switch (currentMood) {
            case HAPPY:        return "( ^_^) [Hpy]"; // Shortened text to fit buffer
            case THIRSTY:      return "( >_<) [Dry]";
            case OVERWATERED:  return "( ~_~) [Wet]";
            case EMPTY_TANK:   return "( O_O) [H2O]";
            case SUNBURNT:     return "( x_x) [Hot]";
            case COLD:         return "(*_* ) [Cld]";
            case PH_IMBALANCE: return "( ?_?) [Bad]";
            default:           return "( o_o) [???]";
        }
    }

    // IMPROVEMENT B: Cleaner Serial Debugging using printf (ESP32 only)
    void printStatus() {
        Serial.println("\n--- Plant Status Report ---");
        Serial.printf("Current Mood:  %s\n", getEmoji().c_str());
        Serial.printf("Soil Moisture: %.1f%%\n", data.soilMoisture);
        Serial.printf("Reservoir Lvl: %.1f%%\n", data.waterLevel);
        Serial.printf("pH Level:      %.1f\n", data.pH);
        Serial.printf("Light Level:   %.1f%%\n", data.lightLevel);
        Serial.printf("Temperature:   %.1f C\n", data.temperature);
        Serial.printf("Air Humidity:  %.1f%%\n", data.airHumidity);
        Serial.printf("CO2 Level:     %d ppm\n", data.co2);
        Serial.println("---------------------------");
    }

    // IMPROVEMENT A: Flicker-Free Display Update
    void updateDisplay() {
        // We use a buffer to format the line exactly to 20 chars
        // This overwrites old characters with spaces, so no lcd.clear() is needed
        char buffer[21]; 

        // ROW 0: Mood & CO2
        // %-12s: Left align string, 12 chars wide
        // %4d:   Right align integer, 4 digits wide
        lcd.setCursor(0, 0);
        String emoji = getEmoji();
        snprintf(buffer, 21, "%-12s CO2:%4d", emoji.substring(0, 12).c_str(), data.co2);
        lcd.print(buffer);

        // ROW 1: Soil & Water
        lcd.setCursor(0, 1);
        snprintf(buffer, 21, "Soil:%3d%%  H2O:%3d%%", (int)data.soilMoisture, (int)data.waterLevel);
        lcd.print(buffer);

        // ROW 2: Light & Humidity
        lcd.setCursor(0, 2);
        snprintf(buffer, 21, "Sun :%3d%%  Hum:%3d%%", (int)data.lightLevel, (int)data.airHumidity);
        lcd.print(buffer);

        // ROW 3: Temp & pH
        // %4.1f: Float, 4 chars total width, 1 decimal place
        lcd.setCursor(0, 3);
        snprintf(buffer, 21, "Temp:%4.1fC pH :%4.1f", data.temperature, data.pH);
        lcd.print(buffer);
    }
};

// --- Global Objects ---
SmartPot myPlant;
unsigned long lastUpdate = 0;       // Timer tracker
const long interval = 2000;         // Update interval (2 seconds)

// Helper to read and convert Light to %
float getLightPercentage() {
    int rawValue = analogRead(PIN_LIGHT);
    // ESP32 Analog is 0 (0V) to 4095 (3.3V)
    float percentage = (rawValue / 4095.0) * 100.0;
    return percentage;
}

float getSoilMoisture() {
    int rawValue = analogRead(35); // Read GPIO 35
    
    // Map raw range to 0-100%
    // syntax: map(value, low_input, high_input, low_output, high_output)
    int percentage = map(rawValue, DRY_VALUE, WATER_VALUE, 0, 100);
    
    // Fix edge cases (if it goes slightly below 0% or above 100%)
    return constrain(percentage, 0, 100);
}

float getWaterLevel() {
    // 1. Power ON
    digitalWrite(PIN_WATER_POWER, HIGH);
    delay(10); 
    
    // 2. Read value
    int raw = analogRead(PIN_WATER_SIGNAL);
    
    // 3. Power OFF
    digitalWrite(PIN_WATER_POWER, LOW);

    // 4. Map
    int percent = map(raw, TANK_EMPTY_RAW, TANK_FULL_RAW, 0, 100);
    return constrain(percent, 0, 100);
}

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    Wire.begin(); 
    
    // LCD Initialization
    lcd.init();      
    lcd.backlight(); 
    
    // Boot Screen
    lcd.clear();
    lcd.setCursor(2, 0); 
    lcd.print("Smart Pot System");

    // SCD41 Setup 
    scd4x.begin(Wire, 0x62);
    scd4x.stopPeriodicMeasurement(); 
    delay(500);
    scd4x.startPeriodicMeasurement();
    
    lcd.setCursor(4, 1); 
    lcd.print("Initializing...");
    delay(2000);
    
    // Clear once before entering the loop to ensure clean slate
    lcd.clear(); 
    
    // Pin setup
    pinMode(PIN_LIGHT, INPUT);

    // Configure Water Sensor Pins
    pinMode(PIN_WATER_SIGNAL, INPUT);
    pinMode(PIN_WATER_POWER, OUTPUT);
    digitalWrite(PIN_WATER_POWER, LOW); // Keep it OFF by default!
    
    Serial.println("Smart Pot System Started!");
}

void loop() {
    // IMPROVEMENT C: Non-Blocking Timer Logic
    unsigned long currentMillis = millis();

    // Check if 2 seconds have passed
    if (currentMillis - lastUpdate >= interval) {
        lastUpdate = currentMillis; // Reset timer

        // --- 1. REAL SENSORS ---
        float realLight = getLightPercentage();
        float realSoil = getSoilMoisture();
        float realWater = getWaterLevel();

        // SCD41 sensor values 
        uint16_t co2 = 400;    
        float temp = 20.0;      
        float hum = 50.0;      

        bool isDataReady = false;
        uint16_t error = scd4x.getDataReadyStatus(isDataReady);
        
        if (error == 0 && isDataReady) {
            error = scd4x.readMeasurement(co2, temp, hum);
            if (error != 0) {
                Serial.printf("Error reading SCD41: %d\n", error);
            }
        }

        // --- 2. MOCK DATA (For missing sensors) ---
        float mockWater = 80.0;  
        float mockPH = 6.5;      

        // --- 3. LOGIC UPDATE ---
        myPlant.updateSensors(realSoil, realWater, realLight, mockPH, temp, hum, co2);
        
        // --- 4. DISPLAY UPDATE ---
        myPlant.printStatus();
        myPlant.updateDisplay();
        
    }
}
