#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "pins.h"
#include "sensor_logic.h"

#ifndef ENABLE_TEMP_SENSOR_TO_92
#define ENABLE_TEMP_SENSOR_TO_92 0
#endif

#ifndef ENABLE_SOIL_MOISTURE_PROBE_PRONE
#define ENABLE_SOIL_MOISTURE_PROBE_PRONE 0
#endif

#ifndef ENABLE_SOIL_MOISTURE_CAPACITIVE
#define ENABLE_SOIL_MOISTURE_CAPACITIVE 0
#endif

#ifndef SENSOR_TYPE_NAME
#define SENSOR_TYPE_NAME "UNSPECIFIED_NODE"
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "unknown-device"
#endif

#ifndef SENSOR_SEND_INTERVAL_MS
#define SENSOR_SEND_INTERVAL_MS 10000UL
#endif

#ifndef TEMP_SENSOR_TO_92_OUTPUT_NAME
#define TEMP_SENSOR_TO_92_OUTPUT_NAME "temp_sensor_TO-92"
#endif

#ifndef SOIL_MOISTURE_PROBE_PRONE_OUTPUT_NAME
#define SOIL_MOISTURE_PROBE_PRONE_OUTPUT_NAME "ENABLE_SOIL_MOISTURE_PROBE_PRONE"
#endif

#ifndef SOIL_MOISTURE_CAPACITIVE_OUTPUT_NAME
#define SOIL_MOISTURE_CAPACITIVE_OUTPUT_NAME "soil_moisture_capacitive"
#endif

#ifndef SOIL_MOISTURE_PROBE_1_AIR_VALUE
#define SOIL_MOISTURE_PROBE_1_AIR_VALUE 3500
#endif

#ifndef SOIL_MOISTURE_PROBE_1_WATER_VALUE
#define SOIL_MOISTURE_PROBE_1_WATER_VALUE 100
#endif

#ifndef SOIL_MOISTURE_PROBE_2_AIR_VALUE
#define SOIL_MOISTURE_PROBE_2_AIR_VALUE 3480
#endif

#ifndef SOIL_MOISTURE_PROBE_2_WATER_VALUE
#define SOIL_MOISTURE_PROBE_2_WATER_VALUE 50
#endif

#ifndef SOIL_MOISTURE_CAPACITIVE_AIR_VALUE
#define SOIL_MOISTURE_CAPACITIVE_AIR_VALUE 3200
#endif

#ifndef SOIL_MOISTURE_CAPACITIVE_WATER_VALUE
#define SOIL_MOISTURE_CAPACITIVE_WATER_VALUE 1300
#endif

#if ENABLE_TEMP_SENSOR_TO_92
#include <Adafruit_SHT31.h>
#include <Wire.h>
#endif

#if !ENABLE_TEMP_SENSOR_TO_92 && !ENABLE_SOIL_MOISTURE_PROBE_PRONE && !ENABLE_SOIL_MOISTURE_CAPACITIVE
#error "No sensors are enabled. Define at least one of ENABLE_TEMP_SENSOR_TO_92, ENABLE_SOIL_MOISTURE_PROBE_PRONE, or ENABLE_SOIL_MOISTURE_CAPACITIVE in the active PlatformIO environment."
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Create platformio.secrets.ini from the example file."
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD is not defined. Create platformio.secrets.ini from the example file."
#endif

#ifndef SENSOR_API_ENDPOINT
#error "SENSOR_API_ENDPOINT is not defined. Create platformio.secrets.ini from the example file."
#endif

#ifndef SENSOR_API_TOKEN
#error "SENSOR_API_TOKEN is not defined. Create platformio.secrets.ini from the example file."
#endif

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* sensorApiEndpoint = SENSOR_API_ENDPOINT;
const char* sensorApiToken = SENSOR_API_TOKEN;
const char* sensorTypeName = SENSOR_TYPE_NAME;
const char* deviceId = DEVICE_ID;
const char* tempSensorOutputName = TEMP_SENSOR_TO_92_OUTPUT_NAME;
const char* soilMoistureOutputName = SOIL_MOISTURE_PROBE_PRONE_OUTPUT_NAME;
const char* soilMoistureCapacitiveOutputName = SOIL_MOISTURE_CAPACITIVE_OUTPUT_NAME;

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE || ENABLE_SOIL_MOISTURE_CAPACITIVE
// Per-sensor warm-up (ms) between powering on and sampling; override in platformio.ini
#ifndef SOIL_MOISTURE_PROBE_1_WARMUP_MS
  #define SOIL_MOISTURE_PROBE_1_WARMUP_MS 500
