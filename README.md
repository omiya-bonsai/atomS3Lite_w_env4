# AtomS3Lite Environmental Sensor

**Language:** [日本語](README-ja.md) | English

[Integration Hub](../../projects/m5papers3-weather-learning-system/README.md)

An environmental sensor project for **M5 Atom S3 Lite** with **WiFi, NTP, and MQTT** support.  
It measures **temperature, humidity, and atmospheric pressure**, then publishes structured JSON data to MQTT for monitoring, logging, and alerting workflows.

This revision adds:

- **NTP time synchronization**
- **Unix timestamp (`ts`) in MQTT payload**
- **Device ID (`id`) in payload**
- **Publish sequence counter (`seq`)**
- **Uptime (`uptime_s`)**
- **Time validity flag (`time_valid`)**
- Publish is skipped until the device has a valid clock

## Device Appearance

<div style="display: flex; gap: 20px;">
  <img src="images/IMG_8186.jpeg" alt="Device - Front" width="45%">
  <img src="images/IMG_8187.jpeg" alt="Device - Back" width="45%">
</div>

## Features

- **Sensor Measurement**
  - SHT40 (temperature & humidity)
  - BMP280 (atmospheric pressure)

- **Communication**
  - WiFi auto-reconnection
  - MQTT auto-reconnection
  - NTP synchronization at boot
  - Periodic NTP re-sync
  - Timeout protection

- **Structured MQTT Payload**
  - `id`: device identifier
  - `ts`: Unix epoch timestamp
  - `temperature`
  - `humidity`
  - `pressure`
  - `seq`: publish sequence number
  - `uptime_s`: device uptime in seconds
  - `time_valid`: whether the device clock is valid

- **LED Health Indicator** (NeoPixel RGB)
  - Purple: system startup
  - Yellow: WiFi / MQTT connecting
  - Blue: normal operation
  - Red: error detected
  - Green: MQTT publish successful

- **Robustness**
  - Watchdog Timer (WDT)
  - Error handling
  - Automatic sensor recovery
  - 24/7 operation ready

## Required Hardware

- **Microcontroller Board**
  - M5 Atom S3 Lite

- **Sensors**
  - SHT40 (I2C, address `0x44`)
  - BMP280 (I2C, address `0x76`)

## Wiring

| M5 Atom S3 Lite | SHT40 | BMP280 |
|----------------|-------|--------|
| 5V(5)          | VCC   | VCC    |
| GND(GND)       | GND   | GND    |
| G2(SDA)        | SDA   | SDA    |
| G1(SCL)        | SCL   | SCL    |

### I2C Addresses

- SHT40: `0x44`
- BMP280: `0x76`

## Setup Instructions

### 1. Arduino IDE Setup

#### 1.1 Add Board Manager URLs

1. Open Arduino IDE
2. Go to **Arduino IDE > Settings** (Mac) or **File > Preferences** (Windows)
3. Add the following URLs to **Additional Boards Manager URLs**:

   ```text
   https://dl.espressif.com/dl/package_esp32_index.json
   https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
   ```

4. Click **OK**

#### 1.2 Install Board Support

1. Open **Tools > Board > Boards Manager**
2. Search for `M5Stack`
3. Install **M5Stack by M5Stack official** (3.2.5 or later recommended)
4. Select **M5Stack > M5Stack AtomS3** from **Tools > Board**

#### 1.3 Recommended Board Settings

Check the following under **Tools**:

- **Board**: M5Stack AtomS3
- **Upload Speed**: 921600
- **USB Mode**: Hardware CDC and JTAG
- **CPU Frequency**: 240MHz
- **Flash Size**: 8MB
- **Partition Scheme**: Default 8MB

### 2. Install Required Libraries

Install the following from **Sketch > Include Library > Manage Libraries**:

| Library | Description |
|---------|-------------|
| **M5AtomS3** | Core library for M5 Atom S3 Lite |
| **PubSubClient** | MQTT client library |
| **Sensirion I2C SHT4x** | SHT40 driver |
| **Adafruit BMP280** | BMP280 driver |
| **FastLED** | NeoPixel RGB LED control |

