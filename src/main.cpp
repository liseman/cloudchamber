#include <Arduino.h>

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <WiFi.h>
#include <ArduinoHttpClient.h>

// Pin assignments (Feather M4)
const uint8_t DS_HOT_PIN = 5;    // hot end
const uint8_t DS_COLD_PIN = 6;   // cold end
const uint8_t DS_MID_PIN = 9;    // middle
const uint8_t RELAY_HOT_PIN = 10;
const uint8_t RELAY_COLD_PIN = 11;
const uint8_t LIGHT_PIN = A2;    // ALS-PT19
const uint8_t LED_PIN = 13;      // Onboard LED (GPIO 13 on Feather ESP32S3)

// WiFi and Adafruit IO
const char* WIFI_SSID = "cult";
const char* WIFI_PASS = "hereticality";
const char* AIO_KEY = "4c06ce1666504628a241f07107012585";
const char* AIO_USER = "liseman";
const char* FEEDS[] = {"hot", "mid", "cold", "air-temp", "air-humidity", "light"};
const char* FEED_KEYS[] = {"HOT", "MID", "COLD", "AIR_T", "AIR_H", "LIGHT"};
const int NUM_FEEDS = 6;
WiFiClient wifi;
HttpClient client = HttpClient(wifi, "io.adafruit.com", 80);

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 60000; // 1 minute
bool lastSendSuccess = false;

OneWire oneWireHot(DS_HOT_PIN);
OneWire oneWireCold(DS_COLD_PIN);
OneWire oneWireMid(DS_MID_PIN);

DallasTemperature sensorsHot(&oneWireHot);
DallasTemperature sensorsCold(&oneWireCold);
DallasTemperature sensorsMid(&oneWireMid);

Adafruit_SHT4x sht4 = Adafruit_SHT4x();

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 2000;

bool relayHotState = false;
bool relayColdState = false;

void setRelayHot(bool on) {
  relayHotState = on;
  digitalWrite(RELAY_HOT_PIN, on ? HIGH : LOW);
}

void setRelayCold(bool on) {
  relayColdState = on;
  digitalWrite(RELAY_COLD_PIN, on ? HIGH : LOW);
}

void toggleRelayHot() { setRelayHot(!relayHotState); }
void toggleRelayCold() { setRelayCold(!relayColdState); }

void handleSerialCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "RELAY HOT ON") { setRelayHot(true); Serial.println("ACK RELAY HOT ON"); }
  else if (cmd == "RELAY HOT OFF") { setRelayHot(false); Serial.println("ACK RELAY HOT OFF"); }
  else if (cmd == "RELAY HOT TOGGLE") { toggleRelayHot(); Serial.println("ACK RELAY HOT TOGGLE"); }
  else if (cmd == "RELAY COLD ON") { setRelayCold(true); Serial.println("ACK RELAY COLD ON"); }
  else if (cmd == "RELAY COLD OFF") { setRelayCold(false); Serial.println("ACK RELAY COLD OFF"); }
  else if (cmd == "RELAY COLD TOGGLE") { toggleRelayCold(); Serial.println("ACK RELAY COLD TOGGLE"); }
}

void setup() {
  Serial.begin(115200);
  delay(10);
  sensorsHot.begin();
  sensorsCold.begin();
  sensorsMid.begin();

  Wire.begin();
  if (!sht4.begin()) {
    Serial.println("SHT4x: NOT FOUND");
  } else {
    Serial.println("SHT4x: OK");
  }

  pinMode(RELAY_HOT_PIN, OUTPUT);
  pinMode(RELAY_COLD_PIN, OUTPUT);
  setRelayHot(false);
  setRelayCold(false);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Configure ADC for full range on light sensor
  analogSetAttenuation(ADC_11db);  // 0-3.3V range for 12-bit ADC
  analogReadResolution(12);        // 12-bit resolution (0-4095)

  // WiFi connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi failed!");
  }

  Serial.println("READY");
}

