#include "esp_camera.h"
#include <WiFi.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// =====================================================
// WIFI
// =====================================================

const char* ssid = "Shashi";
const char* password = "0000000000";

// =====================================================
// RENDER SERVER
// =====================================================

const char* WS_URL =
  "wss://esp32cam-global-ws.onrender.com/ws";

// =====================================================
// AI-THINKER ESP32-CAM PINS
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

// =====================================================
// CAMERA SETTINGS
// =====================================================

#define CAMERA_FRAME_SIZE  FRAMESIZE_VGA
#define JPEG_QUALITY       12

// About 14 FPS target.
// Actual FPS depends on Wi-Fi and Render connection.
const unsigned long FRAME_INTERVAL = 70;

// =====================================================
// WEBSOCKET
// =====================================================

WebsocketsClient client;

bool websocketConnected = false;

unsigned long lastFrameTime = 0;
unsigned long lastReconnectTime = 0;
unsigned long fpsTimer = 0;

uint32_t frameCount = 0;
uint32_t droppedFrames = 0;

// =====================================================
// WEBSOCKET MESSAGE CALLBACK
// =====================================================

void onMessageCallback(WebsocketsMessage message) {

  if (!message.isText()) {
    return;
  }

  String command = message.data();

  Serial.print("Command from Render: ");
  Serial.println(command);

  // ---------------------------------------------------
  // FLASH ON
  // ---------------------------------------------------

  if (command == "FLASH_ON") {

    digitalWrite(
      FLASH_LED_PIN,
      HIGH
    );

    Serial.println("FLASH ON");
  }

  // ---------------------------------------------------
  // FLASH OFF
  // ---------------------------------------------------

  else if (command == "FLASH_OFF") {

    digitalWrite(
      FLASH_LED_PIN,
      LOW
    );

    Serial.println("FLASH OFF");
  }

  // ---------------------------------------------------
  // CAPTURE PHOTO
  // ---------------------------------------------------

  else if (command == "CAPTURE_NOW") {

    Serial.println(
      "CAPTURE_NOW received"
    );

    capturePhotoForCloud();
  }
}

// =====================================================
// WEBSOCKET EVENT CALLBACK
// =====================================================

void onEventsCallback(
  WebsocketsEvent event,
  String data
) {

  if (
    event == WebsocketsEvent::ConnectionOpened
  ) {

    websocketConnected = true;

    Serial.println();
    Serial.println(
      "======================================"
    );

    Serial.println(
      " WEBSOCKET CONNECTED"
    );

    Serial.println(
      " RENDER CONNECTION SUCCESS"
    );

    Serial.println(
      "======================================"
    );

  }

  else if (
    event == WebsocketsEvent::ConnectionClosed
  ) {

    websocketConnected = false;

    Serial.println(
      "WEBSOCKET DISCONNECTED"
    );

  }

  else if (
    event == WebsocketsEvent::GotPing
  ) {

    Serial.println(
      "WebSocket Ping received"
    );

  }

  else if (
    event == WebsocketsEvent::GotPong
  ) {

    Serial.println(
      "WebSocket Pong received"
    );
  }
}

// =====================================================
// CAMERA INITIALIZATION
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

  // ===================================================
  // PSRAM
  // ===================================================

  if (psramFound()) {

    Serial.println(
      "PSRAM detected"
    );

    config.frame_size =
      CAMERA_FRAME_SIZE;

    config.jpeg_quality =
      JPEG_QUALITY;

    config.fb_count = 2;

    config.fb_location =
      CAMERA_FB_IN_PSRAM;

    config.grab_mode =
      CAMERA_GRAB_LATEST;

  }

  else {

    Serial.println(
      "WARNING: PSRAM not detected"
    );

    config.frame_size =
      FRAMESIZE_QVGA;

    config.jpeg_quality =
      15;

    config.fb_count = 1;

    config.fb_location =
      CAMERA_FB_IN_DRAM;

    config.grab_mode =
      CAMERA_GRAB_WHEN_EMPTY;
  }

  // ===================================================
  // START CAMERA
  // ===================================================

  esp_err_t err =
    esp_camera_init(&config);

  if (err != ESP_OK) {

    Serial.print(
      "Camera init failed: 0x"
    );

    Serial.println(
      err,
      HEX
    );

    return false;
  }

  sensor_t* sensor =
    esp_camera_sensor_get();

  if (sensor != nullptr) {

    sensor->set_framesize(
      sensor,
      CAMERA_FRAME_SIZE
    );

    sensor->set_quality(
      sensor,
      JPEG_QUALITY
    );

    sensor->set_brightness(
      sensor,
      0
    );

    sensor->set_contrast(
      sensor,
      0
    );

    sensor->set_saturation(
      sensor,
      0
    );

    sensor->set_sharpness(
      sensor,
      1
    );

    sensor->set_vflip(
      sensor,
      0
    );

    sensor->set_hmirror(
      sensor,
      0
    );
  }

  Serial.println(
    "Camera initialized successfully"
  );

  Serial.println(
    "Resolution: 640x480"
  );

  return true;
}

// =====================================================
// WIFI CONNECTION
// =====================================================

void connectWiFi() {

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setSleep(false);

  WiFi.begin(
    ssid,
    password
  );

  Serial.print(
    "Connecting to Wi-Fi"
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");

    attempts++;

    if (
      attempts >= 40
    ) {

      Serial.println();

      Serial.println(
        "Wi-Fi timeout"
      );

      ESP.restart();
    }
  }

  Serial.println();

  Serial.println(
    "Wi-Fi connected"
  );

  Serial.print(
    "ESP32-CAM IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.print(
    "RSSI: "
  );

  Serial.print(
    WiFi.RSSI()
  );

  Serial.println(
    " dBm"
  );
}

