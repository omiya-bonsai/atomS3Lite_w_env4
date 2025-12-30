#include <M5AtomS3.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include <Adafruit_BMP280.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>
#include <FastLED.h>
#include "config.h"

// ============================================================================
// FastLED設定 (M5AtomS3 NeoPixel LED)
// ============================================================================
#define NUM_LEDS 1
#define LED_PIN 35 // M5AtomS3 の LED ピン
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
unsigned long lastMqttPublish = 0;
unsigned long lastSensorCheck = 0;
unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastLedUpdate = 0;
unsigned long lastMqttPublishSuccess = 0;
unsigned int sensorErrorCount = 0;
unsigned int mqttErrorCount = 0;
unsigned int wifiErrorCount = 0;
bool sensorHealthy = true;
bool mqttHealthy = true;

// ============================================================================
// グローバル変数: LED状態
// ============================================================================
typedef enum
{
  LED_STATE_STARTUP,     // 起動中（紫色）
  LED_STATE_CONNECTING,  // 接続中（黄色ゆっくり点滅）
  LED_STATE_HEALTHY,     // 正常（青色点灯）
  LED_STATE_ERROR,       // エラー（赤色高速点滅）
  LED_STATE_MQTT_SUCCESS // MQTT送信成功（緑色短点灯）
} LED_State_t;

LED_State_t currentLedState = LED_STATE_STARTUP;

// ============================================================================
// ユーティリティ関数
// ============================================================================

/**
 * @brief システムイベントをシリアルに出力
 * @param level ログレベル ("INFO", "WARN")
 * @param message ログメッセージ
 */
void logEvent(const char *level, const char *message)
{
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(message);
}

/**
 * @brief ファームウェア情報をシリアルに出力
 */
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

// ============================================================================
// LED制御関数
// ============================================================================

/**
 * @brief LEDを指定した色で点灯させる（RGB値で指定）
 * @param red RED値 (0-255)
 * @param green GREEN値 (0-255)
 * @param blue BLUE値 (0-255)
 */
void setLedColor(uint8_t red, uint8_t green, uint8_t blue)
{
  // FastLED を使用して NeoPixel LED を制御
  leds[0] = CRGB(red, green, blue);
  FastLED.show();
}

/**
 * @brief 現在の健康状態に応じてLED状態を更新
 * 正常：青色点灯
 * 接続中：黄色ゆっくり点滅
 * エラー：赤色高速点滅
 * MQTT送信成功：緑色短点灯
 */
void updateLedState()
{
  unsigned long currentTime = millis();

  // MQTT送信成功の短い点灯（200msの表示）
  if (currentLedState == LED_STATE_MQTT_SUCCESS &&
      currentTime - lastMqttPublishSuccess > 200)
  {
    currentLedState = LED_STATE_HEALTHY;
  }

  // 100ms ごとにLED更新
  if (currentTime - lastLedUpdate < 100)
  {
    return;
  }
  lastLedUpdate = currentTime;

  // エラーがある場合は赤色高速点滅（100ms周期）
  if (sensorErrorCount >= CONFIG_MAX_CONSECUTIVE_ERRORS ||
      mqttErrorCount >= 5 ||
      wifiErrorCount >= 3)
  {
    currentLedState = LED_STATE_ERROR;
    static bool blink = false;
    blink = !blink;
    setLedColor(blink ? 255 : 0, 0, 0); // 赤色高速点滅
  }
  // WiFiまたはMQTT接続中は黄色ゆっくり点滅（500ms周期）
  else if (WiFi.status() != WL_CONNECTED || !client.connected())
  {
    currentLedState = LED_STATE_CONNECTING;
    static unsigned long blinkCycle = 0;
    blinkCycle++;
    if (blinkCycle > 5) // 500ms周期
    {
      blinkCycle = 0;
    }
    setLedColor(255, 255, 0); // 黄色点灯（全周期点灯で「ゆっくり」を表現）
  }
  // 正常状態は青色点灯
  else
  {
    currentLedState = LED_STATE_HEALTHY;
    setLedColor(0, 0, 255); // 青色点灯
  }
}