void loop() {
  // handle serial commands
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    if (line.length() > 0) handleSerialCommand(line);
  }

  unsigned long now = millis();
  if (now - lastRead < READ_INTERVAL) return;
  lastRead = now;

  sensorsHot.requestTemperatures();
  sensorsMid.requestTemperatures();
  sensorsCold.requestTemperatures();

  float tHot = sensorsHot.getTempCByIndex(0);
  float tMid = sensorsMid.getTempCByIndex(0);
  float tCold = sensorsCold.getTempCByIndex(0);

  sensors_event_t humidity, temp_event;
  float aTemp = NAN, aHum = NAN;
  if (sht4.getEvent(&humidity, &temp_event)) {
    aTemp = temp_event.temperature;
    aHum = humidity.relative_humidity;
  }

  int lightRaw = analogRead(LIGHT_PIN);
  int adcMax = 4095;
  if (lightRaw > adcMax) lightRaw = adcMax;

  // Print a single-line, labeled reading that the host GUI can parse
  Serial.print("SENSORS;");
  Serial.print("HOT:"); Serial.print(tHot, 2); Serial.print(";");
  Serial.print("MID:"); Serial.print(tMid, 2); Serial.print(";");
  Serial.print("COLD:"); Serial.print(tCold, 2); Serial.print(";");
  Serial.print("AIR_T:"); if (!isnan(aTemp)) Serial.print(aTemp,2); else Serial.print("NaN"); Serial.print(";");
  Serial.print("AIR_H:"); if (!isnan(aHum)) Serial.print(aHum,2); else Serial.print("NaN"); Serial.print(";");
  Serial.print("LIGHT:"); Serial.print(lightRaw); Serial.print(";");
  Serial.print("RHOT:"); Serial.print(relayHotState?"ON":"OFF"); Serial.print(";");
  Serial.print("RCOLD:"); Serial.print(relayColdState?"ON":"OFF");
  Serial.println();

  // Send to Adafruit IO every minute
  if (WiFi.status() == WL_CONNECTED && (now - lastSend > SEND_INTERVAL)) {
    lastSend = now;
    lastSendSuccess = true;
    float values[NUM_FEEDS] = {tHot, tMid, tCold, aTemp, aHum, (float)lightRaw};
    for (int i = 0; i < NUM_FEEDS; i++) {
      if (!isnan(values[i])) {
        String url = "/api/v2/" + String(AIO_USER) + "/feeds/" + FEEDS[i] + "/data";
        String postData = String("{\"value\":") + String(values[i], 2) + "}";
        client.beginRequest();
        client.post(url.c_str());
        client.sendHeader("Content-Type", "application/json");
        client.sendHeader("X-AIO-Key", AIO_KEY);
        client.sendHeader("Content-Length", postData.length());
        client.beginBody();
        client.print(postData);
        client.endRequest();
        int status = client.responseStatusCode();
        if (status != 200 && status != 201) {
          Serial.print("[AIO ERROR] Feed "); Serial.print(FEEDS[i]); Serial.print(": "); Serial.println(status);
          lastSendSuccess = false;
        } else {
          Serial.print("[AIO OK] "); Serial.print(FEEDS[i]); Serial.print(" sent: "); Serial.println(values[i], 2);
        }
        // consume response body
        while (client.available()) client.read();
      }
    }
  }

  // Pulse LED green if successful
  static unsigned long ledPulseStart = 0;
  static bool ledOn = false;
  if (WiFi.status() == WL_CONNECTED && lastSendSuccess) {
    if (!ledOn) {
      digitalWrite(LED_PIN, HIGH);
      ledOn = true;
      ledPulseStart = now;
    } else if (now - ledPulseStart > 200) {
      digitalWrite(LED_PIN, LOW);
      ledOn = false;
      ledPulseStart = now + 800; // off for 800ms, on for 200ms
    }
  } else {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
  }
}