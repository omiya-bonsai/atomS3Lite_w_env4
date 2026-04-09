#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ファームウェア情報
// ============================================================================
#define DEVICE_MODEL "M5 Atom S3 Lite"
#define FIRMWARE_NAME "AtomS3Lite Environmental Sensor"
#define FIRMWARE_VERSION "1.1.1"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// ============================================================================
// デバイス識別
// ============================================================================
#define CONFIG_DEVICE_ID "env4"
#define CONFIG_MQTT_CLIENT_ID_PREFIX "AtomS3Lite-Env4-"

// ============================================================================
// WiFi 設定
// ============================================================================
const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";

// ============================================================================
// I2C 設定
// AtomS3 Lite: SDA=2, SCL=1
// ============================================================================
#define CONFIG_SDA_PIN 2
#define CONFIG_SCL_PIN 1
#define CONFIG_I2C_FREQ 100000UL

// ============================================================================
// I2C センサアドレス
// ============================================================================
#define CONFIG_SHT40_ADDRESS 0x44
#define CONFIG_BMP280_ADDRESS 0x76

// ============================================================================
// MQTT サーバ設定
// ============================================================================
#define CONFIG_MQTT_SERVER "broker.local"
#define CONFIG_MQTT_PORT 1883
#define CONFIG_MQTT_TOPIC "env4"
#define CONFIG_MQTT_META_TOPIC "home/env/env4/meta"
#define CONFIG_MQTT_META_WINDOWS_TOPIC "home/env/env4/meta_windows"
#define CONFIG_MQTT_STATUS_TOPIC "home/env/env4/status"
#define CONFIG_MQTT_KEEPALIVE 60
#define CONFIG_MQTT_SOCKET_TIMEOUT_SEC 5

// ============================================================================
// NTP / 時刻設定
// ============================================================================
#define CONFIG_TZ_INFO "JST-9"
#define CONFIG_NTP_SERVER_1 "ntp.nict.jp"
#define CONFIG_NTP_SERVER_2 "pool.ntp.org"
#define CONFIG_NTP_SERVER_3 "time.google.com"

// NTP同期待ちタイムアウト
#define CONFIG_NTP_SYNC_TIMEOUT_MS 15000UL

// 24時間ごとに再同期
#define CONFIG_NTP_RESYNC_INTERVAL_MS 86400000UL

// この epoch より大きければ「有効な時刻」とみなす
// 1700000000 ≒ 2023-11-14 UTC
#define CONFIG_VALID_EPOCH_THRESHOLD 1700000000LL

// ============================================================================
// WiFi / MQTT 接続タイミング
// ============================================================================
#define CONFIG_WIFI_TIMEOUT 30000UL
#define CONFIG_WIFI_RECONNECT_INTERVAL 10000UL
#define CONFIG_WIFI_RECONNECT_SHORT_TRY_MS 1000UL

#define CONFIG_MQTT_TIMEOUT 10000UL
#define CONFIG_MQTT_RECONNECT_DELAY 2000UL
#define CONFIG_MQTT_INITIAL_DELAY 500UL

// ============================================================================
// センサー保守設定
// ============================================================================
#define CONFIG_SENSOR_REINIT_INTERVAL 300000UL
#define CONFIG_MAX_CONSECUTIVE_ERRORS 10

// ============================================================================
// データ送信設定
// ============================================================================
#define CONFIG_PUBLISH_INTERVAL 30000UL
#define CONFIG_JSON_PAYLOAD_SIZE 192
#define CONFIG_META_JSON_PAYLOAD_SIZE 640
#define CONFIG_META_WINDOWS_JSON_PAYLOAD_SIZE 768
#define CONFIG_STATUS_JSON_PAYLOAD_SIZE 320

// 短・中・長の差分ウィンドウ（publish単位）
#define CONFIG_DELTA_SHORT_STEPS 1
#define CONFIG_DELTA_MID_STEPS 4
#define CONFIG_DELTA_LONG_STEPS 10

// 時刻が有効になるまで publish を抑止するか
#define CONFIG_REQUIRE_TIME_VALID 1

// ============================================================================
// LED設定
// ============================================================================
#define CONFIG_LED_UPDATE_INTERVAL 100UL
#define CONFIG_LED_MQTT_SUCCESS_TIME 200UL
#define CONFIG_LED_BRIGHTNESS 200

// ============================================================================
// メインループ設定
// ============================================================================
#define CONFIG_MAIN_LOOP_DELAY_MS 10UL

// ============================================================================
// シリアル通信設定
// ============================================================================
#define CONFIG_SERIAL_BAUD 115200
#define CONFIG_INIT_DELAY 100UL

#endif // CONFIG_H
