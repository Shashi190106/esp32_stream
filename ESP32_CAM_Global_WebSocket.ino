#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "esp_camera.h"

// =====================================================
// Wi-Fi
// =====================================================
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =====================================================
// Render WebSocket server
// Must match render.yaml service: esp32cam-global-stream
// =====================================================
const char* WS_HOST = "esp32cam-global-stream.onrender.com";
const uint16_t WS_PORT = 443;
const char* WS_PATH = "/ws";

// =====================================================
// AI-Thinker ESP32-CAM pins
// =====================================================
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

#define FLASH_LED_PIN 4

WebSocketsClient webSocket;
bool socketConnected = false;
bool cameraReady = false;
bool flashState = false;

unsigned long lastFrame = 0;
unsigned long frameCounter = 0;
unsigned long fpsTimer = 0;
const uint32_t FRAME_INTERVAL_MS = 100;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (++attempts > 40) {
      Serial.println("\nWiFi retry");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      attempts = 0;
    }
  }

  Serial.println();
  Serial.print("WiFi IP: ");
  Serial.println(WiFi.localIP());
}

bool initCamera() {
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

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.print("Camera init failed: 0x");
    Serial.println(err, HEX);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_framesize(sensor, psramFound() ? FRAMESIZE_VGA : FRAMESIZE_QVGA);
    sensor->set_quality(sensor, 12);
  }

  Serial.println("Camera ready");
  return true;
}

void setFlash(bool on) {
  flashState = on;
  digitalWrite(FLASH_LED_PIN, on ? HIGH : LOW);
  Serial.println(on ? "FLASH ON" : "FLASH OFF");
}

// Capture one JPEG and send it as a PHOTO message followed by binary data.
void capturePhoto() {
  if (!socketConnected) {
    Serial.println("Cannot capture: WebSocket not connected");
    return;
  }

  // Turn flash on only for the photo if it is enabled by dashboard.
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Photo capture failed");
    return;
  }

  webSocket.sendTXT("PHOTO");
  bool sent = webSocket.sendBIN(fb->buf, fb->len);

  Serial.print("Photo: ");
  Serial.print(fb->len / 1024.0f, 1);
  Serial.print(" KB, sent=");
  Serial.println(sent ? "YES" : "NO");

  esp_camera_fb_return(fb);
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      socketConnected = false;
      Serial.println("WebSocket disconnected");
      break;

    case WStype_CONNECTED:
      socketConnected = true;
      Serial.println("WebSocket connected to Render");
      webSocket.sendTXT("camera");
      break;

    case WStype_TEXT: {
      String command;
      command.reserve(length);
      for (size_t i = 0; i < length; i++) command += (char)payload[i];

      Serial.print("Cloud command: ");
      Serial.println(command);

      if (command == "FLASH_ON") {
        setFlash(true);
      } else if (command == "FLASH_OFF") {
        setFlash(false);
      } else if (command == "CAPTURE_NOW") {
        capturePhoto();
      }
      break;
    }

    default:
      break;
  }
}

void sendFrame() {
  if (!socketConnected) return;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera frame failed");
    return;
  }

  bool sent = webSocket.sendBIN(fb->buf, fb->len);
  size_t frameSize = fb->len;
  esp_camera_fb_return(fb);

  if (sent) frameCounter++;

  unsigned long now = millis();
  if (now - fpsTimer >= 2000) {
    float fps = (frameCounter * 1000.0f) / (now - fpsTimer);
    Serial.print("Upload FPS: ");
    Serial.print(fps, 1);
    Serial.print(" | JPEG: ");
    Serial.print(frameSize / 1024.0f, 1);
    Serial.println(" KB");
    frameCounter = 0;
    fpsTimer = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32-CAM GLOBAL WEBSOCKET");
  Serial.println("========================================");

  pinMode(FLASH_LED_PIN, OUTPUT);
  setFlash(false);

  if (psramFound()) Serial.println("PSRAM detected");
  else Serial.println("WARNING: PSRAM not detected");

  if (!initCamera()) {
    Serial.println("Camera initialization failed");
    while (true) delay(1000);
  }

  connectWiFi();

  webSocket.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  webSocket.setInsecure();
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);

  fpsTimer = millis();
  cameraReady = true;
  Serial.println("Connecting to global WebSocket...");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    socketConnected = false;
    connectWiFi();
    delay(100);
    return;
  }

  webSocket.loop();

  if (cameraReady && socketConnected) {
    unsigned long now = millis();
    if (now - lastFrame >= FRAME_INTERVAL_MS) {
      lastFrame = now;
      sendFrame();
    }
  }

  delay(1);
}
