#include <Arduino.h>

// --- Hardware Settings ---
#define PIN_LIGHT 34        // Analog pin for Light Sensor
const float TEMP_OFFSET = -15.0; // Calibration: Internal CPU is hotter than air

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
};

// --- Global Objects ---
SmartPot myPlant;

// Helper to read ESP32 Internal Temp
float getInternalTemp() {
    // temperatureRead() is a built-in ESP32 function
    float rawTemp = temperatureRead(); 
    return rawTemp + TEMP_OFFSET;
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
    analogReadResolution(12); // Force 0-4095 range
    pinMode(PIN_LIGHT, INPUT);
    
    delay(1000); 
    Serial.println("Smart Pot System: Phase 2.3 (Light & Temp)");
}

void loop() {
    // --- REAL SENSORS (Phase 2.3) ---
    float realLight = getLightPercentage();
    float realTemp = getInternalTemp();

    // --- MOCK DATA (Keep these for sensors we haven't wired yet) ---
    float mockMoisture = 50.0; 
    float mockWater = 80.0;
    float mockPH = 6.5;

    // Send hybrid data (Real + Mock) to the Brain
    myPlant.updateSensors(mockMoisture, mockWater, realLight, mockPH, realTemp);
    
    myPlant.printStatus();
    
    // Debug specific to this phase (helps you calibrate)
    Serial.print("DEBUG -> Raw Light Pin (0-4095): ");
    Serial.println(analogRead(PIN_LIGHT)); 

    delay(2000); // Faster updates for testing light
}
