#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

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

const bool USE_DUMMY_SENSOR_DATA = false;
const unsigned long SENSOR_SEND_INTERVAL_MS = 60000;
const char* DEVICE_ID = "esp32-c3-garden-01";

const int LED_PIN = 8;
const int SOIL_MOISTURE_PIN = 0;
const int AIR_VALUE = 3200;
const int WATER_VALUE = 1500;
unsigned long lastSensorSendMillis = 0;

int soilMoisturePercentFromRaw(int rawValue) {
  int percent = map(rawValue, AIR_VALUE, WATER_VALUE, 0, 100);
  return constrain(percent, 0, 100);
}

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

void setup() {
  Serial.begin(115200);
  delay(2000); 
  pinMode(LED_PIN, OUTPUT);

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

bool postSensorData(float temperature, float humidity, float batteryVoltage, float moisturePercent, unsigned long timestamp) {
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
  payload += DEVICE_ID;
  payload += "\",";
  payload += "\"temperature\":" + String(temperature, 1) + ",";
  payload += "\"humidity\":" + String(humidity, 1) + ",";
  payload += "\"battery_voltage\":" + String(batteryVoltage, 2) + ",";
  payload += "\"moisture_percent\":" + String(moisturePercent, 1) + ",";
  payload += "\"soil_moisture_percent\":" + String(moisturePercent, 1) + ",";
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
  const int rawValue = analogRead(SOIL_MOISTURE_PIN);
  const int moisturePercent = soilMoisturePercentFromRaw(rawValue);
  const float temperature = 0.0f;
  const float humidity = static_cast<float>(moisturePercent);
  const float batteryVoltage = 0.0f;

  Serial.print("[Soil Sensor] raw_adc=");
  Serial.print(rawValue);
  Serial.print(" moisture_percent=");
  Serial.print(moisturePercent);
  Serial.println("%");

  if (postSensorData(temperature, humidity, batteryVoltage, static_cast<float>(moisturePercent), timestamp)) {
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
