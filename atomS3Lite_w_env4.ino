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
static constexpr size_t META_HISTORY_SIZE = 12;

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
unsigned int wifiReconnectCount = 0;
unsigned int mqttReconnectCount = 0;
unsigned long publishSeq = 0;
unsigned long statusSeq = 0;

bool sensorHealthy = true;
bool mqttHealthy = true;
bool timeValid = false;
bool wifiEverConnected = false;
bool mqttEverConnected = false;

float tempHistory[META_HISTORY_SIZE] = {0};
float humHistory[META_HISTORY_SIZE] = {0};
float pressureHistory[META_HISTORY_SIZE] = {0};
size_t metaHistoryCount = 0;
size_t metaHistoryIndex = 0;

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

void addMetaHistory(float temperature, float humidity, float pressure)
{
  tempHistory[metaHistoryIndex] = temperature;
  humHistory[metaHistoryIndex] = humidity;
  pressureHistory[metaHistoryIndex] = pressure;
  metaHistoryIndex = (metaHistoryIndex + 1) % META_HISTORY_SIZE;
  if (metaHistoryCount < META_HISTORY_SIZE)
  {
    metaHistoryCount++;
  }
}

float historyValue(const float *history, size_t logicalIndex)
{
  size_t base = (metaHistoryIndex + META_HISTORY_SIZE - metaHistoryCount) % META_HISTORY_SIZE;
  size_t actual = (base + logicalIndex) % META_HISTORY_SIZE;
  return history[actual];
}

float historyAverage(const float *history)
{
  if (metaHistoryCount == 0)
  {
    return 0.0f;
  }

  float sum = 0.0f;
  for (size_t i = 0; i < metaHistoryCount; ++i)
  {
    sum += historyValue(history, i);
  }
  return sum / static_cast<float>(metaHistoryCount);
}

float historyPrevious(const float *history)
{
  if (metaHistoryCount < 2)
  {
    return historyValue(history, metaHistoryCount - 1);
  }
  return historyValue(history, metaHistoryCount - 2);
}

float historyByStepsBack(const float *history, size_t stepsBack)
{
  if (metaHistoryCount == 0 || stepsBack >= metaHistoryCount)
  {
    return 0.0f;
  }
  size_t latestLogicalIndex = metaHistoryCount - 1;
  size_t targetLogicalIndex = latestLogicalIndex - stepsBack;
  return historyValue(history, targetLogicalIndex);
}

float deltaFromAverage(float current, float average)
{
  return current - average;
}

float deltaFromPrevious(float current, float previous)
{
  return current - previous;
}

float ratePercent(float current, float average)
{
  if (fabs(average) < 0.0001f)
  {
    return 0.0f;
  }
  return ((current - average) / average) * 100.0f;
}

const char *classifyEnvTrend(float delta, float mildThreshold, float strongThreshold)
{
  if (delta <= -strongThreshold) return "falling_fast";
  if (delta <= -mildThreshold) return "falling";
  if (delta >= strongThreshold) return "rising_fast";
  if (delta >= mildThreshold) return "rising";
  return "stable";
}

const char *currentStatusText()
{
  if (WiFi.status() != WL_CONNECTED || !client.connected())
  {
    return "offline";
  }
  if (!sensorHealthy || sensorErrorCount > 0 || mqttErrorCount > 0 || wifiErrorCount > 0)
  {
    return "warn";
  }
  return "ok";
}