### 3. Project Configuration

#### 3.1 Create `config.h`

Copy the example file:

```bash
cp config.example.h config.h
```

Then edit `config.h` to match your environment.

Example:

```cpp
// WiFi settings
const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";

// Device identity
#define CONFIG_DEVICE_ID "env4"
#define CONFIG_MQTT_CLIENT_ID_PREFIX "AtomS3Lite-Env4-"

// MQTT settings
#define CONFIG_MQTT_SERVER "192.168.3.82"
#define CONFIG_MQTT_PORT 1883
#define CONFIG_MQTT_TOPIC "env4"

// Time / NTP settings
#define CONFIG_TZ_INFO "JST-9"
#define CONFIG_NTP_SERVER_1 "ntp.nict.jp"
#define CONFIG_NTP_SERVER_2 "pool.ntp.org"
#define CONFIG_NTP_SERVER_3 "time.google.com"
```

### 4. Build & Upload

1. Verify the sketch using **Sketch > Verify/Compile**
2. Upload using **Sketch > Upload**
3. Open **Tools > Serial Monitor** at **115200 baud**

## How It Works

### Boot Sequence

At startup, the firmware performs the following:

1. Initializes LED, I2C, and sensors
2. Connects to WiFi
3. Configures MQTT
4. Synchronizes time with NTP
5. Starts sensor reading and MQTT publishing

### Time Handling

The device publishes a Unix timestamp in the payload:

- `ts` = seconds since Unix epoch
- `time_valid` = `1` if the device clock is valid, otherwise `0`

To avoid invalid telemetry, the firmware **skips MQTT publish until time is valid**.

### NTP Synchronization Policy

Typical behavior:

- NTP sync at boot
- Re-sync after WiFi recovery
- Periodic re-sync every 24 hours

This is a good balance between:

- timestamp accuracy
- network overhead
- power efficiency

## Serial Monitor Output

### Example: Startup

```text
================================================================================
Device: M5 Atom S3 Lite
Firmware: AtomS3Lite Environmental Sensor v1.1.0
Built: Mar 22 2026 12:34:56
================================================================================

[INFO] System startup
[INFO] SHT40 sensor initialized
[INFO] BMP280 sensor initialized
[INFO] Connecting to WiFi
[INFO] WiFi connected
[INFO] Starting NTP sync
[INFO] NTP sync successful
[INFO] Local time: 2026-03-22 12:35:10
[INFO] Attempting MQTT connection
[INFO] MQTT connected
```

### Example: Sensor Read + Publish

```text
[TEMP] 25.50 °C
[HUM] 45.30 %
[PRES] 1013.25 hPa
[MQTT] {"id":"env4","ts":1774150510,"temperature":25.50,"humidity":45.30,"pressure":1013.25,"seq":12,"uptime_s":365,"time_valid":1}
[INFO] MQTT publish successful
```

## MQTT Topic and Payload

### Topic

```text
env4
```

### Payload Example

```json
{
  "id": "env4",
  "ts": 1774150510,
  "temperature": 25.50,
  "humidity": 45.30,
  "pressure": 1013.25,
  "seq": 12,
  "uptime_s": 365,
  "time_valid": 1
}
```

### Field Definitions

| Field | Type | Description |
|------|------|-------------|
| `id` | string | Device identifier |
| `ts` | integer | Unix epoch timestamp |
| `temperature` | number | Temperature in °C |
| `humidity` | number | Relative humidity in % |
| `pressure` | number | Atmospheric pressure in hPa |
| `seq` | integer | Publish sequence number |
| `uptime_s` | integer | Device uptime in seconds |
| `time_valid` | integer | `1` if time is valid, otherwise `0` |

## Why the Extra Payload Fields Matter

The older payload format only contained sensor values. That was enough for display, but weak for monitoring.

The revised payload improves:

- **alerting**
  - determine when a condition actually occurred

- **logging**
  - preserve event timing accurately

- **debugging**
  - detect device reboot or dropped publishes