#endif
#ifndef SOIL_MOISTURE_PROBE_2_WARMUP_MS
  #define SOIL_MOISTURE_PROBE_2_WARMUP_MS 500
#endif
#ifndef SOIL_MOISTURE_CAPACITIVE_WARMUP_MS
  #define SOIL_MOISTURE_CAPACITIVE_WARMUP_MS 1000
#endif
#endif

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
const int PROBE_1_AIR_VALUE = SOIL_MOISTURE_PROBE_1_AIR_VALUE;
const int PROBE_1_WATER_VALUE = SOIL_MOISTURE_PROBE_1_WATER_VALUE;
const int PROBE_2_AIR_VALUE = SOIL_MOISTURE_PROBE_2_AIR_VALUE;
const int PROBE_2_WATER_VALUE = SOIL_MOISTURE_PROBE_2_WATER_VALUE;
#endif

#if ENABLE_SOIL_MOISTURE_CAPACITIVE
const int CAPACITIVE_AIR_VALUE = SOIL_MOISTURE_CAPACITIVE_AIR_VALUE;
const int CAPACITIVE_WATER_VALUE = SOIL_MOISTURE_CAPACITIVE_WATER_VALUE;
#endif

#if ENABLE_TEMP_SENSOR_TO_92
const uint8_t TEMP_SENSOR_PRIMARY_ADDRESS = 0x44;
const uint8_t TEMP_SENSOR_SECONDARY_ADDRESS = 0x45;
#endif

unsigned long lastSensorSendMillis = 0;

#if ENABLE_TEMP_SENSOR_TO_92
bool tempSensorAvailable = false;
uint8_t tempSensorAddress = TEMP_SENSOR_PRIMARY_ADDRESS;

Adafruit_SHT31 tempSensor = Adafruit_SHT31();
#endif

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE || ENABLE_SOIL_MOISTURE_CAPACITIVE
int soilMoisturePercentFromRaw(int rawValue, int airValue, int waterValue) {
  return calculateMoisturePercent(rawValue, airValue, waterValue);
}

int readPoweredAnalogRaw(int powerPin, int analogPin, unsigned long warmupMs) {
  digitalWrite(powerPin, HIGH);
  delay(warmupMs);
  const int rawValue = analogRead(analogPin);
  digitalWrite(powerPin, LOW);
  return rawValue;
}
#endif

// WiFi TX power level (wifi_power_t); override in platformio.ini. Too high can brown out weak supplies.
#ifndef WIFI_TX_POWER
  #define WIFI_TX_POWER WIFI_POWER_15dBm
#endif

// Reboot if WiFi stays down this long after boot (ms); override in platformio.ini
#ifndef WIFI_LOSS_RESTART_MS
  #define WIFI_LOSS_RESTART_MS 120000UL
#endif
unsigned long wifiLostSinceMillis = 0;  // 0 = WiFi currently up

