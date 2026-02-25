#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <Firebase_ESP_Client.h>

// Built-in Base64 library for ESP32
#include "mbedtls/base64.h"

#include <addons/TokenHelper.h>
#include "secrets.h"

// --- CONFIGURATION ---
const char* githubHost = "api.github.com";

// Camera Pins (Freenove ESP32 WROVER)
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    21
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      19
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      5
#define Y2_GPIO_NUM      4
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Forward Declarations
String uploadToGitHub();
void saveLinkToFirebase(String url);
void executePhotoRoutine();

void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // 1. WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("\nConnecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  // 2. Camera Init
  camera_config_t configCam;
  configCam.ledc_channel = LEDC_CHANNEL_0;
  configCam.ledc_timer = LEDC_TIMER_0;
  configCam.pin_d0 = Y2_GPIO_NUM;
  configCam.pin_d1 = Y3_GPIO_NUM;
  configCam.pin_d2 = Y4_GPIO_NUM;
  configCam.pin_d3 = Y5_GPIO_NUM;
  configCam.pin_d4 = Y6_GPIO_NUM;
  configCam.pin_d5 = Y7_GPIO_NUM;
  configCam.pin_d6 = Y8_GPIO_NUM;
  configCam.pin_d7 = Y9_GPIO_NUM;
  configCam.pin_xclk = XCLK_GPIO_NUM;
  configCam.pin_pclk = PCLK_GPIO_NUM;
  configCam.pin_vsync = VSYNC_GPIO_NUM;
  configCam.pin_href = HREF_GPIO_NUM;
  configCam.pin_sscb_sda = SIOD_GPIO_NUM;
  configCam.pin_sscb_scl = SIOC_GPIO_NUM;
  configCam.pin_pwdn = PWDN_GPIO_NUM;
  configCam.pin_reset = RESET_GPIO_NUM;
  configCam.xclk_freq_hz = 20000000; // 20MHz
  configCam.pixel_format = PIXFORMAT_JPEG;

  configCam.frame_size = FRAMESIZE_VGA; // 640x480
  configCam.jpeg_quality = 12;
  configCam.fb_count = 1;

  if (esp_camera_init(&configCam) != ESP_OK) {
    Serial.println("Camera Init Failed! Check power and camera ribbon cable.");
    return;
  }
  Serial.println("Camera Init OK.");

  // 3. Firebase Init
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // --- BOOT TEST TRIGGER ---
  Serial.println("\n==================================");
  Serial.println("STARTING INITIAL BOOT TEST IN 5s...");
  Serial.println("==================================");
  delay(5000); // Give you time to open the serial monitor
  executePhotoRoutine();
}

unsigned long lastTestTime = 0;
const unsigned long TEST_INTERVAL = 60000; // 60 Seconds

void loop() {
  // --- TIMER TEST TRIGGER ---
  // Takes a photo every 60 seconds automatically
  if (millis() - lastTestTime >= TEST_INTERVAL) {
    lastTestTime = millis();
    Serial.println("\n>> TIMER TRIGGER! Snapping photo...");
    executePhotoRoutine();
  }
}

void executePhotoRoutine() {
  String imgLink = uploadToGitHub();
  if (imgLink != "") {
    saveLinkToFirebase(imgLink);
  } else {
    Serial.println(">> Skipping Firebase save because GitHub upload failed.");
  }
}

String uploadToGitHub() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera Capture Failed");
    return "";
  }
  Serial.println("Photo captured! Encoding to Base64...");

  size_t outputLen = 0;
  size_t base64Len = ((fb->len + 2) / 3) * 4;

  unsigned char* base64Buf = (unsigned char*)ps_malloc(base64Len + 1);
  if (!base64Buf) {
    base64Buf = (unsigned char*)malloc(base64Len + 1);
  }

  if (!base64Buf) {
    Serial.println("Memory Alloc Failed for Base64");
    esp_camera_fb_return(fb);
    return "";
  }

  mbedtls_base64_encode(base64Buf, base64Len + 1, &outputLen, fb->buf, fb->len);
  base64Buf[outputLen] = 0;

  esp_camera_fb_return(fb);

  WiFiClientSecure client;
  client.setInsecure();

  if (client.connect(githubHost, 443)) {
    Serial.println("Connected to GitHub... Uploading...");

    // --- NEW: FOLDER STRUCTURE MODIFICATIONS ---
    String folderName = "plant_pictures";
    String fileName = "plant_" + String(millis()) + ".jpg";
    
    // Updated GitHub API Path to include the folder
    String path = "/repos/" + String(GITHUB_USER) + "/" + String(GITHUB_REPO) + "/contents/" + folderName + "/" + fileName;

    String jsonHead = "{\"message\":\"Daily Plant Photo\",\"content\":\"";
    String jsonTail = "\"}";
    size_t jsonLen = jsonHead.length() + outputLen + jsonTail.length();

    client.println("PUT " + path + " HTTP/1.1");
    client.println("Host: " + String(githubHost));
    client.println("Authorization: token " + String(GITHUB_TOKEN));
    client.println("User-Agent: ESP32-CAM");
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(jsonLen));
    client.println();

    client.print(jsonHead);

    int chunkSize = 1024;
    for (size_t i = 0; i < outputLen; i += chunkSize) {
      if (i + chunkSize < outputLen) {
        client.write(base64Buf + i, chunkSize);
      } else {
        client.write(base64Buf + i, outputLen - i);
      }
    }

    client.print(jsonTail);

    Serial.println("Data sent! Waiting for GitHub response...");

    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }
    String response = client.readString();

    if (psramFound()) free(base64Buf); else free(base64Buf);

    if (response.indexOf("\"content\"") > 0) {
      // --- NEW: RAW LINK MODIFIED TO INCLUDE FOLDER ---
      String rawLink = "https://raw.githubusercontent.com/" + String(GITHUB_USER) + "/" + String(GITHUB_REPO) + "/" + String(GITHUB_BRANCH) + "/" + folderName + "/" + fileName;
      
      Serial.println(">> GitHub Upload Success!");
      Serial.println(">> Link: " + rawLink);
      return rawLink;
    } else {
      Serial.println("GitHub Upload Failed. API Response:");
      Serial.println(response);
      return "";
    }
  } else {
    Serial.println("Connection to GitHub failed (SSL/TLS Issue)");
  }

  if (psramFound()) free(base64Buf); else free(base64Buf);
  return "";
}

void saveLinkToFirebase(String url) {
  if (!Firebase.ready()) {
    Serial.println("Firebase is not ready yet!");
    return;
  }

  FirebaseJson json;
  json.set("timestamp/.sv", "timestamp");
  json.set("url", url);

  if (Firebase.RTDB.pushJSON(&fbdo, "/gallery", &json)) {
    Serial.println(">> Link saved to Firebase successfully!");
  } else {
    Serial.println(">> Firebase Error: " + fbdo.errorReason());
  }
}
