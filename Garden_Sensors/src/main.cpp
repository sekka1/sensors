#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "pins.h"
#include "sensor_logic.h"

#ifndef ENABLE_SHT31
#define ENABLE_SHT31 0
#endif

#ifndef ENABLE_SOIL_MOISTURE_PROBE_PRONE
#define ENABLE_SOIL_MOISTURE_PROBE_PRONE 0
#endif

#ifndef SENSOR_TYPE_NAME
#define SENSOR_TYPE_NAME "UNSPECIFIED_NODE"
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "unknown-device"
#endif

#if ENABLE_SHT31
#include <Adafruit_SHT31.h>
#include <Wire.h>
#endif

#if !ENABLE_SHT31 && !ENABLE_SOIL_MOISTURE_PROBE_PRONE
#error "No sensors are enabled. Define at least one of ENABLE_SHT31 or ENABLE_SOIL_MOISTURE_PROBE_PRONE in the active PlatformIO environment."
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

const unsigned long SENSOR_SEND_INTERVAL_MS = 10000;

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
const unsigned long SOIL_MOISTURE_WARMUP_MS = 50;
const int AIR_VALUE = 3500;
const int WATER_VALUE = 100;
#endif

#if ENABLE_SHT31
const uint8_t TEMP_SENSOR_PRIMARY_ADDRESS = 0x44;
const uint8_t TEMP_SENSOR_SECONDARY_ADDRESS = 0x45;
#endif

unsigned long lastSensorSendMillis = 0;

#if ENABLE_SHT31
bool tempSensorAvailable = false;
uint8_t tempSensorAddress = TEMP_SENSOR_PRIMARY_ADDRESS;

Adafruit_SHT31 tempSensor = Adafruit_SHT31();
#endif

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
int soilMoisturePercentFromRaw(int rawValue) {
  return calculateMoisturePercent(rawValue, AIR_VALUE, WATER_VALUE);
}

int readSoilMoistureProbeRaw(int powerPin, int analogPin) {
  digitalWrite(powerPin, HIGH);
  delay(SOIL_MOISTURE_WARMUP_MS);
  const int rawValue = analogRead(analogPin);
  digitalWrite(powerPin, LOW);
  return rawValue;
}
#endif

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi] Station Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi] Connected to Access Point!");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WiFi] Got IP: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.print("[WiFi] Disconnected! Reason code: ");
      Serial.println(info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

bool initializeTempSensor() {
#if ENABLE_SHT31
  Wire.begin(TEMP_SENSOR_SDA_PIN, TEMP_SENSOR_SCL_PIN);

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

#if ENABLE_SHT31
  tempSensorAvailable = initializeTempSensor();
  if (tempSensorAvailable) {
    Serial.print("[SHT31] Sensor initialized at 0x");
    Serial.println(tempSensorAddress, HEX);
  } else {
    Serial.println("[SHT31] Sensor not found on 0x44 or 0x45.");
  }
#else
  Serial.println("[SHT31] Disabled by build configuration.");
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

  // Register event listener for detailed debugging
  WiFi.onEvent(WiFiEvent);

  // Clean disconnect and set station mode explicitly
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);

  // Critical fixes for ESP32-C3 SuperMini boards:
  // 1. Disable Wi-Fi power saving
  WiFi.setSleep(false); 
  // 2. Reduce TX power slightly to prevent RF power spikes on small ceramic antennas
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 

  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); 
  }

  digitalWrite(LED_PIN, LOW); 
  Serial.println("\n[WiFi] Connection Complete!");
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

#if ENABLE_SHT31
  payload += "\"temperature\":" + String(temperatureC, 1) + ",";
  payload += "\"temperature_c\":" + String(temperatureC, 2) + ",";
  payload += "\"temperature_f\":" + String(temperatureF, 2) + ",";
  payload += "\"temperature_sensor_sda_pin\":" + String(TEMP_SENSOR_SDA_PIN) + ",";
  payload += "\"temperature_sensor_scl_pin\":" + String(TEMP_SENSOR_SCL_PIN) + ",";
  payload += "\"temperature_sensor_i2c_address\":" + String(tempSensorAddress) + ",";
  payload += "\"temperature_sensor_connected\":" + String(tempSensorConnected ? "true" : "false") + ",";
  payload += "\"temperature_sensor_count\":" + String(temperatureSensorCount) + ",";
  payload += "\"humidity\":" + String(humidity, 1) + ",";
