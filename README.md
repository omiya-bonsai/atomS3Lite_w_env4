# AtomS3Lite Environmental Sensor

**Language:** [日本語](README-ja.md) | English

An environmental sensor project using M5 Atom S3 Lite with WiFi and MQTT support. Measures temperature, humidity, and atmospheric pressure in real-time and transmits data via MQTT.

## Device Appearance

<div style="display: flex; gap: 20px;">
  <img src="images/IMG_8186.jpeg" alt="Device - Front" width="45%">
  <img src="images/IMG_8187.jpeg" alt="Device - Back" width="45%">
</div>

## Features

- **Sensor Measurement**
  - SHT40 (Temperature & Humidity)
  - BMP280 (Atmospheric Pressure)
  
- **Communication**
  - WiFi auto-reconnection
  - MQTT auto-reconnection
  - Timeout protection

- **LED Health Indicator** (NeoPixel RGB)
  - Purple: System startup
  - Yellow (slow blink): Connecting to WiFi/MQTT
  - Blue: Normal operation
  - Red (fast blink): Error detected
  - Green (flash): MQTT publish successful

- **Robustness**
  - Watchdog Timer (WDT)
  - Error handling
  - Automatic sensor recovery
  - 24/7 operation ready

## Required Hardware

- **Microcontroller Board**: M5 Atom S3 Lite
- **Sensors**
  - SHT40 (I2C, Address: 0x44)
  - BMP280 (I2C, Address: 0x76)

## Setup Instructions

### 1. Arduino IDE Setup

#### 1.1 Add M5Stack Board Support

1. Open Arduino IDE
2. Navigate to **Arduino IDE > Settings** (Mac) / **File > Preferences** (Windows)
3. Add the following URLs to **Additional Boards Manager URLs**:
   ```
   https://dl.espressif.com/dl/package_esp32_index.json
   https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
   ```
4. Click **OK**

#### 1.2 Install Board via Board Manager

1. Open **Tools > Board > Boards Manager**
2. Search for "M5Stack"
3. Install **M5Stack by M5Stack official** (version 3.2.5 or later)
4. After installation, select **M5Stack > M5Stack AtomS3** from **Tools > Board**

#### 1.3 Verify Board Settings

Confirm the following settings in **Tools** menu:
- **Board**: M5Stack AtomS3
- **Upload Speed**: 921600
- **USB Mode**: Hardware CDC and JTAG
- **CPU Frequency**: 240MHz
- **Flash Size**: 8MB
- **Partition Scheme**: Default 8MB

### 2. Install Required Libraries

From **Sketch > Include Library > Manage Libraries**, install the following:

| Library | Description |
|---------|-------------|
| **M5AtomS3** | Core library for M5 Atom S3 Lite |
| **PubSubClient** | MQTT communication library |
| **Sensirion I2C SHT4x** | SHT40 temperature/humidity sensor driver |
| **Adafruit BMP280** | BMP280 atmospheric pressure sensor driver |
| **FastLED** | NeoPixel RGB LED control library |

**Installation Steps:**
1. Open Manage Libraries
2. Search for each library name in the table above
3. Install the latest version

### 3. Project Configuration

#### 3.1 Create Configuration File

1. Copy `config.example.h`:
   ```bash
   cp config.example.h config.h
   ```

2. Edit `config.h` to match your environment:
   ```cpp
   // WiFi settings
   const char *ssid = "YOUR_SSID";
   const char *password = "YOUR_PASSWORD";
   
   // MQTT server settings
   #define CONFIG_MQTT_SERVER "192.168.1.100"
   #define CONFIG_MQTT_PORT 1883
   #define CONFIG_MQTT_TOPIC "sensors/env4"
   ```

#### 3.2 Wiring Diagram

| M5 Atom S3 Lite | SHT40 | BMP280 |
|----------------|-------|--------|
| 5V(5) | VCC | VCC |
| GND(GND) | GND | GND |
| G2(SDA) | SDA | SDA |
| G1(SCL) | SCL | SCL |

I2C Addresses:
- SHT40: 0x44
- BMP280: 0x76

### 4. Build & Upload

