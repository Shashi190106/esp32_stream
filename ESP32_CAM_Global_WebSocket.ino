#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "esp_camera.h"

// =====================================================
// Wi-Fi
// =====================================================
const char* WIFI_SSID = "Shashi";
const char* WIFI_PASSWORD = "0000000000";

// =====================================================
// Render WebSocket server
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

// 70 ms targets about 14 FPS. If unstable, use 100 ms (~10 FPS).
const uint32_t FRAME_INTERVAL_MS = 70;

// =====================================================
// Wi-Fi
// =====================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi IP: ");
  Serial.println(WiFi.localIP());
}

// =====================================================
// Camera
// =====================================================
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

  // 640x480: good balance of quality and FPS.
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.print("Camera init failed: 0x");
    Serial.println(err, HEX);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_framesize(sensor, FRAMESIZE_VGA);
    sensor->set_quality(sensor, 12);
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);
    sensor->set_vflip(sensor, 0);
    sensor->set_hmirror(sensor, 0);
  }

  Serial.println("Camera ready: 640x480 JPEG");
  return true;
}

// =====================================================
// Flash command
// =====================================================
void setFlash(bool on) {
  flashState = on;
  digitalWrite(FLASH_LED_PIN, on ? HIGH : LOW);
  Serial.println(on ? "FLASH ON" : "FLASH OFF");
}

// =====================================================
// WebSocket event handler
// =====================================================
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      socketConnected = false;
      Serial.println("WebSocket disconnected");
      break;

    case WStype_CONNECTED:
      socketConnected = true;
      Serial.println("WebSocket connected to Render");
      // Tell the server that this connection is the camera.
      webSocket.sendTXT("camera");
      break;

    case WStype_TEXT:
      if (length > 0) {
        String command;
        command.reserve(length);
        for (size_t i = 0; i < length; i++) command += (char)payload[i];

        if (command == "FLASH_ON") {
          setFlash(true);
        } else if (command == "FLASH_OFF") {
          setFlash(false);
        }
      }
      break;

    default:
      break;
  }
}

// =====================================================
// Send one JPEG frame over persistent WebSocket
// =====================================================
void sendFrame() {
  if (!socketConnected) return;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  bool sent = webSocket.sendBIN(fb->buf, fb->len);
  size_t frameSize = fb->len;

  esp_camera_fb_return(fb);

  if (sent) {
    frameCounter++;
  } else {
    Serial.println("WebSocket frame send failed");
  }

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

// =====================================================
// Setup
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32-CAM GLOBAL WEBSOCKET STREAM");
  Serial.println("========================================");

  pinMode(FLASH_LED_PIN, OUTPUT);
  setFlash(false);

  if (!psramFound()) {
    Serial.println("WARNING: PSRAM not detected");
  } else {
    Serial.println("PSRAM detected");
  }

  if (!initCamera()) {
    Serial.println("Camera initialization failed.");
    while (true) delay(1000);
  }

  connectWiFi();

  // HTTPS WebSocket (WSS). Render uses TLS on port 443.
  webSocket.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  webSocket.setInsecure();
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);

  fpsTimer = millis();
  cameraReady = true;

  Serial.println("Connecting to global WebSocket...");
}

// =====================================================
// Main loop
// =====================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    socketConnected = false;
    connectWiFi();
    delay(100);
    return;
  }

  // Must run frequently for WebSocket RX/TX and heartbeat.
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
