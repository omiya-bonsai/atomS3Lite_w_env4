#include <M5AtomS3.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include <Adafruit_BMP280.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <time.h>
#include "config.h"

// ============================================================================
// FastLED設定 (M5AtomS3 NeoPixel LED)
// ============================================================================
#define NUM_LEDS 1
#define LED_PIN 35
CRGB leds[NUM_LEDS];

// ============================================================================
// グローバル変数: センサー
// ============================================================================
SensirionI2cSht4x sht4x;
Adafruit_BMP280 bmp;

// ============================================================================
// グローバル変数: WiFi・MQTT
// ============================================================================
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// ============================================================================
// グローバル変数: 状態管理
// ============================================================================
unsigned long lastPublishAttemptMs = 0;
unsigned long lastSensorReinitMs = 0;
unsigned long lastWifiReconnectAttemptMs = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
unsigned long lastLedUpdateMs = 0;
unsigned long lastMqttPublishSuccessMs = 0;
unsigned long lastNtpSyncAttemptMs = 0;
unsigned long lastNtpSyncSuccessMs = 0;

unsigned int sensorErrorCount = 0;
unsigned int mqttErrorCount = 0;
unsigned int wifiErrorCount = 0;
unsigned long publishSeq = 0;

bool sensorHealthy = true;
bool mqttHealthy = true;
bool timeValid = false;

// ============================================================================
// LED状態
// ============================================================================
typedef enum
{
  LED_STATE_STARTUP,
  LED_STATE_CONNECTING,
  LED_STATE_HEALTHY,
  LED_STATE_ERROR,
  LED_STATE_MQTT_SUCCESS
} LED_State_t;

LED_State_t currentLedState = LED_STATE_STARTUP;

// ============================================================================
// ユーティリティ
// ============================================================================

void logEvent(const char *level, const char *message)
{
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(message);
}

void printFirmwareInfo()
{
  Serial.println();
  Serial.println("================================================================================");
  Serial.print("Device: ");
  Serial.println(DEVICE_MODEL);
  Serial.print("Firmware: ");
  Serial.print(FIRMWARE_NAME);
  Serial.print(" v");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("Built: ");
  Serial.print(FIRMWARE_BUILD_DATE);
  Serial.print(" ");
  Serial.println(FIRMWARE_BUILD_TIME);
  Serial.println("================================================================================");
  Serial.println();
}

void setLedColor(uint8_t red, uint8_t green, uint8_t blue)
{
  leds[0] = CRGB(red, green, blue);
  FastLED.show();
}

// ============================================================================
// LED制御
// ============================================================================

void updateLedState()
{
  unsigned long nowMs = millis();

  if (currentLedState == LED_STATE_MQTT_SUCCESS &&
      nowMs - lastMqttPublishSuccessMs > CONFIG_LED_MQTT_SUCCESS_TIME)
  {
    currentLedState = LED_STATE_HEALTHY;
  }

  if (nowMs - lastLedUpdateMs < CONFIG_LED_UPDATE_INTERVAL)
  {
    return;
  }
  lastLedUpdateMs = nowMs;

  if (sensorErrorCount >= CONFIG_MAX_CONSECUTIVE_ERRORS ||
      mqttErrorCount >= 5 ||
      wifiErrorCount >= 3)
  {
    currentLedState = LED_STATE_ERROR;
    static bool blink = false;
    blink = !blink;
    setLedColor(blink ? 255 : 0, 0, 0);
  }
  else if (WiFi.status() != WL_CONNECTED || !client.connected())
  {
    currentLedState = LED_STATE_CONNECTING;
    static bool blink = false;
    blink = !blink;
    setLedColor(blink ? 255 : 64, blink ? 180 : 64, 0);
  }
  else
  {
    currentLedState = LED_STATE_HEALTHY;
    setLedColor(0, 0, 255);
  }
}

// ============================================================================
// 時刻/NTP
// ============================================================================

bool isTimeValid()
{
  time_t now = time(nullptr);
  return now >= CONFIG_VALID_EPOCH_THRESHOLD;
}

void printCurrentLocalTime()
{
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo))
  {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.print("[INFO] Local time: ");
    Serial.println(buf);
  }
}