1. **Sketch > Verify/Compile** to verify compilation
   ```
   Sketch uses 1159210 bytes of program storage space...
   ```

2. **Sketch > Upload** to upload the firmware
   ```
   Writing at 0x00080000... (100%)
   Wrote 525280 bytes to file ... checksum ... ok
   ```

3. Open **Tools > Serial Monitor** to view logs (Baud rate: 115200)

## Usage

### Serial Monitor Output

On board startup:
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

Sensor data output (every 30 seconds):
```
[TEMP] 25.50 °C
[HUM] 45.30 %
[PRES] 1013.25 hPa
[INFO] MQTT publish successful
```

### MQTT Topic

**Topic**: `sensors/env4`

**Payload** (JSON):
```json
{
  "temperature": 25.50,
  "humidity": 45.30,
  "pressure": 1013.25
}
```

## Troubleshooting

### Library Not Found Error

```
fatal error: M5AtomS3.h: No such file or directory
```

**Solutions:**
1. Verify M5Stack board manager is installed
2. Check that **M5Stack AtomS3** is selected in **Tools > Board**
3. Restart Arduino IDE

### WiFi Connection Failure

```
[WARN] WiFi connection failed
```

**Checklist:**
- Verify `ssid` and `password` in `config.h` are correct
- Confirm router supports 2.4GHz (5GHz only not supported)
- Check if within WiFi signal range

### MQTT Connection Failure

```
[WARN] MQTT connection timeout
```

**Checklist:**
- Verify `CONFIG_MQTT_SERVER` and `CONFIG_MQTT_PORT` in `config.h`
- Confirm MQTT server is running
- Check firewall settings

### Sensor Recognition Failure

```
[WARN] SHT40 sensor initialization failed
[WARN] BMP280 sensor initialization failed
```

**Checklist:**
- Verify I2C wiring (SDA: G2, SCL: G1)
- Check sensor I2C addresses (SHT40: 0x44, BMP280: 0x76)
- Verify address definitions in `config.h`

### I2C Address Discovery

Run the following in Arduino IDE Serial Monitor:
```cpp
Wire.begin(2, 1, 100000);  // SDA=G2, SCL=G1, 100kHz
for(int addr = 1; addr < 127; addr++) {
  Wire.beginTransmission(addr);
  if(Wire.endTransmission() == 0) {
    Serial.print("Found at 0x"); Serial.println(addr, HEX);
  }
}
```

## File Structure

```
atomS3Lite_w_env4/
├── README.md                      # English documentation
├── README-ja.md                   # Japanese documentation
├── config.example.h               # Configuration template
├── config.h                       # Environment-specific settings (excluded from git)
├── atomS3Lite_w_env4.ino         # Main sketch
└── .gitignore                     # Git exclusion rules
```

## Configuration Customization

The following settings can be customized in `config.h`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `ssid` | - | WiFi network name |
| `password` | - | WiFi password |
| `CONFIG_MQTT_SERVER` | 192.168.3.82 | MQTT server IP |
| `CONFIG_MQTT_PORT` | 1883 | MQTT server port |
| `CONFIG_MQTT_TOPIC` | env4 | Publish topic |
| `CONFIG_WIFI_TIMEOUT` | 30000ms | WiFi connection timeout |
| `CONFIG_WIFI_RECONNECT_INTERVAL` | 10000ms | WiFi reconnection attempt interval |
| `CONFIG_MQTT_TIMEOUT` | 10000ms | MQTT connection timeout |
| `CONFIG_PUBLISH_INTERVAL` | 30000ms | Data publication interval |
| `CONFIG_SENSOR_REINIT_INTERVAL` | 300000ms | Sensor reinitialization interval |

See `config.example.h` for more details.

## Log Levels

| Level | Description |
|-------|-------------|
| **INFO** | Normal operation information |
| **WARN** | Warning and error information |

## License

MIT License

## Author

omiya-bonsai

## References

- [M5AtomS3 Official Documentation](https://docs.m5stack.com/en/core/AtomS3)
- [Arduino ESP32 Setup Guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
- [PubSubClient Documentation](https://pubsubclient.knolleary.net/)