// True while associated with the AP (before/after DHCP), so a retry never aborts it
volatile bool wifiAssociated = false;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi] Station Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      wifiAssociated = true;
      Serial.println("[WiFi] Connected to Access Point!");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WiFi] Got IP: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiAssociated = false;
      Serial.print("[WiFi] Disconnected! Reason code: ");
      Serial.println(info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

bool initializeTempSensor() {
#if ENABLE_TEMP_SENSOR_TO_92
  Wire.begin(TEMP_SENSOR_TO_92_SDA_PIN, TEMP_SENSOR_TO_92_SCL_PIN);

  if (tempSensor.begin(TEMP_SENSOR_PRIMARY_ADDRESS)) {
    tempSensorAddress = TEMP_SENSOR_PRIMARY_ADDRESS;
    return true;
  }

  if (tempSensor.begin(TEMP_SENSOR_SECONDARY_ADDRESS)) {
    tempSensorAddress = TEMP_SENSOR_SECONDARY_ADDRESS;
    return true;
  }

  return false;
#else
  return false;
#endif
}

void setup() {
  Serial.begin(115200);
  delay(2000); 

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_MOISTURE_PROBE_1_AO_PIN, ADC_11db);
  analogSetPinAttenuation(SOIL_MOISTURE_PROBE_2_AO_PIN, ADC_11db);
#endif

#if ENABLE_SOIL_MOISTURE_CAPACITIVE
  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_MOISTURE_CAPACITIVE_AO_PIN, ADC_11db);
#endif

#if ENABLE_TEMP_SENSOR_TO_92
  tempSensorAvailable = initializeTempSensor();
  if (tempSensorAvailable) {
    Serial.print("[SHT31] Sensor initialized at 0x");
    Serial.println(tempSensorAddress, HEX);
  } else {
    Serial.println("[SHT31] Sensor not found on 0x44 or 0x45.");
  }
#else
  Serial.println("[temp_sensor_TO-92] Disabled by build configuration.");
#endif

  pinMode(LED_PIN, OUTPUT);

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  pinMode(SOIL_MOISTURE_PROBE_1_POWER_PIN, OUTPUT);
  pinMode(SOIL_MOISTURE_PROBE_2_POWER_PIN, OUTPUT);
  digitalWrite(SOIL_MOISTURE_PROBE_1_POWER_PIN, LOW);
  digitalWrite(SOIL_MOISTURE_PROBE_2_POWER_PIN, LOW);
#else
  Serial.println("[Soil Moisture Probe Prone] Disabled by build configuration.");
#endif

#if ENABLE_SOIL_MOISTURE_CAPACITIVE
  pinMode(SOIL_MOISTURE_CAPACITIVE_POWER_PIN, OUTPUT);
  digitalWrite(SOIL_MOISTURE_CAPACITIVE_POWER_PIN, LOW);
#else
  Serial.println("[soil_moisture_capacitive] Disabled by build configuration.");
#endif

  // Register event listener for detailed debugging
  WiFi.onEvent(WiFiEvent);

  // Clean disconnect and set station mode explicitly
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);

  // Critical fixes for ESP32-C3 SuperMini boards:
  // 1. Disable Wi-Fi power saving
  WiFi.setSleep(false);
  // 2. Moderate TX power. Too low (8.5dBm) weakens the directed probe requests
  //    needed to find a hidden SSID, causing reason 201 (NO_AP_FOUND).
  WiFi.setTxPower(WIFI_TX_POWER);
  // 3. Scan all channels and prefer the strongest AP when several share the SSID
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

#if WIFI_SCAN_DIAGNOSTICS
  // A hidden AP appears with an empty SSID but still reports its channel and RSSI
  Serial.println("[WiFi] Scanning nearby networks...");
  const int found = WiFi.scanNetworks(false, true);
  for (int i = 0; i < found; i++) {
    Serial.printf("[WiFi]  %2d: %-24s RSSI %d dBm (%s) ch %d  %s%s\n", i + 1,
                  WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>",
                  WiFi.RSSI(i), rssiQualityLabel(WiFi.RSSI(i)), WiFi.channel(i),
                  WiFi.BSSIDstr(i).c_str(),
                  WiFi.SSID(i) == ssid ? "  <== TARGET" : "");
  }
  WiFi.scanDelete();
#endif

  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Hidden SSID: retry a fresh attempt periodically, restart if it never works.
  const uint32_t ATTEMPT_TIMEOUT_MS = 15000;
  const int MAX_ATTEMPTS = 8;
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (attempt >= MAX_ATTEMPTS) {
      Serial.println("\n[WiFi] Giving up, restarting...");
      ESP.restart();
    }
    attempt++;
    Serial.printf("\n[WiFi] Attempt %d/%d\n", attempt, MAX_ATTEMPTS);
    WiFi.disconnect(true);
    delay(200);