#endif

  payload += "\"battery_voltage\":" + String(batteryVoltage, 2) + ",";

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  payload += "\"moisture_sensor_raw_adc\":" + String(probe1RawValue) + ",";
  payload += "\"moisture_sensor_air_value\":" + String(AIR_VALUE) + ",";
  payload += "\"moisture_sensor_water_value\":" + String(WATER_VALUE) + ",";
  payload += "\"moisture_sensor_moisture_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"moisture_sensor_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"moisture_sensor_calibrated_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"moisture_sensor_pin\":" + String(SOIL_MOISTURE_PROBE_1_AO_PIN) + ",";
  payload += "\"moisture_probe_1_raw_adc\":" + String(probe1RawValue) + ",";
  payload += "\"moisture_probe_1_moisture_percent\":" + String(probe1MoisturePercent) + ",";
  payload += "\"moisture_probe_1_power_pin\":" + String(SOIL_MOISTURE_PROBE_1_POWER_PIN) + ",";
  payload += "\"moisture_probe_1_ao_pin\":" + String(SOIL_MOISTURE_PROBE_1_AO_PIN) + ",";
  payload += "\"moisture_probe_2_raw_adc\":" + String(probe2RawValue) + ",";
  payload += "\"moisture_probe_2_moisture_percent\":" + String(probe2MoisturePercent) + ",";
  payload += "\"moisture_probe_2_power_pin\":" + String(SOIL_MOISTURE_PROBE_2_POWER_PIN) + ",";
  payload += "\"moisture_probe_2_ao_pin\":" + String(SOIL_MOISTURE_PROBE_2_AO_PIN) + ",";
  payload += "\"moisture_sensor_reading_time_ms\":" + String(millis()) + ",";
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
  const unsigned long timestamp = millis() / 1000;
  int probe1RawValue = -1;
  int probe1MoisturePercent = -1;
  int probe2RawValue = -1;
  int probe2MoisturePercent = -1;

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  probe1RawValue = readSoilMoistureProbeRaw(SOIL_MOISTURE_PROBE_1_POWER_PIN, SOIL_MOISTURE_PROBE_1_AO_PIN);
  probe1MoisturePercent = soilMoisturePercentFromRaw(probe1RawValue);
  probe2RawValue = readSoilMoistureProbeRaw(SOIL_MOISTURE_PROBE_2_POWER_PIN, SOIL_MOISTURE_PROBE_2_AO_PIN);
  probe2MoisturePercent = soilMoisturePercentFromRaw(probe2RawValue);
#endif

  const float batteryVoltage = 0.0f;

  float temperatureC = NAN;
  float temperatureF = NAN;
  float humidity = NAN;
  bool tempSensorConnected = false;
  int discoveredSensorCount = 0;

#if ENABLE_SHT31
  if (tempSensorAvailable) {
    temperatureC = tempSensor.readTemperature();
    humidity = tempSensor.readHumidity();
    tempSensorConnected = !isnan(temperatureC) && !isnan(humidity);
  }

  temperatureF = tempSensorConnected ? (temperatureC * 9.0f / 5.0f) + 32.0f : NAN;
  discoveredSensorCount = tempSensorConnected ? 1 : 0;

  if (tempSensorConnected) {
    Serial.print("[SHT31] Temp: ");
    Serial.print(temperatureC);
    Serial.print(" C | ");
    Serial.print(temperatureF);
    Serial.print(" F | Humidity: ");
    Serial.print(humidity);
    Serial.println("%");
  } else {
    Serial.println("[SHT31] Could not read temperature/humidity data!");
    temperatureC = -127.0f;
    humidity = -1.0f;
    temperatureF = -196.6f;
  }
#endif

#if ENABLE_SOIL_MOISTURE_PROBE_PRONE
  Serial.print("[Soil Probe Prone 1] raw_adc=");
  Serial.print(probe1RawValue);
  Serial.print(" air_value=");
  Serial.print(AIR_VALUE);
  Serial.print(" water_value=");
  Serial.print(WATER_VALUE);
  Serial.print(" moisture_percent=");
  Serial.print(probe1MoisturePercent);
  Serial.println("%");

  Serial.print("[Soil Probe Prone 2] raw_adc=");
  Serial.print(probe2RawValue);
  Serial.print(" air_value=");
  Serial.print(AIR_VALUE);
  Serial.print(" water_value=");
  Serial.print(WATER_VALUE);
  Serial.print(" moisture_percent=");
  Serial.print(probe2MoisturePercent);
  Serial.println("%");
#endif

  if (postSensorData(temperatureC, temperatureF, humidity, tempSensorConnected, discoveredSensorCount, probe1RawValue, probe1MoisturePercent, probe2RawValue, probe2MoisturePercent, batteryVoltage, timestamp)) {
    lastSensorSendMillis = millis();
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    if (lastSensorSendMillis == 0 || millis() - lastSensorSendMillis >= SENSOR_SEND_INTERVAL_MS) {
      sendCurrentSensorReading();
    }
  }
  delay(5000);
}
