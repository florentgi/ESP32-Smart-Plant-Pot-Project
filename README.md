The Smart Gardener – A Connected Plant Pot 🪴

The Smart Gardener is a fully integrated IoT solution designed to monitor and maintain plant health. Developed as part of the IoT Experimental Project (DIG008AS3AE-3004), this project bridges the gap between hardware sensors and a modern web dashboard, providing real-time insights into your plant's environment and mood.

👥 The Team

Florent Gilliéron – Firmware, Web Development & Electronics

Julien Mignon – Firmware, Web Development & Electronics

Léon Ehrwein – Electronics, 3D Modeling & Design

🚀 Overview

The Smart Gardener isn't just a sensor; it’s a complete end-to-end IoT ecosystem. By combining environmental data with specific plant profiles, the system determines the "mood" of your plant and provides actionable data via a local LCD interface and a remote web dashboard.

Key Features

Multi-Sensor Monitoring: Tracks soil moisture, light levels, CO2, temperature, and humidity.

Intelligent Mood Logic: A state machine compares live data against pre-defined profiles (Monstera, Snake Plant, etc.) to report if the plant is "Happy," "Thirsty," or "Too Hot."

On-Device UI: A 20x4 LCD screen with a rotary encoder for manual navigation through data pages and plant selection.

Cloud Connectivity: Data is synced every 10 minutes to a Firebase Realtime Database.

Web Dashboard: A React-based frontend featuring live metrics, historical charts, and local weather forecasts.

Timelapse Photography: An integrated ESP32-CAM module captures daily photos, uploading them directly to GitHub to track growth over time.

Custom Enclosure: A 3D-printed outer pot designed in Blender to house all electronics safely.

🛠️ Tech Stack

Hardware

Microcontrollers: ESP32 (Main Hub) & ESP32-CAM (Photography)

Sensors: * SCD41 (CO2, Temp, Humidity)

Capacitive Soil Moisture Sensor

Resistive Water Level Sensor (Pulse-powered to prevent corrosion)

Light Sensor (LDR)

Interface: 20x4 LCD (I2C) & Rotary Encoder

Software & Cloud

Firmware: C++ (Arduino IDE)

Backend/Database: Firebase Realtime Database

Frontend: React 19, TypeScript, Vite

Data Visualization: Recharts

APIs: GitHub API (Image Hosting), FMI API (Weather Data)

Design: Blender (3D Printing)

🏗️ Implementation Details

Sensor Calibration & Logic

To ensure accuracy, the soil moisture sensor was manually calibrated using dry vs. submerged values. To solve the common issue of probe corrosion in water level sensors, we implemented a GPIO-controlled power cycle, only powering the sensor for 10ms during readings.

The Dashboard

The web dashboard provides a high-level view of the plant's health. It features:

Live Mood Indicator: Immediate visual feedback on the plant's status.

Historical Trends: Graphs showing moisture and temperature fluctuations over 24 hours.

Weather Integration: Real-time data from the Finnish Meteorological Institute (FMI).

Note: Below are screenshots of the web dashboard in action.

(Insert your dashboard images here)

📈 Outcome & Learning

This project was a journey from zero electronics experience to a functional full-stack IoT system. Key takeaways included:

Hardware Challenges: Navigating I2C address conflicts and library configuration (especially the SCD41 and Firebase authentication).

System Architecture: Structuring C++ code with state machines to handle multiple UI pages and sensor thresholds.

Problem Solving: Learning the "patience of the wire"—spending hours debugging a single misplaced connection to finally achieve a working system.

🔮 Future Roadmap

Automated Irrigation: Adding a water pump for self-watering capabilities.

Mobile Alerts: Push notifications via Pushbullet or Telegram when the plant needs urgent care.

Enhanced Gallery: Improving the ESP32-CAM positioning and creating a smoother timelapse playback in the dashboard.

Expanded Library: Adding more plant species and detailed horticultural data to the database.

📂 Repository Structure

/firmware: Arduino sketches for the ESP32 and ESP32-CAM.

/dashboard: React + TypeScript source code.

/3d-models: .stl and Blender files for the pot enclosure.

/assets: Project images and documentation.

Created for the IoT Experimental Project at Haaga-Helia university of applied science.