#if defined(WIFI_CHANNEL) && defined(WIFI_BSSID_BYTES)
    // Pinning channel + BSSID avoids relying on a hidden SSID's probe response.
    // If the pinned AP never works (e.g. router replaced), fall back to channel only.
    static const uint8_t wifiBssid[6] = {WIFI_BSSID_BYTES};
    if (attempt <= MAX_ATTEMPTS / 2) {
      WiFi.begin(ssid, password, WIFI_CHANNEL, wifiBssid);
    } else {
      WiFi.begin(ssid, password, WIFI_CHANNEL);
    }
#elif defined(WIFI_CHANNEL)
    // Fixed channel skips the all-channel scan: much more reliable for hidden SSIDs
    WiFi.begin(ssid, password, WIFI_CHANNEL);
#else
    WiFi.begin(ssid, password);
#endif
    uint32_t start = millis();
    // Once associated, keep waiting (up to the IP timeout) for DHCP instead of aborting
    const uint32_t IP_TIMEOUT_MS = 15000;
    uint32_t associatedAt = 0;
    while (WiFi.status() != WL_CONNECTED) {
      if (wifiAssociated) {
        if (associatedAt == 0) associatedAt = millis();
        if (millis() - associatedAt > IP_TIMEOUT_MS) break;
      } else {
        associatedAt = 0;
        if (millis() - start > ATTEMPT_TIMEOUT_MS) break;
      }
      delay(500);
      Serial.print(".");
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }

  digitalWrite(LED_PIN, LOW); 
  Serial.println("\n[WiFi] Connection Complete!");
  Serial.printf("[WiFi] Signal: %d dBm (%s), channel %d, BSSID %s\n",
                WiFi.RSSI(), rssiQualityLabel(WiFi.RSSI()), WiFi.channel(),
                WiFi.BSSIDstr().c_str());

  // Force the ESP32 to query Cloudflare's DNS
  IPAddress dns1(1, 1, 1, 1);
  IPAddress dns2(8, 8, 8, 8);
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
}

