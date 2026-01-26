# 🗒️ Logbook

## 🗓️ Monday, 12th of January 2026
* **Florent Gilliéron**: Searching for IoT project ideas
* **Julien Mignon**: Searching for IoT project ideas
* **Léon Ehrwein**: Learning the basics of Arduino

## 🗓️ Wednesday, 14th of January 2026
* **Florent Gilliéron**: Searching for IoT project ideas. Creation of the first code. Creation of a Github Repository.
* **Julien Mignon**: Searching for IoT project ideas. Creation of the first code.
* **Léon Ehrwein**: Learning the basics of Arduino and ESP32 with different sensors
* 
* **Result**: We decided to create a Smart Plant Pot for our IoT project. The project has been validated by Heikki, and we have already obtained the required materials. We have also written our first code, which uses the ESP32’s light and temperature sensors. However, we plan to add an external temperature sensor to achieve better accuracy. A Github Repository was created for better collaboration and versioning.

## 🗓️ Monday, 19th of January 2026

* **Florent Gilliéron**: worked on the LCD screen and temperature/CO2 sensor.
* **Julien Mignon**: worked on the LCD screen and temperature/CO2 sensor.
* **Léon Ehrwein**: Joined the group and start to help the two others students after learning the basics of Arduino and ESP32. Worked on the LCD screen and temperature/CO2 sensor.

* **Results**: We worked on wiring a large LCD screen to the ESP32. We had to use a bread board so we had enough grounds and power pins, plus we could have our light sensor plugged in too. We managed to get the screen to display all the data. We had to adapt our code in order to make this work. So far, we have real data only for the light and have mockup data for the moisture/water/PH level. We also worked on setting up a temperature/CO2 sensor. For the moment, we have some issues with it. It seems that we are having a problem with the  specific library, Sensirion. We will need to work on the layout of the information on the screen next. We are also planning to calibrate our sensors for the following session, so we will need to get some soil.

## 🗓️ Wednesday, 21st of January 2026

* **Florent Gilliéron**: Working on fixing some code issues related to the temperature/CO2 sensor. Worked on moisture sensor calibration.
* **Julien Mignon**: Updated the daily log. Worked on the wiring of the moisture sensor and its calibration
* **Léon Ehrwein**: Updated the README document. Worked on a diagram.

* **Results**: Today, we have been very productive by fixing the issue with our temperature/CO2 sensor. We have also added and calibrated our moisture sensor. All the data was displayed succesfully on the LCD screen. For the calibration, we had to take a series of measures when the sensor was dry and submerged in water. We got a high limit when dry and a low limit when wet. We adapted our code to work this range as a percentage and this is what is displayed on our screen.

## 🗓️ Monday, 26th of January 2026

* **Florent Gilliéron**: Working on the calibration of the water level sensor.
* **Julien Mignon**: Updated the daily log. Working on the calibration of the water level sensor.
* **Léon Ehrwein**: Updated the README document andd the diagram. Working on the calibration of the water level sensor.

* **Results**: After doing some research on resistive sensors, we found out that they suffer from electrolysis (corrosion) if you leave them powered on 24/7. The metal traces will literally eat themselves away within weeks. So, we will not connect the sensor's power pin to 3.3V. Instead, we will connect it to a GPIO Pin. We will turn the sensor ON for 10 milliseconds to take a reading, and then immediately turn it OFF. This should extend the sensor's life from weeks to years.
  
* For the calibration, we took 2 measures. One with a dry sensor and another when submerged. As a result, we had a range of values that we adapted in our code as percentage for a better user experience and visibility.
Next, we asked Heikki for a rotary encoder module. This a rotary button allowing us to have pages on our LCD screen. Our idea is to display an emoji on the main page for simplicity and readability, happy face if everything is good and so on. We want to use the following pages to display the data collected by the sensors. We spent the rest of the session adapting the code to implement theses changes successfully.