- **data quality**
  - reject invalid time data when NTP has not completed yet

## Monitoring Example

You can inspect the MQTT output with:

```bash
mosquitto_sub -h 192.168.3.82 -t "env4" -v
```

Example output:

```text
env4 {"id":"env4","ts":1774150510,"temperature":25.50,"humidity":45.30,"pressure":1013.25,"seq":12,"uptime_s":365,"time_valid":1}
```

## Troubleshooting

### Library Not Found

```text
fatal error: M5AtomS3.h: No such file or directory
```

**Checklist**

1. Confirm M5Stack board support is installed
2. Confirm **M5Stack AtomS3** is selected
3. Restart Arduino IDE

### WiFi Connection Failure

```text
[WARN] WiFi connection failed
```

**Checklist**

- Verify `ssid` and `password` in `config.h`
- Use a 2.4GHz WiFi network
- Check signal strength and range

### MQTT Connection Failure

```text
[WARN] MQTT connection timeout
```

**Checklist**

- Verify `CONFIG_MQTT_SERVER`
- Verify `CONFIG_MQTT_PORT`
- Confirm MQTT broker is running
- Check firewall or LAN isolation settings

### No MQTT Publish Even Though Sensors Work

Possible reason:

- NTP has not succeeded yet
- `time_valid` is still false
- publish is intentionally skipped until clock validity is established

Check Serial Monitor for:

```text
[WARN] Time not valid yet, skipping publish
```

### Wrong or Zero Timestamp

If `ts` is invalid, check:

- WiFi connection
- NTP server reachability
- timezone / NTP settings in `config.h`

### Sensor Detection Failure

```text
[WARN] BMP280 sensor initialization failed
```

**Checklist**

- Check I2C wiring
- Check power and ground
- Verify I2C addresses
- Confirm the BMP280 breakout is actually BMP280, not BME280 or another variant

### I2C Address Scan

Use this sketch snippet to scan I2C addresses:

```cpp
Wire.begin(2, 1, 100000);
for (int addr = 1; addr < 127; addr++) {
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() == 0) {
    Serial.print("Found at 0x");
    Serial.println(addr, HEX);
  }
}
```

## File Structure

```text
atomS3Lite_w_env4/
├── README.md
├── README-ja.md
├── config.example.h
├── config.h
├── atomS3Lite_w_env4.ino
└── .gitignore
```

## Main Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `CONFIG_DEVICE_ID` | `env4` | Device ID included in payload |
| `CONFIG_MQTT_SERVER` | `192.168.3.82` | MQTT broker address |
| `CONFIG_MQTT_PORT` | `1883` | MQTT broker port |
| `CONFIG_MQTT_TOPIC` | `env4` | MQTT publish topic |
| `CONFIG_PUBLISH_INTERVAL` | `30000` | Publish interval in ms |
| `CONFIG_WIFI_TIMEOUT` | `30000` | WiFi connect timeout in ms |
| `CONFIG_WIFI_RECONNECT_INTERVAL` | `10000` | WiFi reconnect interval in ms |
| `CONFIG_MQTT_TIMEOUT` | `10000` | MQTT connect timeout in ms |
| `CONFIG_SENSOR_REINIT_INTERVAL` | `300000` | Sensor reinit interval in ms |
| `CONFIG_NTP_SYNC_TIMEOUT_MS` | `15000` | NTP sync timeout in ms |
| `CONFIG_NTP_RESYNC_INTERVAL_MS` | `86400000` | NTP re-sync interval in ms |
| `CONFIG_JSON_PAYLOAD_SIZE` | `192` | MQTT payload buffer size |

## Notes

- `ts` is Unix epoch time.
- `seq` increases after each successful publish.
- `uptime_s` is based on `millis()`.
- `time_valid=1` means the internal clock passed the validity threshold.

## License

MIT License

## Author

omiya-bonsai

## References

- [M5AtomS3 Official Documentation](https://docs.m5stack.com/en/core/AtomS3)
- [Arduino ESP32 Installation Guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
- [PubSubClient Documentation](https://pubsubclient.knolleary.net/)