bool postSensorData(
  float temperatureC,
  float temperatureF,
  float humidity,
  bool tempSensorConnected,
  int temperatureSensorCount,
  int probe1RawValue,
  int probe1MoisturePercent,
  int probe2RawValue,
  int probe2MoisturePercent,
  int capacitiveRawValue,
  int capacitiveMoisturePercent,
  float batteryVoltage,
  unsigned long timestamp
) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Skipping upload: WiFi is not connected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, sensorApiEndpoint)) {
    Serial.println("[HTTP] Failed to initialize HTTPS client.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-sensor-api-token", sensorApiToken);

  String payload = "{";
  payload += "\"device_id\":\"";
  payload += deviceId;
  payload += "\",";
  payload += "\"sensor_type\":\"";
  payload += sensorTypeName;
  payload += "\",";

#if ENABLE_TEMP_SENSOR_TO_92
  payload += "\"" + String(tempSensorOutputName) + "_temperature\":" + String(temperatureC, 1) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_temperature_c\":" + String(temperatureC, 2) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_temperature_f\":" + String(temperatureF, 2) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_sda_pin\":" + String(TEMP_SENSOR_TO_92_SDA_PIN) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_scl_pin\":" + String(TEMP_SENSOR_TO_92_SCL_PIN) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_i2c_address\":" + String(tempSensorAddress) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_connected\":" + String(tempSensorConnected ? "true" : "false") + ",";
  payload += "\"" + String(tempSensorOutputName) + "_count\":" + String(temperatureSensorCount) + ",";
  payload += "\"" + String(tempSensorOutputName) + "_humidity\":" + String(humidity, 1) + ",";
#endif

  payload += "\"battery_voltage\":" + String(batteryVoltage, 2) + ",";

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  payload += "\"" + String(soilMoistureOutputName) + "_raw_adc\":" + String(probe1RawValue) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_air_value\":" + String(PROBE_1_AIR_VALUE) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_water_value\":" + String(PROBE_1_WATER_VALUE) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_moisture_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_calibrated_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_1_ao_pin\":" + String(SOIL_MOISTURE_PROBE_1_AO_PIN) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_1_raw_adc\":" + String(probe1RawValue) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_1_air_value\":" + String(PROBE_1_AIR_VALUE) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_1_water_value\":" + String(PROBE_1_WATER_VALUE) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_1_moisture_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_1_power_pin\":" + String(SOIL_MOISTURE_PROBE_1_POWER_PIN) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_2_raw_adc\":" + String(probe2RawValue) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_2_air_value\":" + String(PROBE_2_AIR_VALUE) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_2_water_value\":" + String(PROBE_2_WATER_VALUE) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_2_moisture_percent\":" + String(probe2MoisturePercent) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_2_power_pin\":" + String(SOIL_MOISTURE_PROBE_2_POWER_PIN) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_probe_2_ao_pin\":" + String(SOIL_MOISTURE_PROBE_2_AO_PIN) + ",";
  payload += "\"" + String(soilMoistureOutputName) + "_uptime_ms\":" + String(millis()) + ",";
#endif

#if ENABLE_SOIL_MOISTURE_CAPACITIVE
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_raw_adc\":" + String(capacitiveRawValue) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_air_value\":" + String(CAPACITIVE_AIR_VALUE) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_water_value\":" + String(CAPACITIVE_WATER_VALUE) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_moisture_percent\":" + String(capacitiveMoisturePercent) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_percent\":" + String(capacitiveMoisturePercent) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_calibrated_percent\":" + String(capacitiveMoisturePercent) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_power_pin\":" + String(SOIL_MOISTURE_CAPACITIVE_POWER_PIN) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_ao_pin\":" + String(SOIL_MOISTURE_CAPACITIVE_AO_PIN) + ",";
  payload += "\"" + String(soilMoistureCapacitiveOutputName) + "_uptime_ms\":" + String(millis()) + ",";
#endif

  payload += "\"timestamp\":" + String(timestamp);
  payload += "}";

  Serial.println("[HTTP] Posting sensor reading...");
  Serial.println(payload);

  const int responseCode = http.POST(payload);
  if (responseCode <= 0) {
    Serial.print("[HTTP] POST failed: ");
    Serial.println(http.errorToString(responseCode));
    http.end();
    return false;
  }

  Serial.print("[HTTP] Response code: ");
  Serial.println(responseCode);
  Serial.println(http.getString());
  http.end();
  return responseCode >= 200 && responseCode < 300;
}

