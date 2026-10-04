#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>

// =====================================================
// WIFI / CLOUD
// =====================================================
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* SERVER_HOST = "esp32cam-global-ws.onrender.com";
const uint16_t SERVER_PORT = 443;
const char* SERVER_PATH = "/ws";

// =====================================================
// AI-THINKER ESP32-CAM
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

#define FLASH_LED_PIN      4

WebSocketsClient webSocket;

bool websocketConnected = false;
bool flashState = false;

unsigned long lastFrameTime = 0;
unsigned long fpsTimer = 0;

uint32_t frameCount = 0;
uint32_t droppedFrames = 0;

#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define JPEG_QUALITY 12

// Live stream target. Actual FPS depends on Wi-Fi/TLS/server.
const unsigned long FRAME_INTERVAL = 70;

void sendStillPhoto() {
  if (!websocketConnected) return;

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Still capture failed");
    return;
  }

  Serial.print("Sending still photo: ");
  Serial.print(fb->len);
  Serial.println(" bytes");

  // Tell Render that the next binary WebSocket message is the
  // last captured photo, not a live frame.
  webSocket.sendTXT("PHOTO");
  bool ok = webSocket.sendBIN(fb->buf, fb->len);

  esp_camera_fb_return(fb);

  if (ok) {
    Serial.println("Still photo sent");
  } else {
    Serial.println("Still photo send failed");
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      websocketConnected = true;
      Serial.println("WebSocket CONNECTED");
      break;

    case WStype_DISCONNECTED:
      websocketConnected = false;
      Serial.println("WebSocket DISCONNECTED");
      break;

    case WStype_TEXT:
      if (!payload) return;

      if (strcmp((char*)payload, "FLASH_ON") == 0) {
        digitalWrite(FLASH_LED_PIN, HIGH);
        flashState = true;
        Serial.println("FLASH ON");
      }
      else if (strcmp((char*)payload, "FLASH_OFF") == 0) {
        digitalWrite(FLASH_LED_PIN, LOW);
        flashState = false;
        Serial.println("FLASH OFF");
      }
      else if (strcmp((char*)payload, "CAPTURE_NOW") == 0) {
        Serial.println("CAPTURE_NOW received");
        sendStillPhoto();
      }
      break;

    default:
      break;
  }
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

  if (!psramFound()) {
    Serial.println("PSRAM NOT FOUND - using QVGA");
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  } else {
    Serial.println("PSRAM OK");
    config.frame_size = CAMERA_FRAME_SIZE;
    config.jpeg_quality = JPEG_QUALITY;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.print("Camera init failed: 0x");
    Serial.println(err, HEX);
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();

  if (sensor) {
    sensor->set_framesize(sensor, CAMERA_FRAME_SIZE);
    sensor->set_quality(sensor, JPEG_QUALITY);
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);
    sensor->set_sharpness(sensor, 1);
    sensor->set_vflip(sensor, 0);
    sensor->set_hmirror(sensor, 0);
  }

  Serial.println("Camera initialized");
  return true;
}

void connectWiFi() {
  Serial.println("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");

    if (++attempts > 60) {
      Serial.println("\nWiFi timeout - restarting");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void sendFrame() {
  if (!websocketConnected) return;

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    droppedFrames++;
    Serial.println("Camera capture failed");
    return;
  }

  bool ok = webSocket.sendBIN(fb->buf, fb->len);

  esp_camera_fb_return(fb);

  if (ok) frameCount++;
  else droppedFrames++;
}

void printFPS() {
  unsigned long now = millis();

  if (now - fpsTimer >= 2000) {
    float fps = frameCount * 1000.0f / (now - fpsTimer);

    Serial.print("FPS: ");
    Serial.print(fps, 1);
    Serial.print(" | Sent: ");
    Serial.print(frameCount);
    Serial.print(" | Dropped: ");
    Serial.print(droppedFrames);
    Serial.print(" | Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" | PSRAM: ");
    Serial.println(ESP.getFreePsram());

    frameCount = 0;
    droppedFrames = 0;
    fpsTimer = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32-CAM SMART HOME GLOBAL CAMERA");
  Serial.println("========================================");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    while (true) delay(1000);
  }

  connectWiFi();

  webSocket.beginSSL(SERVER_HOST, SERVER_PORT, SERVER_PATH);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(10000, 3000, 2);

  fpsTimer = millis();

  Serial.print("WSS: wss://");
  Serial.print(SERVER_HOST);
  Serial.println(SERVER_PATH);
}

void loop() {
  webSocket.loop();

  if (WiFi.status() != WL_CONNECTED) {
    websocketConnected = false;
    connectWiFi();
    return;
  }

  unsigned long now = millis();

  if (websocketConnected && now - lastFrameTime >= FRAME_INTERVAL) {
    lastFrameTime = now;
    sendFrame();
  }

  printFPS();
}
