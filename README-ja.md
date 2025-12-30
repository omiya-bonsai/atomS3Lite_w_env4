# AtomS3Lite Environmental Sensor

**言語:** 日本語 | [English](README.md)

M5 Atom S3 Liteを使用した、WiFi・MQTT対応の環境センサープロジェクトです。温度、湿度、気圧をリアルタイムで測定し、MQTTで送信します.

## デバイスの外観

<div style="display: flex; gap: 20px;">
  <img src="images/IMG_8186.jpeg" alt="デバイス - 表" width="45%">
  <img src="images/IMG_8187.jpeg" alt="デバイス - 裏" width="45%">
</div>

## 機能

- **センサー計測**
  - SHT40（温度・湿度）
  - BMP280（気圧）
  
- **通信**
  - WiFi自動再接続
  - MQTT自動再接続
  - タイムアウト保護

- **LEDヘルスインジケーター** (NeoPixel RGB)
  - 紫色：システム起動中
  - 黄色（ゆっくり点滅）：WiFi/MQTT接続中
  - 青色：正常動作
  - 赤色（高速点滅）：エラー検出
  - 緑色（短点灯）：MQTT送信成功

- **堅牢性**
  - ウォッチドッグタイマー（WDT）
  - エラーハンドリング
  - センサー自動復帰
  - 24/7運用対応

## 必要なハードウェア

- **マイコンボード**: M5 Atom S3 Lite
- **センサー**
  - SHT40 (I2C, アドレス: 0x44)
  - BMP280 (I2C, アドレス: 0x76)

## セットアップ手順

### 1. Arduino IDE のセットアップ

#### 1.1 M5Stackボードサポートの追加

1. Arduino IDE を開く
2. **Arduino IDE > Settings** (Mac) / **File > Preferences** (Windows) に移動
3. **Additional Boards Manager URLs** に以下を追加:
   ```
   https://dl.espressif.com/dl/package_esp32_index.json
   https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
   ```
4. **OK** をクリック

#### 1.2 ボードマネージャーからボードをインストール

1. **Tools > Board > Boards Manager** を開く
2. "M5Stack" を検索
3. **M5Stack by M5Stack official** をインストール（バージョン 3.2.5以上）
4. インストール完了後、**Tools > Board** から **M5Stack > M5Stack AtomS3** を選択

#### 1.3 ボード設定の確認

**Tools** メニューで以下を確認:
- **Board**: M5Stack AtomS3
- **Upload Speed**: 921600
- **USB Mode**: Hardware CDC and JTAG
- **CPU Frequency**: 240MHz
- **Flash Size**: 8MB
- **Partition Scheme**: Default 8MB

### 2. 必要なライブラリのインストール

**Sketch > Include Library > Manage Libraries** から以下をインストール:

| ライブラリ | 説明 |
|-----------|------|
| **M5AtomS3** | M5 Atom S3 Lite用コアライブラリ |
| **PubSubClient** | MQTT通信ライブラリ |
| **Sensirion I2C SHT4x** | SHT40温湿度センサドライバ |
| **Adafruit BMP280** | BMP280気圧センサドライバ |
| **FastLED** | NeoPixel RGB LED制御ライブラリ |

**インストール方法:**
1. Manage Libraries を開く
2. 上表のライブラリ名で検索
3. 最新バージョンをインストール

### 3. プロジェクト設定

#### 3.1 設定ファイルの作成

1. `config.example.h` をコピー:
   ```bash
   cp config.example.h config.h
   ```

2. `config.h` を編集して環境に合わせる:
   ```cpp
   // WiFi設定
   const char *ssid = "YOUR_SSID";
   const char *password = "YOUR_PASSWORD";
   
   // MQTTサーバ設定
   #define CONFIG_MQTT_SERVER "192.168.1.100"
   #define CONFIG_MQTT_PORT 1883
   #define CONFIG_MQTT_TOPIC "sensors/env4"
   ```

#### 3.2 配線

| M5 Atom S3 Lite | SHT40 | BMP280 |
|----------------|-------|--------|
| 5V(5) | VCC | VCC |
| GND(GND) | GND | GND |
| G2(SDA) | SDA | SDA |
| G1(SCL) | SCL | SCL |

I2Cアドレス:
- SHT40: 0x44
- BMP280: 0x76

### 4. ビルド・アップロード

1. **Sketch > Verify/Compile** でコンパイル確認
   ```
   Sketch uses 1159210 bytes of program storage space...
   ```

2. **Sketch > Upload** でアップロード
   ```
   Writing at 0x00080000... (100%)
   Wrote 1159210 bytes to file ... checksum ... ok
   ```