// ============================================================================
// WiFi 接続関数
// ============================================================================

/**
 * @brief WiFiに接続する
 */
void setup_wifi()
{
  logEvent("INFO", "Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(CONFIG_MQTT_INITIAL_DELAY);
    Serial.print(".");
    if (millis() - startTime > CONFIG_WIFI_TIMEOUT)
    {
      logEvent("WARN", "WiFi connection timeout");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    logEvent("INFO", "WiFi connected");
    wifiErrorCount = 0;
  }
  else
  {
    logEvent("WARN", "WiFi connection failed");
    wifiErrorCount++;
  }
}

/**
 * @brief WiFiの接続状態を確認し、必要に応じて再接続する（非ブロッキング）
 * 接続失敗時も、定期的に再接続を試みる
 */
void reconnect_wifi()
{
  unsigned long currentTime = millis();

  // 前回の再接続試行から一定時間経過した場合のみ実行
  if (currentTime - lastWifiReconnectAttempt < CONFIG_WIFI_RECONNECT_INTERVAL)
  {
    return;
  }

  lastWifiReconnectAttempt = currentTime;

  if (WiFi.status() != WL_CONNECTED)
  {
    logEvent("INFO", "WiFi disconnected. Attempting to reconnect...");
    WiFi.reconnect();

    // 短時間の接続試行（ノンブロッキング）
    int attempt = 0;
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED &&
           attempt < 5 && // 最大5回試行（500ms程度）
           millis() - startTime < 500)
    {
      delay(100);
      Serial.print(".");
      attempt++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      logEvent("INFO", "WiFi reconnected successfully");
      wifiErrorCount = 0;
    }
    else
    {
      logEvent("WARN", "WiFi reconnection attempt in progress");
      wifiErrorCount++;
    }
  }
}

// ============================================================================
// センサー制御関数
// ============================================================================

/**
 * @brief センサーを再初期化する
 */
void reinitialize_sensors()
{
  logEvent("INFO", "Reinitializing sensors");

  sht4x.begin(Wire, CONFIG_SHT40_ADDRESS);

  if (!bmp.begin(CONFIG_BMP280_ADDRESS))
  {
    logEvent("WARN", "BMP280 reinitialization failed");
  }

  sensorErrorCount = 0;
  sensorHealthy = true;
  lastSensorCheck = millis();
}

// ============================================================================
// MQTT 接続関数
// ============================================================================

/**
 * @brief MQTTサーバーに接続する
 */
void reconnect()
{
  unsigned long connectStartTime = millis();

  while (!client.connected())
  {
    esp_task_wdt_reset();

    logEvent("INFO", "Attempting MQTT connection");
    String clientId = "AtomS3Lite-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str()))
    {
      logEvent("INFO", "MQTT connected");
      mqttErrorCount = 0;
      mqttHealthy = true;
      break;
    }
    else
    {
      Serial.print("MQTT failed, rc=");
      Serial.println(client.state());
      mqttErrorCount++;

      if (millis() - connectStartTime > CONFIG_MQTT_TIMEOUT)
      {
        logEvent("WARN", "MQTT connection timeout");
        mqttHealthy = false;
        break;
      }

      delay(CONFIG_MQTT_RECONNECT_DELAY);
    }
  }
}

// ============================================================================
// システム初期化
// ============================================================================

/**
 * @brief システムを初期化する
 */