bool buildStatusPayload(char *payload, size_t payloadSize, const char *reason)
{
  time_t now = time(nullptr);
  unsigned long uptimeSec = millis() / 1000UL;
  int tv = isTimeValid() ? 1 : 0;
  const char *wifiText = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  IPAddress ip = WiFi.localIP();

  int written = snprintf(
      payload, payloadSize,
      "{\"status\":\"%s\",\"reason\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%u.%u.%u.%u\","
      "\"sensor_ready\":%s,\"sensor_error_count\":%u,\"wifi_reconnect_count\":%u,"
      "\"mqtt_reconnect_count\":%u,\"uptime_s\":%lu,\"seq\":%lu,\"unix_time\":%lld,\"time_valid\":%s}",
      currentStatusText(),
      reason ? reason : "none",
      wifiText,
      ip[0], ip[1], ip[2], ip[3],
      sensorHealthy ? "true" : "false",
      sensorErrorCount,
      wifiReconnectCount,
      mqttReconnectCount,
      uptimeSec,
      statusSeq,
      static_cast<long long>(now),
      tv ? "true" : "false");

  return (written > 0 && static_cast<size_t>(written) < payloadSize);
}

bool publishStatus(const char *reason)
{
  if (!client.connected())
  {
    return false;
  }

  char payload[CONFIG_STATUS_JSON_PAYLOAD_SIZE];
  if (!buildStatusPayload(payload, sizeof(payload), reason))
  {
    logEvent("WARN", "Status payload buffer too small");
    return false;
  }

  bool ok = client.publish(CONFIG_MQTT_STATUS_TOPIC, payload, true);
  if (ok)
  {
    statusSeq++;
  }
  else
  {
    logEvent("WARN", "MQTT status publish failed");
  }
  return ok;
}

bool publishEnvMeta(float temperature, float humidity, float pressure)
{
  if (!client.connected())
  {
    return false;
  }

  addMetaHistory(temperature, humidity, pressure);

  float tempAvg = historyAverage(tempHistory);
  float humAvg = historyAverage(humHistory);
  float pressureAvg = historyAverage(pressureHistory);
  float tempPrev = historyPrevious(tempHistory);
  float humPrev = historyPrevious(humHistory);
  float pressurePrev = historyPrevious(pressureHistory);

  float tempDelta = deltaFromAverage(temperature, tempAvg);
  float humDelta = deltaFromAverage(humidity, humAvg);
  float pressureDelta = deltaFromAverage(pressure, pressureAvg);
  float tempDeltaPrev = deltaFromPrevious(temperature, tempPrev);
  float humDeltaPrev = deltaFromPrevious(humidity, humPrev);
  float pressureDeltaPrev = deltaFromPrevious(pressure, pressurePrev);
  float tempRate = ratePercent(temperature, tempAvg);
  float humRate = ratePercent(humidity, humAvg);
  float pressureRate = ratePercent(pressure, pressureAvg);

  char payload[CONFIG_META_JSON_PAYLOAD_SIZE];
  time_t now = time(nullptr);
  int tv = isTimeValid() ? 1 : 0;
  int written = snprintf(
      payload, sizeof(payload),
      "{\"temperature\":{\"current\":%.2f,\"avg\":%.2f,\"delta\":%.2f,\"delta_prev\":%.2f,\"rate_pct\":%.2f,\"trend\":\"%s\"},"
      "\"humidity\":{\"current\":%.2f,\"avg\":%.2f,\"delta\":%.2f,\"delta_prev\":%.2f,\"rate_pct\":%.2f,\"trend\":\"%s\"},"
      "\"pressure\":{\"current\":%.2f,\"avg\":%.2f,\"delta\":%.2f,\"delta_prev\":%.2f,\"rate_pct\":%.3f,\"trend\":\"%s\"},"
      "\"samples\":%u,\"interval_ms\":%lu,\"seq\":%lu,\"unix_time\":%lld,\"time_valid\":%s}",
      temperature, tempAvg, tempDelta, tempDeltaPrev, tempRate, classifyEnvTrend(tempDelta, 0.2f, 0.6f),
      humidity, humAvg, humDelta, humDeltaPrev, humRate, classifyEnvTrend(humDelta, 2.0f, 6.0f),
      pressure, pressureAvg, pressureDelta, pressureDeltaPrev, pressureRate, classifyEnvTrend(pressureDelta, 0.3f, 1.0f),
      static_cast<unsigned>(metaHistoryCount),
      static_cast<unsigned long>(CONFIG_PUBLISH_INTERVAL),
      static_cast<unsigned long>(publishSeq),
      static_cast<long long>(now),
      tv ? "true" : "false");

  if (written <= 0 || static_cast<size_t>(written) >= sizeof(payload))
  {
    logEvent("WARN", "Env meta payload buffer too small");
    return false;
  }

  bool ok = client.publish(CONFIG_MQTT_META_TOPIC, payload, true);
  if (!ok)
  {
    logEvent("WARN", "MQTT env meta publish failed");
  }
  return ok;
}