3. **Tools > Serial Monitor** でログを確認（ボーレート: 115200）

## 使用方法

### シリアルモニタ出力

ボード起動時:
```
================================================================================
Device: M5 Atom S3 Lite
Firmware: AtomS3Lite Environmental Sensor v1.0.0
Built: Dec 30 2025 10:30:45
================================================================================

[INFO] System startup
[INFO] SHT40 sensor initialized
[INFO] BMP280 sensor initialized
[INFO] Connecting to WiFi
[INFO] WiFi connected
[INFO] Attempting MQTT connection
[INFO] MQTT connected
```

センサーデータ出力（30秒ごと）:
```
[TEMP] 25.50 °C
[HUM] 45.30 %
[PRES] 1013.25 hPa
[INFO] MQTT publish successful
```

### MQTT トピック

**トピック**: `sensors/env4`

**ペイロード** (JSON):
```json
{
  "temperature": 25.50,
  "humidity": 45.30,
  "pressure": 1013.25
}
```

## トラブルシューティング

### ライブラリが見つからないエラー

```
fatal error: M5AtomS3.h: No such file or directory
```

**解決策:**
1. M5Stackボードマネージャーがインストール済みか確認
2. **Tools > Board** で **M5Stack AtomS3** が選択されているか確認
3. Arduino IDEを再起動

### WiFi接続失敗

```
[WARN] WiFi connection failed
```

**確認事項:**
- `config.h` の `ssid` と `password` が正しいか
- WiFiルーターが2.4GHz対応か（5GHzのみ非対応）
- WiFi信号範囲内か

### MQTT接続失敗

```
[WARN] MQTT connection timeout
```

**確認事項:**
- `config.h` の `CONFIG_MQTT_SERVER` と `CONFIG_MQTT_PORT` が正しいか
- MQTTサーバーが起動しているか
- ファイアウォール設定を確認

### センサー認識失敗

```
[WARN] SHT40 sensor initialization failed
[WARN] BMP280 sensor initialization failed
```

**確認事項:**
- I2C配線を確認（SDA: G2, SCL: G1）
- センサーのI2Cアドレスを確認（SHT40: 0x44, BMP280: 0x76）
- `config.h` のアドレス定義を確認

### I2Cアドレス確認

Arduino IDEのシリアルモニタで以下を実行:
```cpp
Wire.begin(2, 1, 100000);  // SDA=G2, SCL=G1, 100kHz
for(int addr = 1; addr < 127; addr++) {
  Wire.beginTransmission(addr);
  if(Wire.endTransmission() == 0) {
    Serial.print("Found at 0x"); Serial.println(addr, HEX);
  }
}
```

## ファイル構成

```
atomS3Lite_w_env4/
├── README.md                      # 英語ドキュメント
├── README-ja.md                   # 日本語ドキュメント
├── config.example.h               # 設定テンプレート
├── config.h                       # 環境固有設定（.gitignoreで除外）
├── atomS3Lite_w_env4.ino         # メインスケッチ
└── .gitignore                     # Git除外ファイル
```

## 設定のカスタマイズ

`config.h` で以下がカスタマイズ可能:

| 項目 | デフォルト | 説明 |
|------|----------|------|
| `ssid` | - | WiFiネットワーク名 |
| `password` | - | WiFiパスワード |
| `CONFIG_MQTT_SERVER` | 192.168.3.82 | MQTTサーバIP |
| `CONFIG_MQTT_PORT` | 1883 | MQTTサーバポート |
| `CONFIG_MQTT_TOPIC` | env4 | 送信トピック |
| `CONFIG_WIFI_TIMEOUT` | 30000ms | WiFi接続タイムアウト |
| `CONFIG_WIFI_RECONNECT_INTERVAL` | 10000ms | WiFi再接続試行間隔 |
| `CONFIG_MQTT_TIMEOUT` | 10000ms | MQTT接続タイムアウト |
| `CONFIG_PUBLISH_INTERVAL` | 30000ms | データ送信間隔 |
| `CONFIG_SENSOR_REINIT_INTERVAL` | 300000ms | センサー再初期化間隔 |

詳細は `config.example.h` を参照してください。

## ログレベル

| レベル | 説明 |
|-------|------|
| **INFO** | 通常動作情報 |
| **WARN** | 警告・エラー情報 |

## ライセンス

MIT License

## 著者

omiya-bonsai

## 参考資料

- [M5AtomS3 公式ドキュメント](https://docs.m5stack.com/en/core/AtomS3)
- [Arduino ESP32 環境構築](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
- [PubSubClient ドキュメント](https://pubsubclient.knolleary.net/)