void sendCurrentSensorReading() {
  // Rate-limit by send attempt time so transient HTTP failures do not trigger 5s retries.
  lastSensorSendMillis = millis();

  const unsigned long timestamp = millis() / 1000;
  int probe1RawValue = -1;
  int probe1MoisturePercent = -1;
  int probe2RawValue = -1;
  int probe2MoisturePercent = -1;
  int capacitiveRawValue = -1;
  int capacitiveMoisturePercent = -1;

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  probe1RawValue = readPoweredAnalogRaw(SOIL_MOISTURE_PROBE_1_POWER_PIN, SOIL_MOISTURE_PROBE_1_AO_PIN, SOIL_MOISTURE_PROBE_1_WARMUP_MS);
  probe1MoisturePercent = soilMoisturePercentFromRaw(probe1RawValue, PROBE_1_AIR_VALUE, PROBE_1_WATER_VALUE);
  probe2RawValue = readPoweredAnalogRaw(SOIL_MOISTURE_PROBE_2_POWER_PIN, SOIL_MOISTURE_PROBE_2_AO_PIN, SOIL_MOISTURE_PROBE_2_WARMUP_MS);
  probe2MoisturePercent = soilMoisturePercentFromRaw(probe2RawValue, PROBE_2_AIR_VALUE, PROBE_2_WATER_VALUE);
#endif

#if ENABLE_SOIL_MOISTURE_CAPACITIVE
  capacitiveRawValue = readPoweredAnalogRaw(SOIL_MOISTURE_CAPACITIVE_POWER_PIN, SOIL_MOISTURE_CAPACITIVE_AO_PIN, SOIL_MOISTURE_CAPACITIVE_WARMUP_MS);
  capacitiveMoisturePercent = soilMoisturePercentFromRaw(capacitiveRawValue, CAPACITIVE_AIR_VALUE, CAPACITIVE_WATER_VALUE);
#endif

  const float batteryVoltage = 0.0f;

  float temperatureC = NAN;
  float temperatureF = NAN;
  float humidity = NAN;
  bool tempSensorConnected = false;
  int discoveredSensorCount = 0;

#if ENABLE_TEMP_SENSOR_TO_92
  if (tempSensorAvailable) {
    temperatureC = tempSensor.readTemperature();
    humidity = tempSensor.readHumidity();
    tempSensorConnected = !isnan(temperatureC) && !isnan(humidity);
  }

  temperatureF = tempSensorConnected ? (temperatureC * 9.0f / 5.0f) + 32.0f : NAN;
  discoveredSensorCount = tempSensorConnected ? 1 : 0;

  if (tempSensorConnected) {
    Serial.print("[temp_sensor_TO-92] Temp: ");
    Serial.print(temperatureC);
    Serial.print(" C | ");
    Serial.print(temperatureF);
    Serial.print(" F | Humidity: ");
    Serial.print(humidity);
    Serial.println("%");
  } else {
    Serial.println("[temp_sensor_TO-92] Could not read temperature/humidity data!");
    temperatureC = -127.0f;
    humidity = -1.0f;
    temperatureF = -196.6f;
  }
#endif

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  Serial.print("[Soil Probe Prone 1] raw_adc=");
  Serial.print(probe1RawValue);
  Serial.print(" air_value=");
  Serial.print(PROBE_1_AIR_VALUE);
  Serial.print(" water_value=");
  Serial.print(PROBE_1_WATER_VALUE);
  Serial.print(" moisture_percent=");
  Serial.print(probe1MoisturePercent);
  Serial.println("%");

  Serial.print("[Soil Probe Prone 2] raw_adc=");
  Serial.print(probe2RawValue);
  Serial.print(" air_value=");
  Serial.print(PROBE_2_AIR_VALUE);
  Serial.print(" water_value=");
  Serial.print(PROBE_2_WATER_VALUE);
  Serial.print(" moisture_percent=");
  Serial.print(probe2MoisturePercent);
  Serial.println("%");
#endif

#if ENABLE_SOIL_MOISTURE_CAPACITIVE
  Serial.print("[soil_moisture_capacitive] raw_adc=");
  Serial.print(capacitiveRawValue);
  Serial.print(" air_value=");
  Serial.print(CAPACITIVE_AIR_VALUE);
  Serial.print(" water_value=");
  Serial.print(CAPACITIVE_WATER_VALUE);
  Serial.print(" moisture_percent=");
  Serial.print(capacitiveMoisturePercent);
  Serial.println("%");
#endif

  postSensorData(temperatureC, temperatureF, humidity, tempSensorConnected, discoveredSensorCount, probe1RawValue, probe1MoisturePercent, probe2RawValue, probe2MoisturePercent, capacitiveRawValue, capacitiveMoisturePercent, batteryVoltage, timestamp);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiLostSinceMillis = 0;
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.print(" dBm (");
    Serial.print(rssiQualityLabel(WiFi.RSSI()));
    Serial.println(")");

    if (lastSensorSendMillis == 0 || millis() - lastSensorSendMillis >= SENSOR_SEND_INTERVAL_MS) {
      sendCurrentSensorReading();
    }
  } else {
    // Give the driver's auto-reconnect a grace period, then reboot to recover
    if (wifiLostSinceMillis == 0) {
      wifiLostSinceMillis = millis();
      if (wifiLostSinceMillis == 0) wifiLostSinceMillis = 1;
      Serial.println("[WiFi] Connection lost, waiting for auto-reconnect...");
    }
    if (shouldRestartForWifiLoss(wifiLostSinceMillis, millis(), WIFI_LOSS_RESTART_MS)) {
      Serial.println("[WiFi] Still down, restarting...");
      Serial.flush();
      ESP.restart();
    }
  }
  delay(5000);
}