bool syncTimeWithNtp(bool verboseLog)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (verboseLog)
    {
      logEvent("WARN", "NTP sync skipped: WiFi not connected");
    }
    timeValid = isTimeValid();
    return timeValid;
  }

  lastNtpSyncAttemptMs = millis();

  if (verboseLog)
  {
    logEvent("INFO", "Starting NTP sync");
  }

  configTzTime(CONFIG_TZ_INFO, CONFIG_NTP_SERVER_1, CONFIG_NTP_SERVER_2, CONFIG_NTP_SERVER_3);

  unsigned long startMs = millis();
  while (!isTimeValid() && (millis() - startMs < CONFIG_NTP_SYNC_TIMEOUT_MS))
  {
    delay(200);
  }

  timeValid = isTimeValid();

  if (timeValid)
  {
    lastNtpSyncSuccessMs = millis();
    if (verboseLog)
    {
      logEvent("INFO", "NTP sync successful");
      printCurrentLocalTime();
    }
    return true;
  }

  if (verboseLog)
  {
    logEvent("WARN", "NTP sync timeout");
  }
  return false;
}

void maintainTimeSync()
{
  timeValid = isTimeValid();

  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (!timeValid)
  {
    syncTimeWithNtp(true);
    return;
  }

  if (millis() - lastNtpSyncSuccessMs >= CONFIG_NTP_RESYNC_INTERVAL_MS)
  {
    syncTimeWithNtp(true);
  }
}

// ============================================================================
// WiFi
// ============================================================================

void setup_wifi()
{
  logEvent("INFO", "Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(CONFIG_MQTT_INITIAL_DELAY);
    Serial.print(".");

    if (millis() - startMs > CONFIG_WIFI_TIMEOUT)
    {
      logEvent("WARN", "WiFi connection timeout");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    logEvent("INFO", "WiFi connected");
    Serial.print("[INFO] IP: ");
    Serial.println(WiFi.localIP());
    wifiErrorCount = 0;
  }
  else
  {
    logEvent("WARN", "WiFi connection failed");
    wifiErrorCount++;
  }
}

void reconnect_wifi()
{
  unsigned long nowMs = millis();

  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  if (nowMs - lastWifiReconnectAttemptMs < CONFIG_WIFI_RECONNECT_INTERVAL)
  {
    return;
  }

  lastWifiReconnectAttemptMs = nowMs;

  logEvent("INFO", "WiFi disconnected. Attempting to reconnect...");
  WiFi.disconnect();
  delay(50);
  WiFi.begin(ssid, password);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startMs < CONFIG_WIFI_RECONNECT_SHORT_TRY_MS)
  {
    delay(100);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    logEvent("INFO", "WiFi reconnected successfully");
    Serial.print("[INFO] IP: ");
    Serial.println(WiFi.localIP());
    wifiErrorCount = 0;

    syncTimeWithNtp(true);
  }
  else
  {
    logEvent("WARN", "WiFi reconnection attempt did not complete");
    wifiErrorCount++;
  }
}

// ============================================================================
// センサー
// ============================================================================

void reinitialize_sensors()
{
  logEvent("INFO", "Reinitializing sensors");

  sht4x.begin(Wire, CONFIG_SHT40_ADDRESS);

  if (!bmp.begin(CONFIG_BMP280_ADDRESS))
  {
    logEvent("WARN", "BMP280 reinitialization failed");
    sensorHealthy = false;
    return;
  }

  sensorErrorCount = 0;
  sensorHealthy = true;
  lastSensorReinitMs = millis();
}

bool readSensors(float &temperature, float &humidity, float &pressure)
{
  temperature = 0.0f;
  humidity = 0.0f;
  pressure = 0.0f;

  uint16_t shtError = sht4x.measureHighPrecision(temperature, humidity);
  if (shtError != 0)
  {
    logEvent("WARN", "SHT40 measurement error");
    sensorErrorCount++;

    if (sensorErrorCount >= CONFIG_MAX_CONSECUTIVE_ERRORS)
    {
      sensorHealthy = false;
      reinitialize_sensors();
    }
    return false;
  }

  sensorErrorCount = 0;
  sensorHealthy = true;

  pressure = bmp.readPressure() / 100.0F;
  if (!isfinite(pressure) || pressure <= 0.0f)
  {
    logEvent("WARN", "BMP280 measurement invalid");
    return false;
  }

  Serial.print("[TEMP] ");
  Serial.print(temperature, 2);
  Serial.println(" °C");

  Serial.print("[HUM] ");
  Serial.print(humidity, 2);
  Serial.println(" %");

  Serial.print("[PRES] ");
  Serial.print(pressure, 2);
  Serial.println(" hPa");

  return true;
}

// ============================================================================
// MQTT
// ============================================================================

void reconnect_mqtt()
{
  unsigned long nowMs = millis();

  if (client.connected())
  {
    return;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    mqttHealthy = false;
    return;
  }

  if (nowMs - lastMqttReconnectAttemptMs < CONFIG_MQTT_RECONNECT_DELAY)
  {
    return;
  }

  lastMqttReconnectAttemptMs = nowMs;

  logEvent("INFO", "Attempting MQTT connection");

  String clientId = String(CONFIG_MQTT_CLIENT_ID_PREFIX) + String(random(0xffff), HEX);

  if (client.connect(clientId.c_str()))
  {
    logEvent("INFO", "MQTT connected");
    mqttErrorCount = 0;
    mqttHealthy = true;
  }
  else
  {
    Serial.print("[WARN] MQTT failed, rc=");
    Serial.println(client.state());
    mqttErrorCount++;
    mqttHealthy = false;
  }
}

