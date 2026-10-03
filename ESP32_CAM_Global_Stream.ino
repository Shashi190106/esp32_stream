#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ==============================
// Wi-Fi
// ==============================
const char* ssid = "Shashi";
const char* password = "0000000000";

// Your Render service
const char* CLOUD_URL = "https://esp32cam-global-stream.onrender.com";

// ==============================
// AI-Thinker ESP32-CAM pins
// ==============================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_PIN      4

unsigned long lastCommandCheck = 0;
const unsigned long COMMAND_INTERVAL = 1000;

bool sendFrame(camera_fb_t* fb) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(CLOUD_URL) + "/frame";
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(5000);

  int code = http.POST(fb->buf, fb->len);
  http.end();

  return code >= 200 && code < 300;
}

void checkFlashCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(CLOUD_URL) + "/command";
  http.begin(url);
  http.setTimeout(3000);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String command = http.getString();
    command.trim();

    if (command == "on") {
      digitalWrite(FLASH_LED_PIN, HIGH);
    } else if (command == "off") {
      digitalWrite(FLASH_LED_PIN, LOW);
    }
  }

  http.end();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("WiFi connected");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;      // 640x480
  config.jpeg_quality = 12;               // lower = better quality/larger frame
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_framesize(sensor, FRAMESIZE_VGA);
    sensor->set_quality(sensor, 12);
  }

  Serial.println("Camera ready");

  connectWiFi();

  Serial.println("Global stream uploader started");
  Serial.print("Open globally: ");
  Serial.println(CLOUD_URL);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect();
    connectWiFi();
    return;
  }

  if (millis() - lastCommandCheck >= COMMAND_INTERVAL) {
    lastCommandCheck = millis();
    checkFlashCommand();
  }

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    delay(50);
    return;
  }

  bool sent = sendFrame(fb);
  esp_camera_fb_return(fb);

  if (!sent) {
    Serial.println("Frame upload failed");
  }
}