void setup()
{
  M5.begin();
  Serial.begin(CONFIG_SERIAL_BAUD);
  delay(CONFIG_INIT_DELAY);

  // FastLED初期化（M5AtomS3 NeoPixel LED）
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(200); // 明るさを80%に設定（バッテリー節約）

  // LED初期化（起動中：紫色）
  setLedColor(128, 0, 128);
  currentLedState = LED_STATE_STARTUP;

  // ファームウェア情報を表示
  printFirmwareInfo();

  logEvent("INFO", "System startup");

  // I2C 初期化
  Wire.begin(CONFIG_SDA_PIN, CONFIG_SCL_PIN, CONFIG_I2C_FREQ);
  delay(CONFIG_INIT_DELAY);

  // SHT40 センサ初期化
  sht4x.begin(Wire, CONFIG_SHT40_ADDRESS);
  logEvent("INFO", "SHT40 sensor initialized");

  // BMP280 センサ初期化
  if (!bmp.begin(CONFIG_BMP280_ADDRESS))
  {
    logEvent("WARN", "BMP280 sensor initialization failed");
    sensorHealthy = false;
  }
  else
  {
    logEvent("INFO", "BMP280 sensor initialized");
  }

  // WiFi 接続
  setup_wifi();

  // MQTT サーバ設定
  client.setServer(CONFIG_MQTT_SERVER, CONFIG_MQTT_PORT);
  client.setKeepAlive(CONFIG_MQTT_KEEPALIVE);
  client.setSocketTimeout(CONFIG_MQTT_LOOP_TIMEOUT / 1000);
  randomSeed(micros());

  // ウォッチドッグタイマー初期化
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = CONFIG_WDT_TIMEOUT * 1000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true};
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  logEvent("INFO", "Watchdog timer enabled");
}

// ============================================================================
// メインループ
// ============================================================================

/**
 * @brief メインループ処理
 * - WiFi接続管理
 * - MQTT接続・データ送信
 * - センサー読取り
 * - ウォッチドッグタイマー管理
 */
void loop()
{
  // ウォッチドッグタイマーをリセット
  esp_task_wdt_reset();

  // LED状態を更新
  updateLedState();

  // WiFi 接続確認・再接続処理
  reconnect_wifi();

  // センサー定期再初期化（ハングアップ防止）
  if (millis() - lastSensorCheck > CONFIG_SENSOR_REINIT_INTERVAL)
  {
    reinitialize_sensors();
  }

  // MQTT 接続確認・再接続処理
  if (!client.connected())
  {
    reconnect();
  }

  // MQTT loop（タイムアウト保護）
  unsigned long mqttStartTime = millis();
  client.loop();
  unsigned long mqttLoopTime = millis() - mqttStartTime;
  if (mqttLoopTime > CONFIG_MQTT_LOOP_TIMEOUT)
  {
    logEvent("WARN", "MQTT loop timeout");
  }

  // ============================================================================
  // センサーデータ取得
  // ============================================================================

  float temperature = 0.0, humidity = 0.0;
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
  }
  else
  {
    sensorErrorCount = 0;
    Serial.print("[TEMP] ");
    Serial.print(temperature);
    Serial.println(" °C");
    Serial.print("[HUM] ");
    Serial.print(humidity);
    Serial.println(" %");
  }

  // BMP280 から気圧を読取り（Pa → hPa に換算）
  float pressure = 0.0;
  if (sensorHealthy)
  {
    pressure = bmp.readPressure() / 100.0F;
    Serial.print("[PRES] ");
    Serial.print(pressure);
    Serial.println(" hPa");
  }

  // ============================================================================
  // MQTT データ送信
  // ============================================================================

  if (client.connected() && shtError == 0)
  {
    char payload[CONFIG_JSON_PAYLOAD_SIZE];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f, \"humidity\":%.2f, \"pressure\":%.2f}",
             temperature, humidity, pressure);

    if (client.publish(CONFIG_MQTT_TOPIC, payload))
    {
      logEvent("INFO", "MQTT publish successful");
      lastMqttPublish = millis();
      lastMqttPublishSuccess = millis();
      currentLedState = LED_STATE_MQTT_SUCCESS;
      setLedColor(0, 255, 0); // 緑色短点灯
      mqttErrorCount = 0;
    }
    else
    {
      logEvent("WARN", "MQTT publish failed");
      mqttErrorCount++;
    }
  }
  else
  {
    if (!client.connected())
    {
      logEvent("WARN", "MQTT not connected, skipping publish");
    }
    if (shtError != 0)
    {
      logEvent("WARN", "Sensor error, skipping publish");
    }
  }

  // データ送信間隔で待機
  delay(CONFIG_PUBLISH_INTERVAL);
}
