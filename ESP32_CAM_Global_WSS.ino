#define DEBUG_WEBSOCKETS_PORT Serial
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "Shashi";
const char* WIFI_PASSWORD = "0000000000";

// =====================================================
// RENDER WSS SERVER
// =====================================================

const char* SERVER_HOST = "esp32cam-global-ws.onrender.com";
const uint16_t SERVER_PORT = 443;
const char* SERVER_PATH = "/ws";

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

// =====================================================
// FLASH
// =====================================================

#define FLASH_LED_PIN 4

// =====================================================
// CAMERA SETTINGS
// =====================================================

#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define JPEG_QUALITY 12

// Approx. 14 FPS target. Actual FPS depends on network.
const unsigned long FRAME_INTERVAL = 70;

// =====================================================
// WEBSOCKET CLIENT
// =====================================================

WebSocketsClient webSocket;

volatile bool websocketConnected = false;

unsigned long lastFrameTime = 0;
unsigned long lastReconnectMessage = 0;
unsigned long fpsTimer = 0;

uint32_t frameCount = 0;
uint32_t droppedFrames = 0;

// =====================================================
// WEBSOCKET EVENT
// =====================================================

void webSocketEvent(
  WStype_t type,
  uint8_t* payload,
  size_t length
) {

  switch (type) {

    case WStype_DISCONNECTED:
    {
      websocketConnected = false;

      Serial.println();
      Serial.println(">>> WSS DISCONNECTED");

      break;
    }

    case WStype_CONNECTED:
    {
      websocketConnected = true;

      Serial.println();
      Serial.println("========================================");
      Serial.println(">>> WSS CONNECTED TO RENDER");
      Serial.print(">>> Server: wss://");
      Serial.print(SERVER_HOST);
      Serial.println(SERVER_PATH);
      Serial.println("========================================");

      break;
    }

    case WStype_TEXT:
    {
      if (payload == nullptr) {
        break;
      }

      String command = String((char*)payload);

      Serial.print(">>> Render command: ");
      Serial.println(command);

      // -----------------------------------------------
      // FLASH ON
      // -----------------------------------------------

      if (command == "FLASH_ON") {

        digitalWrite(
          FLASH_LED_PIN,
          HIGH
        );

        Serial.println(
          ">>> FLASH ON"
        );
      }

      // -----------------------------------------------
      // FLASH OFF
      // -----------------------------------------------

      else if (command == "FLASH_OFF") {

        digitalWrite(
          FLASH_LED_PIN,
          LOW
        );

        Serial.println(
          ">>> FLASH OFF"
        );
      }

      // -----------------------------------------------
      // CAPTURE NOW
      // -----------------------------------------------

      else if (command == "CAPTURE_NOW") {

        Serial.println(
          ">>> CAPTURE_NOW received"
        );

        capturePhotoForCloud();
      }

      break;
    }

    case WStype_ERROR:
    {
      websocketConnected = false;

      Serial.println();
      Serial.println(">>> WSS ERROR");

      if (payload != nullptr && length > 0) {

        Serial.print(
          ">>> Error payload: "
        );

        Serial.write(
          payload,
          length
        );

        Serial.println();
      }

      break;
    }

    default:
      break;
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

  } else {

    Serial.println(
      "WARNING: PSRAM not detected"
    );

    config.frame_size =
      FRAMESIZE_QVGA;

    config.jpeg_quality = 15;

    config.fb_count = 1;

    config.fb_location =
      CAMERA_FB_IN_DRAM;

    config.grab_mode =
      CAMERA_GRAB_WHEN_EMPTY;
  }

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

  Serial.println();
  Serial.println(
    "Connecting to Wi-Fi..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setSleep(
    false
  );

  WiFi.disconnect(
    true
  );

  delay(300);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");

    attempts++;

    if (attempts >= 40) {

      Serial.println();
      Serial.println(
        "Wi-Fi timeout - restarting"
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

  Serial.print(
    "DNS test host: "
  );

  Serial.println(
    SERVER_HOST
  );

  IPAddress resolvedIP;

  if (
    WiFi.hostByName(
      SERVER_HOST,
      resolvedIP
    )
  ) {

    Serial.print(
      "Render DNS IP: "
    );

    Serial.println(
      resolvedIP
    );

  } else {

    Serial.println(
      "Render DNS lookup failed"
    );
  }
}

// =====================================================
// START RENDER WSS
// =====================================================

void startRenderWebSocket() {

  websocketConnected = false;

  Serial.println();
  Serial.println(
    "Starting Render WSS client..."
  );

  Serial.print(
    "WSS URL: wss://"
  );

  Serial.print(
    SERVER_HOST
  );

  Serial.println(
    SERVER_PATH
  );

  // The WebSockets library uses WiFiClientSecure
  // and, when no CA/fingerprint is supplied on ESP32,
  // it uses an insecure TLS mode.
  // Remove the library's default "Origin: file://" header.
  // Some WebSocket servers/proxies reject that Origin.
  webSocket.setExtraHeaders();

  webSocket.beginSSL(
    SERVER_HOST,
    SERVER_PORT,
    SERVER_PATH
  );

  webSocket.onEvent(
    webSocketEvent
  );

  webSocket.setReconnectInterval(
    5000
  );

  webSocket.enableHeartbeat(
    15000,
    5000,
    2
  );

  Serial.println(
    "WSS client started"
  );
}

// =====================================================
// CAPTURE STILL PHOTO AND SEND IT TO RENDER
// =====================================================

void capturePhotoForCloud() {

  if (!websocketConnected) {

    Serial.println(
      "Cannot capture photo - WSS not connected"
    );

    return;
  }

  Serial.println();
  Serial.println(
    ">>> Capturing still photo..."
  );

  camera_fb_t* fb =
    esp_camera_fb_get();

  if (fb == nullptr) {

    Serial.println(
      ">>> Camera capture failed"
    );

    return;
  }

  Serial.print(
    ">>> Photo size: "
  );

  Serial.print(
    fb->len
  );

  Serial.println(
    " bytes"
  );

  // Tell Render that the NEXT binary message
  // is a still photo for the "Last Captured Photo".
  bool tagOK =
    webSocket.sendTXT(
      "PHOTO"
    );

  if (!tagOK) {

    Serial.println(
      ">>> PHOTO tag send failed"
    );

    esp_camera_fb_return(
      fb
    );

    return;
  }

  delay(10);

  bool photoOK =
    webSocket.sendBIN(
      fb->buf,
      fb->len
    );

  esp_camera_fb_return(
    fb
  );

  if (photoOK) {

    Serial.println(
      ">>> Last photo sent to Render"
    );

  } else {

    Serial.println(
      ">>> Last photo send FAILED"
    );

    websocketConnected = false;
  }
}

// =====================================================
// SEND LIVE CAMERA FRAME
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
    webSocket.sendBIN(
      fb->buf,
      fb->len
    );

  esp_camera_fb_return(
    fb
  );

  if (result) {

    frameCount++;

  } else {

    droppedFrames++;

    websocketConnected = false;

    Serial.println(
      "Live frame send failed"
    );
  }
}

// =====================================================
// PRINT FPS
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

  // Render WSS
  startRenderWebSocket();

  fpsTimer =
    millis();

  lastReconnectMessage =
    millis();

  Serial.println();
  Serial.println(
    "System ready"
  );
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // Let the WebSocketsClient handle connection,
  // reconnect and incoming messages.
  webSocket.loop();

  // Wi-Fi recovery
  if (
    WiFi.status() != WL_CONNECTED
  ) {

    websocketConnected = false;

    Serial.println(
      "Wi-Fi lost - reconnecting..."
    );

    connectWiFi();

    startRenderWebSocket();

    delay(100);

    return;
  }

  unsigned long now =
    millis();

  // Periodic connection status
  if (
    !websocketConnected &&
    now - lastReconnectMessage >= 5000
  ) {

    lastReconnectMessage =
      now;

    Serial.println(
      "Waiting for Render WSS connection..."
    );
  }

  // Live stream
  if (
    websocketConnected &&
    now - lastFrameTime >= FRAME_INTERVAL
  ) {

    lastFrameTime =
      now;

    sendLiveFrame();
  }

  // FPS
  printFPS();

  delay(1);
}
