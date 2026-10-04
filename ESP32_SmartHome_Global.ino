#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>

// =====================================================
// WIFI
// =====================================================
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Global Render web service
const char* CLOUD_HOST = "esp32cam-global-ws.onrender.com";
const uint16_t CLOUD_PORT = 443;

// =====================================================
// PINS
// =====================================================
#define PIR_PIN       27
#define FLAME_PIN     26
#define SMOKE_PIN     34
#define DHT_PIN        4
#define DHT_TYPE       DHT11
#define BUZZER_PIN    25
#define GREEN_LED      14
#define RED_LED        12

// Tune this after observing your MQ-2 readings
const int SMOKE_THRESHOLD = 1800;

// PIR capture cooldown
const unsigned long PIR_COOLDOWN = 5000;

// Sensor upload interval
const unsigned long SENSOR_INTERVAL = 2000;

DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastSensorUpload = 0;
unsigned long lastPirTrigger = 0;
bool previousPir = false;

bool motionDetected = false;
bool flameDetected = false;
bool smokeDetected = false;

int smokeValue = 0;
float humidity = 0.0;
float temperature = 0.0;

String lastEvent = "System Started";

bool postJSON(const String& path, const String& body) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = String("https://") + CLOUD_HOST + path;

  if (!http.begin(client, url)) {
    Serial.println("HTTPS begin failed");
    return false;
  }

  http.setTimeout(4000);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);

  Serial.print("POST ");
  Serial.print(path);
  Serial.print(" -> ");
  Serial.println(code);

  http.end();
  return code > 0 && code < 400;
}

bool postTrigger() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = String("https://") + CLOUD_HOST + "/trigger";

  if (!http.begin(client, url)) {
    return false;
  }

  http.setTimeout(4000);
  int code = http.POST("");
  Serial.print("POST /trigger -> ");
  Serial.println(code);

  http.end();
  return code > 0 && code < 400;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (++attempts > 40) {
      Serial.println("\nWiFi retry");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      attempts = 0;
    }
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

String makeJSON() {
  String json = "{";
  json += "\"pir\":" + String(motionDetected ? "true" : "false");
  json += ",\"flame\":" + String(flameDetected ? "true" : "false");
  json += ",\"smoke\":" + String(smokeDetected ? "true" : "false");
  json += ",\"smokeValue\":" + String(smokeValue);
  json += ",\"humidity\":" + String(humidity, 1);
  json += ",\"temperature\":" + String(temperature, 1);
  json += ",\"event\":\"" + lastEvent + "\"";
  json += ",\"device_ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  return json;
}

void readSensors() {
  motionDetected = digitalRead(PIR_PIN) == HIGH;

  // Most common flame sensor modules are active LOW.
  flameDetected = digitalRead(FLAME_PIN) == LOW;

  smokeValue = analogRead(SMOKE_PIN);
  smokeDetected = smokeValue > SMOKE_THRESHOLD;

  static unsigned long lastDHT = 0;
  if (millis() - lastDHT >= 2000) {
    lastDHT = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h)) humidity = h;
    if (!isnan(t)) temperature = t;
  }

  if (flameDetected) {
    lastEvent = "FLAME DETECTED";
  } else if (smokeDetected) {
    lastEvent = "SMOKE DETECTED";
  } else if (motionDetected) {
    lastEvent = "Motion Detected";
  }

  if (flameDetected || smokeDetected) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }
}

void checkMotionCapture() {
  unsigned long now = millis();

  bool risingEdge = motionDetected && !previousPir;
  bool cooldownOK = (now - lastPirTrigger) >= PIR_COOLDOWN;

  if (risingEdge && cooldownOK) {
    lastPirTrigger = now;
    lastEvent = "Motion Detected - Photo Requested";

    Serial.println("PIR -> requesting cloud camera capture");
    postTrigger();
  }

  previousPir = motionDetected;
}

void uploadSensorState() {
  if (WiFi.status() != WL_CONNECTED) return;
  postJSON("/sensor", makeJSON());
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIR_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT);
  pinMode(SMOKE_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);

  dht.begin();

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  readSensors();
  checkMotionCapture();

  unsigned long now = millis();

  if (now - lastSensorUpload >= SENSOR_INTERVAL) {
    lastSensorUpload = now;
    uploadSensorState();
  }

  delay(50);
}
