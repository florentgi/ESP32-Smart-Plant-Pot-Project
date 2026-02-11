# 🪴 ESP32 Smart Plant Pot Project

## 📝 Project Overview
This project transforms a standard plant pot into a smart, interactive IoT device. Built using Object-Oriented C++ on the ESP32 platform, the system monitors critical environmental data to ensure optimal plant health.

Instead of raw data streams, the device uses a State Machine to display the plant's "Mood" (Tamagotchi-style) via an LCD screen (e.g., Thirsty, Happy, Sunburnt). The system is designed for battery efficiency using deep sleep cycles and aims to integrate SMS alerts and historical web logging.

## 👥 Team Members
* **Florent Gilliéron** (@florentgi)
* **Julien Mignon** (@Djkidcarter)
* **Léon Ehrwein** (@Leonewnn)

## 🛠 Hardware Requirements
* **Microcontroller:** AZ Delivery ESP32-WROOM-32
* **Sensors:**
  * Capacitive soil moisture sensor v2.0 (Soil Moisture)
  * Velleman VMA 303 Water Sensor (water level in internal water reservoir)
  * Light dection Module v2.0 (light exposure)
  * Freenove pH meter v1.1 (soil pH)
  * ESP32-WROVER-E with integrated camera 
* **Actuators:** 
* **Other:** 

## 🔌 Wiring Diagram (Pinout)
| Component | ESP32 Pin | Type | Power |
| :--- | :--- | :--- | :--- |
| **Light Sensor (AO)** | GPIO 34 | Analog Input | 3V3 |
| **LCD Screen (SDA)** | GPIO 21 | I2C Data | 5V |
| **LCD Screen (SCL)** | GPIO 22 | I2C Clock | - |
| **SCD41 Sensor (SDA)** | GPIO 21 | I2C Data | 3V3 |
| **SCD41 Sensor (SCL)** | GPIO 22 | I2C Clock | - |

## 💻 Software Setup
1. **Board Manager:** Install ESP32 by Espressif Systems in Arduino IDE.
2. **Libraries Needed:**
   * `DHT sensor library` by Adafruit
   * `PubSubClient` (for MQTT)
3. **Configuration:** Create a `secrets.h` file with your WiFi credentials (do not commit this file!).

## 🚀 How to Run
1. Clone the repo.
2. Open `smart_plant_pot/smart_plant_pot.ino`.
3. Select **DOIT ESP32 DEVKIT V1** in the board menu.
4. Click **Upload**.
