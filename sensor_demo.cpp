#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>

// Pin assignments (Feather M4)
const uint8_t DS_HOT_PIN = 5;    // hot end temperature sensor
const uint8_t DS_COLD_PIN = 6;   // cold end temperature sensor
const uint8_t DS_MID_PIN = 9;    // middle temperature sensor
const uint8_t LIGHT_PIN = A2;    // light sensor (ALS-PT19)

// OneWire sensor setup
OneWire oneWireHot(DS_HOT_PIN);
OneWire oneWireCold(DS_COLD_PIN);
OneWire oneWireMid(DS_MID_PIN);

DallasTemperature sensorsHot(&oneWireHot);
DallasTemperature sensorsCold(&oneWireCold);
DallasTemperature sensorsMid(&oneWireMid);

// I2C humidity/temperature sensor
Adafruit_SHT4x sht4 = Adafruit_SHT4x();

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 2000; // Read every 2 seconds

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Initialize temperature sensors
  sensorsHot.begin();
  sensorsCold.begin();
  sensorsMid.begin();
  
  // Initialize I2C and humidity sensor
  Wire.begin();
  if (!sht4.begin()) {
    Serial.println("ERROR: SHT4x not found!");
  } else {
    Serial.println("SHT4x initialized OK");
  }
  
  Serial.println("=== Sensor Monitor Started ===");
  Serial.println("Reading: Hot°C | Mid°C | Cold°C | Air_Temp°C | Air_Humidity% | Light_Raw");
}

void loop() {
  unsigned long now = millis();
  
  // Only read sensors every READ_INTERVAL milliseconds
  if (now - lastRead < READ_INTERVAL) {
    return;
  }
  lastRead = now;
  
  // Request temperature conversions from all DS18B20 sensors
  sensorsHot.requestTemperatures();
  sensorsMid.requestTemperatures();
  sensorsCold.requestTemperatures();
  
  // Read DS18B20 temperatures (in Celsius)
  float tempHot = sensorsHot.getTempCByIndex(0);
  float tempMid = sensorsMid.getTempCByIndex(0);
  float tempCold = sensorsCold.getTempCByIndex(0);
  
  // Read SHT4x humidity and temperature
  sensors_event_t humidity, temp_event;
  float airTemp = NAN;
  float airHumidity = NAN;
  
  if (sht4.getEvent(&humidity, &temp_event)) {
    airTemp = temp_event.temperature;
    airHumidity = humidity.relative_humidity;
  }
  
  // Read light sensor (0-4095 for 12-bit ADC on Feather M4)
  int lightRaw = analogRead(LIGHT_PIN);
  float lightPercent = (100.0 * lightRaw) / 4095.0;
  
  // Print all sensor readings
  Serial.print("Temps: ");
  Serial.print(tempHot, 1); Serial.print("°C | ");
  Serial.print(tempMid, 1); Serial.print("°C | ");
  Serial.print(tempCold, 1); Serial.print("°C  |  ");
  
  Serial.print("Air: ");
  if (!isnan(airTemp)) {
    Serial.print(airTemp, 1); Serial.print("°C / ");
  } else {
    Serial.print("N/A°C / ");
  }
  if (!isnan(airHumidity)) {
    Serial.print(airHumidity, 1); Serial.print("% | ");
  } else {
    Serial.print("N/A% | ");
  }
  
  Serial.print("Light: ");
  Serial.print(lightRaw);
  Serial.print(" (");
  Serial.print(lightPercent, 1);
  Serial.println("%)");
}