// =====================================================
// CONNECT TO RENDER
// =====================================================

bool connectToRender() {

  Serial.println();
  Serial.println(
    "======================================"
  );

  Serial.println(
    "Connecting to Render..."
  );

  Serial.println(
    WS_URL
  );

  Serial.println(
    "======================================"
  );

  // Render provides HTTPS/WSS certificate.
  // This disables local certificate validation.
  client.setInsecure();

  // Register callbacks
  client.onMessage(
    onMessageCallback
  );

  client.onEvent(
    onEventsCallback
  );

  // Connect
  bool connected =
    client.connect(
      WS_URL
    );

  if (connected) {

    websocketConnected = true;

    Serial.println(
      "Render WebSocket connection OK"
    );

    return true;
  }

  websocketConnected = false;

  Serial.println(
    "Render WebSocket connection FAILED"
  );

  return false;
}

// =====================================================
// CAPTURE STILL PHOTO
// =====================================================

void capturePhotoForCloud() {

  if (!websocketConnected) {

    Serial.println(
      "Cannot capture photo - WebSocket not connected"
    );

    return;
  }

  Serial.println();
  Serial.println(
    "Capturing photo..."
  );

  camera_fb_t* fb =
    esp_camera_fb_get();

  if (fb == nullptr) {

    Serial.println(
      "Camera capture failed"
    );

    return;
  }

  Serial.print(
    "Photo size: "
  );

  Serial.print(
    fb->len
  );

  Serial.println(
    " bytes"
  );

  // Tell Render that the next binary
  // message is the last photo.
  bool tagOK =
    client.send(
      "PHOTO"
    );

  if (!tagOK) {

    Serial.println(
      "Failed to send PHOTO tag"
    );

    esp_camera_fb_return(
      fb
    );

    websocketConnected = false;

    return;
  }

  delay(5);

  // Send JPEG
  bool photoOK =
    client.sendBinary(
      (const char*)fb->buf,
      fb->len
    );

  esp_camera_fb_return(
    fb
  );

  if (photoOK) {

    Serial.println(
      "Last photo sent to Render"
    );

  }

  else {

    Serial.println(
      "Photo send failed"
    );

    websocketConnected = false;
  }
}

// =====================================================
// SEND LIVE FRAME
// =====================================================

void sendLiveFrame() {

  if (!websocketConnected) {
    return;
  }

  camera_fb_t* fb =
    esp_camera_fb_get();

  if (fb == nullptr) {

    droppedFrames++;

    return;
  }

  bool result =
    client.sendBinary(
      (const char*)fb->buf,
      fb->len
    );

  esp_camera_fb_return(
    fb
  );

  if (result) {

    frameCount++;
  }

  else {

    droppedFrames++;
    websocketConnected = false;
  }
}

// =====================================================
// FPS DISPLAY
// =====================================================

void printFPS() {

  unsigned long now =
    millis();

  if (
    now - fpsTimer >= 2000
  ) {

    float fps =
      frameCount *
      1000.0f /
      (now - fpsTimer);

    Serial.print(
      "FPS: "
    );

    Serial.print(
      fps,
      1
    );

    Serial.print(
      " | Sent: "
    );

    Serial.print(
      frameCount
    );

    Serial.print(
      " | Dropped: "
    );

    Serial.print(
      droppedFrames
    );

    Serial.print(
      " | Free Heap: "
    );

    Serial.print(
      ESP.getFreeHeap()
    );

    Serial.print(
      " | Free PSRAM: "
    );

    Serial.println(
      ESP.getFreePsram()
    );

    frameCount = 0;
    droppedFrames = 0;

    fpsTimer = now;
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(1000);

  Serial.println();
  Serial.println(
    "=========================================="
  );

  Serial.println(
    " ESP32-CAM SMART HOME GLOBAL CAMERA"
  );

  Serial.println(
    "=========================================="
  );

  // Flash
  pinMode(
    FLASH_LED_PIN,
    OUTPUT
  );

  digitalWrite(
    FLASH_LED_PIN,
    LOW
  );

  // Camera
  if (!initCamera()) {

    Serial.println(
      "Camera initialization failed"
    );

    while (true) {

      delay(1000);
    }
  }

  // Wi-Fi
  connectWiFi();

  // WebSocket
  connectToRender();

  fpsTimer =
    millis();

  lastReconnectTime =
    millis();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // ---------------------------------------------------
  // Wi-Fi check
  // ---------------------------------------------------

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    websocketConnected = false;

    connectWiFi();

    return;
  }

  // ---------------------------------------------------
  // WebSocket polling
  // ---------------------------------------------------

  if (client.available()) {

    client.poll();
  }

  else {

    websocketConnected = false;
  }

  // ---------------------------------------------------
  // Reconnect when disconnected
  // ---------------------------------------------------

  unsigned long now =
    millis();

  if (
    !websocketConnected &&
    now - lastReconnectTime >= 5000
  ) {

    lastReconnectTime =
      now;

    connectToRender();
  }

  // ---------------------------------------------------
  // Live camera frame
  // ---------------------------------------------------

  if (
    websocketConnected &&
    now - lastFrameTime >= FRAME_INTERVAL
  ) {

    lastFrameTime =
      now;

    sendLiveFrame();
  }

  // ---------------------------------------------------
  // FPS
  // ---------------------------------------------------

  printFPS();
}
