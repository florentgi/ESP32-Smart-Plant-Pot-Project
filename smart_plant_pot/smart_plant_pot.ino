#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SensirionCore.h>
#include <SensirionI2CScd4x.h>

// --- Hardware Settings ---
#define PIN_LIGHT 34        // Analog pin for Light Sensor
const float TEMP_OFFSET = -15.0; // Calibration: Internal CPU is hotter than air

// --- LCD Display ---
// Set address to 0x27. Columns = 20, Rows = 4
LiquidCrystal_I2C lcd(0x27, 20, 4);

// SCD41 Sensor Object
SensirionI2CScd4x scd4x;

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
    float temperature;    // Celsius
    float airHumidity;  // 0 - 100%
    uint16_t co2;       //
};

// --- SmartPot Class ---
class SmartPot {
private:
    PlantData data;
    PlantMood currentMood;

public:
    // Constructor
    SmartPot() : currentMood(HAPPY) {}

    // Method to update sensor data (In Phase 2, this will accept real sensor readings)
    void updateSensors(float moisture, float water, float light, float ph, float temp) {
        data.soilMoisture = moisture;
        data.waterLevel = water;
        data.lightLevel = light;
        data.pH = ph;
        data.temperature = temp;
        
        // Recalculate mood immediately after new data arrives
        evaluateMood();
    }

    // The "Tamagotchi" Logic Engine
    // Order matters here: critical alerts (Empty Tank) usually take precedence
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

    // Returns a string representation of the emoji for the LCD (and later Web/SMS)
    String getEmoji() {
        switch (currentMood) {
            case HAPPY:        return "( ^_^)  [Happy]";
            case THIRSTY:      return "( >_<)  [Thirsty]";
            case OVERWATERED:  return "( ~_~)  [Too Wet]";
            case EMPTY_TANK:   return "( O_O)  [Refill Me]";
            case SUNBURNT:     return "( x_x)  [Too Hot]";
            case COLD:         return "( *_* ) [Cold]";
            case PH_IMBALANCE: return "( ?_?)  [Bad Soil]";
            default:           return "( o_o)  [Unknown]";
        }
    }

    // Debugging method to see what's happening in the Serial Monitor
    void printStatus() {
        Serial.println("\n--- Plant Status Report ---");
        Serial.printf("Current Mood: %s\n", getEmoji().c_str());
        Serial.printf("Soil Moisture: %.1f%%\n", data.soilMoisture);
        Serial.printf("Reservoir Lvl: %.1f%%\n", data.waterLevel);
        Serial.printf("pH Level:      %.1f\n", data.pH);
        Serial.printf("Light Level:   %.1f%%\n", data.lightLevel);
        Serial.printf("Temperature:   %.1f C\n", data.temperature);
        Serial.println("---------------------------");
    }

    void updateDisplay() {
        // ROW 0: Mood & CO2 (Air Quality)
        lcd.setCursor(0, 0);
        // Format: "Happy (^_^) CO2:800"
        lcd.printf("%s CO2:%d", getEmoji().c_str(), data.co2); 
    
        // ROW 1: Soil & Water
        lcd.setCursor(0, 1);
        lcd.printf("Soil:%3.0f%%  H2O:%3.0f%%", data.soilMoisture, data.waterLevel);
    
        // ROW 2: Light & Humidity (New!)
        lcd.setCursor(0, 2);
        lcd.printf("Sun: %3.0f%%  Hum:%3.0f%%", data.lightLevel, data.airHumidity);
    
        // ROW 3: Temperature & pH
        lcd.setCursor(0, 3);
        lcd.printf("Temp:%4.1fC  pH:%4.1f", data.temperature, data.pH);
    }
};

// --- Global Objects ---
SmartPot myPlant;

// Reads standard I2C data from SCD41
void readEnvironment(PlantData &data) {
    uint16_t co2 = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;
    bool isDataReady = false;

    // Check if data is ready (SCD41 is slow, updates every 5s)
    uint16_t dataReadyState = 0;
    scd4x.getDataReadyStatus(dataReadyState);
    
    // If the last 11 bits are not 0, data is ready
    if ((dataReadyState & 0x07FF) != 0) {
        // Read the actual measurement
        uint16_t error = scd4x.readMeasurement(co2, temperature, humidity);
        
        if (error == 0) {
            data.temperature = temperature;
            data.airHumidity = humidity;
            data.co2 = co2;
        } else {
            Serial.print("Error reading SCD41: ");
            Serial.println(error);
        }
    }
}

// Helper to read and convert Light to %
float getLightPercentage() {
    int rawValue = analogRead(PIN_LIGHT);
    // ESP32 Analog is 0 (0V) to 4095 (3.3V)
    // Most LDR modules: Low Value = Bright, High Value = Dark (Inverted)
    // But check your specific module! If 4095 is dark for you:
    float percentage = (rawValue / 4095.0) * 100.0;
    
    // IF your sensor reads 0 when bright and 4095 when dark, 
    // uncomment the line below to invert it:
    // percentage = 100.0 - percentage; 
    
    return percentage;
}

void setup() {
    Serial.begin(115200);
    Wire.begin(); // Start the I2C bus
    
    // LCD Initialization
    lcd.init();      // Initialize the I2C LCD module
    lcd.backlight(); // Turn on the backlight
    
    // Boot Screen
    lcd.setCursor(2, 0); // Column 2, Row 0
    lcd.print("Smart Pot System");

    // 2. SCD41 Setup
    scd4x.begin(Wire);
    
    // Stop any previous measurement to ensure clean start
    scd4x.stopPeriodicMeasurement(); 
    
    // Start measuring (SCD41 updates every 5 seconds)
    scd4x.startPeriodicMeasurement();
    
    lcd.setCursor(4, 1); 
    lcd.print("Initializing...");
    delay(2000);
    lcd.clear();
    
    // ... existing pin setups ...
    pinMode(PIN_LIGHT, INPUT);
}

void loop() {
    // --- REAL SENSORS (Phase 2.3) ---
    float realLight = getLightPercentage();
    float realTemp = getInternalTemp();

    // Because SCD41 is complex, let's just create temp vars here
    uint16_t co2 = 0;
    float temp = 0.0;
    float hum = 0.0;

    // Only read if SCD41 has data ready (Non-blocking)
    uint16_t ready = 0;
    scd4x.getDataReadyStatus(ready);
    if ((ready & 0x07FF) != 0) {
        scd4x.readMeasurement(co2, temp, hum);
    }

    // --- MOCK DATA (Keep these for sensors we haven't wired yet) ---
    float mockMoisture = 50.0; 
    float mockWater = 80.0;
    float mockPH = 6.5;

    // Send hybrid data (Real + Mock) to the Brain
    myPlant.updateSensors(realSoil, mockWater, realLight, mockPH, temp, hum, co2);
    
    myPlant.printStatus();
    myPlant.updateDisplay();
    
    // Debug specific to this phase (helps you calibrate)
    Serial.print("DEBUG -> Raw Light Pin (0-4095): ");
    Serial.println(analogRead(PIN_LIGHT)); 

    delay(2000); // Faster updates for testing light
}