bool publishEnvMetaWindows(float temperature, float humidity, float pressure)
{
  if (!client.connected())
  {
    return false;
  }
  if (metaHistoryCount == 0)
  {
    return false;
  }

  const bool hasShort = CONFIG_DELTA_SHORT_STEPS < metaHistoryCount;
  const bool hasMid = CONFIG_DELTA_MID_STEPS < metaHistoryCount;
  const bool hasLong = CONFIG_DELTA_LONG_STEPS < metaHistoryCount;

  float tempShort = hasShort ? deltaFromPrevious(temperature, historyByStepsBack(tempHistory, CONFIG_DELTA_SHORT_STEPS)) : 0.0f;
  float tempMid = hasMid ? deltaFromPrevious(temperature, historyByStepsBack(tempHistory, CONFIG_DELTA_MID_STEPS)) : 0.0f;
  float tempLong = hasLong ? deltaFromPrevious(temperature, historyByStepsBack(tempHistory, CONFIG_DELTA_LONG_STEPS)) : 0.0f;

  float humShort = hasShort ? deltaFromPrevious(humidity, historyByStepsBack(humHistory, CONFIG_DELTA_SHORT_STEPS)) : 0.0f;
  float humMid = hasMid ? deltaFromPrevious(humidity, historyByStepsBack(humHistory, CONFIG_DELTA_MID_STEPS)) : 0.0f;
  float humLong = hasLong ? deltaFromPrevious(humidity, historyByStepsBack(humHistory, CONFIG_DELTA_LONG_STEPS)) : 0.0f;

  float pressureShort = hasShort ? deltaFromPrevious(pressure, historyByStepsBack(pressureHistory, CONFIG_DELTA_SHORT_STEPS)) : 0.0f;
  float pressureMid = hasMid ? deltaFromPrevious(pressure, historyByStepsBack(pressureHistory, CONFIG_DELTA_MID_STEPS)) : 0.0f;
  float pressureLong = hasLong ? deltaFromPrevious(pressure, historyByStepsBack(pressureHistory, CONFIG_DELTA_LONG_STEPS)) : 0.0f;

  char tempShortBuf[24], tempMidBuf[24], tempLongBuf[24];
  char humShortBuf[24], humMidBuf[24], humLongBuf[24];
  char pressureShortBuf[24], pressureMidBuf[24], pressureLongBuf[24];
  snprintf(tempShortBuf, sizeof(tempShortBuf), hasShort ? "%.2f" : "null", tempShort);
  snprintf(tempMidBuf, sizeof(tempMidBuf), hasMid ? "%.2f" : "null", tempMid);
  snprintf(tempLongBuf, sizeof(tempLongBuf), hasLong ? "%.2f" : "null", tempLong);
  snprintf(humShortBuf, sizeof(humShortBuf), hasShort ? "%.2f" : "null", humShort);
  snprintf(humMidBuf, sizeof(humMidBuf), hasMid ? "%.2f" : "null", humMid);
  snprintf(humLongBuf, sizeof(humLongBuf), hasLong ? "%.2f" : "null", humLong);
  snprintf(pressureShortBuf, sizeof(pressureShortBuf), hasShort ? "%.2f" : "null", pressureShort);
  snprintf(pressureMidBuf, sizeof(pressureMidBuf), hasMid ? "%.2f" : "null", pressureMid);
  snprintf(pressureLongBuf, sizeof(pressureLongBuf), hasLong ? "%.2f" : "null", pressureLong);

  char payload[CONFIG_META_WINDOWS_JSON_PAYLOAD_SIZE];
  time_t now = time(nullptr);
  int tv = isTimeValid() ? 1 : 0;
  int written = snprintf(
      payload, sizeof(payload),
      "{\"temperature\":{\"delta_short\":%s,\"delta_mid\":%s,\"delta_long\":%s},"
      "\"humidity\":{\"delta_short\":%s,\"delta_mid\":%s,\"delta_long\":%s},"
      "\"pressure\":{\"delta_short\":%s,\"delta_mid\":%s,\"delta_long\":%s},"
      "\"short_steps\":%u,\"mid_steps\":%u,\"long_steps\":%u,"
      "\"short_sec\":%lu,\"mid_sec\":%lu,\"long_sec\":%lu,"
      "\"samples\":%u,\"interval_ms\":%lu,\"seq\":%lu,\"unix_time\":%lld,\"time_valid\":%s}",
      tempShortBuf, tempMidBuf, tempLongBuf,
      humShortBuf, humMidBuf, humLongBuf,
      pressureShortBuf, pressureMidBuf, pressureLongBuf,
      static_cast<unsigned>(CONFIG_DELTA_SHORT_STEPS),
      static_cast<unsigned>(CONFIG_DELTA_MID_STEPS),
      static_cast<unsigned>(CONFIG_DELTA_LONG_STEPS),
      static_cast<unsigned long>((CONFIG_DELTA_SHORT_STEPS * CONFIG_PUBLISH_INTERVAL) / 1000UL),
      static_cast<unsigned long>((CONFIG_DELTA_MID_STEPS * CONFIG_PUBLISH_INTERVAL) / 1000UL),
      static_cast<unsigned long>((CONFIG_DELTA_LONG_STEPS * CONFIG_PUBLISH_INTERVAL) / 1000UL),
      static_cast<unsigned>(metaHistoryCount),
      static_cast<unsigned long>(CONFIG_PUBLISH_INTERVAL),
      static_cast<unsigned long>(publishSeq),
      static_cast<long long>(now),
      tv ? "true" : "false");

  if (written <= 0 || static_cast<size_t>(written) >= sizeof(payload))
  {
    logEvent("WARN", "Env windows payload buffer too small");
    return false;
  }

  bool ok = client.publish(CONFIG_MQTT_META_WINDOWS_TOPIC, payload, true);
  if (!ok)
  {
    logEvent("WARN", "MQTT env windows publish failed");
  }
  return ok;
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
    wifiEverConnected = true;
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
    if (wifiEverConnected)
    {
      wifiReconnectCount++;
    }
    wifiEverConnected = true;
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
  char willPayload[CONFIG_STATUS_JSON_PAYLOAD_SIZE];
  buildStatusPayload(willPayload, sizeof(willPayload), "last_will");

  if (client.connect(clientId.c_str(), nullptr, nullptr,
                     CONFIG_MQTT_STATUS_TOPIC, 1, true, willPayload))
  {
    logEvent("INFO", "MQTT connected");
    const bool wasConnectedBefore = mqttEverConnected;
    if (wasConnectedBefore)
    {
      mqttReconnectCount++;
    }
    mqttEverConnected = true;
    mqttErrorCount = 0;
    mqttHealthy = true;
    publishStatus(wasConnectedBefore ? "reconnect" : "boot");
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
    publishEnvMeta(temperature, humidity, pressure);
    publishEnvMetaWindows(temperature, humidity, pressure);
    publishSeq++;
    lastMqttPublishSuccessMs = millis();
    currentLedState = LED_STATE_MQTT_SUCCESS;
    setLedColor(0, 255, 0);
    mqttErrorCount = 0;
    publishStatus("periodic");
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
      publishStatus("sensor_error");
    }
  }

  delay(CONFIG_MAIN_LOOP_DELAY_MS);
}
