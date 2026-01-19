# 🪴 ESP32 Smart Plant Pot Project

## 📝 Project Overview
This project transforms a standard plant pot into a smart, interactive IoT device. Built using Object-Oriented C++ on the ESP32 platform, the system monitors critical environmental data to ensure optimal plant health.

Instead of raw data streams, the device uses a State Machine to display the plant's "Mood" (Tamagotchi-style) via an LCD screen (e.g., Thirsty, Happy, Sunburnt). The system is designed for battery efficiency using deep sleep cycles and aims to integrate SMS alerts and historical web logging.

## 👥 Team Members
* **Florent Gilliéron** (@florentgi)
* **Julien Mignon**
* **Léon Ehrwein**

## 🛠 Hardware Requirements
* **Microcontroller:** AZ Delivery ESP32-WROOM-32
* **Sensors:**
  * Capacitive soil moisture sensor v2.0 (Soil Moisture)
  * Velleman VMA 303 Water Sensor (water level in internal water reservoir)
  * Light dection Module v2.0 (light exposure)
  * Freenove pH meter v1.1 (soil pH)
* **Actuators:** 
* **Other:** 

## 🔌 Wiring Diagram (Pinout)
| Component | ESP32 Pin | Type |
| :--- | :--- | :--- |
| Light Sensor (AO) | GPIO 34 | Analog Input |

## 💻 Software Setup
1. **Board Manager:** Install ESP32 by Espressif Systems in Arduino IDE.
2. **Libraries Needed:**
   * `DHT sensor library` by Adafruit
   * `PubSubClient` (for MQTT)
3. **Configuration:** Create a `secrets.h` file with your WiFi credentials (do not commit this file!).

## 🚀 How to Run
1. Clone the repo.
2. Open `src/ProjectName/ProjectName.ino`.
3. Select **DOIT ESP32 DEVKIT V1** in the board menu.
4. Click **Upload**.