bool buildPayload(char *payload, size_t payloadSize,
                  float temperature, float humidity, float pressure)
{
  time_t now = time(nullptr);
  unsigned long uptimeSec = millis() / 1000UL;
  int tv = isTimeValid() ? 1 : 0;

  int written = snprintf(
      payload, payloadSize,
      "{\"id\":\"%s\",\"ts\":%lld,\"temperature\":%.2f,\"humidity\":%.2f,"
      "\"pressure\":%.2f,\"seq\":%lu,\"uptime_s\":%lu,\"time_valid\":%d}",
      CONFIG_DEVICE_ID,
      static_cast<long long>(now),
      temperature,
      humidity,
      pressure,
      publishSeq,
      uptimeSec,
      tv);

  return (written > 0 && static_cast<size_t>(written) < payloadSize);
}

void publishSensorData(float temperature, float humidity, float pressure)
{
  if (!client.connected())
  {
    logEvent("WARN", "MQTT not connected, skipping publish");
    return;
  }

  if (CONFIG_REQUIRE_TIME_VALID && !isTimeValid())
  {
    logEvent("WARN", "Time not valid yet, skipping publish");
    return;
  }

  char payload[CONFIG_JSON_PAYLOAD_SIZE];
  if (!buildPayload(payload, sizeof(payload), temperature, humidity, pressure))
  {
    logEvent("WARN", "Payload buffer too small");
    return;
  }

  Serial.print("[MQTT] ");
  Serial.println(payload);

  if (client.publish(CONFIG_MQTT_TOPIC, payload))
  {
    logEvent("INFO", "MQTT publish successful");
    publishSeq++;
    lastMqttPublishSuccessMs = millis();
    currentLedState = LED_STATE_MQTT_SUCCESS;
    setLedColor(0, 255, 0);
    mqttErrorCount = 0;
  }
  else
  {
    logEvent("WARN", "MQTT publish failed");
    mqttErrorCount++;
  }
}

// ============================================================================
// setup
// ============================================================================

void setup()
{
  M5.begin();
  Serial.begin(CONFIG_SERIAL_BAUD);
  delay(CONFIG_INIT_DELAY);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(CONFIG_LED_BRIGHTNESS);

  setLedColor(128, 0, 128);
  currentLedState = LED_STATE_STARTUP;

  printFirmwareInfo();
  logEvent("INFO", "System startup");

  Wire.begin(CONFIG_SDA_PIN, CONFIG_SCL_PIN, CONFIG_I2C_FREQ);
  delay(CONFIG_INIT_DELAY);

  sht4x.begin(Wire, CONFIG_SHT40_ADDRESS);
  logEvent("INFO", "SHT40 sensor initialized");

  if (!bmp.begin(CONFIG_BMP280_ADDRESS))
  {
    logEvent("WARN", "BMP280 sensor initialization failed");
    sensorHealthy = false;
  }
  else
  {
    logEvent("INFO", "BMP280 sensor initialized");
  }

  setup_wifi();

  client.setServer(CONFIG_MQTT_SERVER, CONFIG_MQTT_PORT);
  client.setKeepAlive(CONFIG_MQTT_KEEPALIVE);
  client.setSocketTimeout(CONFIG_MQTT_SOCKET_TIMEOUT_SEC);

  randomSeed(micros());

  if (WiFi.status() == WL_CONNECTED)
  {
    syncTimeWithNtp(true);
  }

  lastPublishAttemptMs = millis();
  lastSensorReinitMs = millis();
}

// ============================================================================
// loop
// ============================================================================

void loop()
{
  updateLedState();

  reconnect_wifi();
  maintainTimeSync();
  reconnect_mqtt();

  if (client.connected())
  {
    client.loop();
  }

  if (millis() - lastSensorReinitMs >= CONFIG_SENSOR_REINIT_INTERVAL)
  {
    reinitialize_sensors();
  }

  if (millis() - lastPublishAttemptMs >= CONFIG_PUBLISH_INTERVAL)
  {
    lastPublishAttemptMs = millis();

    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    bool sensorOk = readSensors(temperature, humidity, pressure);
    if (sensorOk)
    {
      publishSensorData(temperature, humidity, pressure);
    }
    else
    {
      logEvent("WARN", "Sensor read failed, skipping publish");
    }
  }

  delay(CONFIG_MAIN_LOOP_DELAY_MS);
}