#include <Arduino.h>
#include <DHT.h>
#include <math.h>

// ---------- PINS ----------
#define SOIL_PIN 34
#define LIGHT_PIN 35
#define PH_PIN 32
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define PUMP_LED 25
#define RELAY_PIN 26
#define RED_LED 27
#define GREEN_LED 14
#define BUZZER 13

#define RELAY_ON HIGH
#define RELAY_OFF LOW

DHT dht(DHT_PIN, DHT_TYPE);

// ---------- 7 MAJOR CROPS ----------
struct Crop {
  const char* name;
  const char* type;
  float phMin, phMax;
  int moistureMin;
  float tempMin, tempMax;
  float humidityMin, humidityMax;
  float lightMin, lightMax;
};

const Crop crops[] = {
  {"Rice",      "Grain",   5.5, 7.0, 50, 20, 35, 60, 90, 500, 1000},
  {"Wheat",     "Grain",   6.0, 7.5, 35, 15, 25, 40, 70, 300, 800},
  {"Maize",     "Grain",   5.8, 7.0, 40, 18, 30, 50, 80, 400, 900},
  {"Chickpea",  "Pulse",   6.0, 8.0, 30, 15, 30, 40, 70, 300, 800},
  {"Groundnut", "Oilseed", 6.0, 7.5, 35, 22, 30, 45, 75, 400, 900},
  {"Mustard",   "Oilseed", 6.0, 7.5, 30, 10, 25, 40, 70, 300, 800},
  {"Soybean",   "Pulse",   6.0, 7.0, 40, 20, 30, 50, 80, 400, 900}
};
const byte cropCount = sizeof(crops) / sizeof(crops[0]);

// ---------- STATE ----------
byte cropIndex = 0;
int moisture = 0;
float ph = 7.0, temp = 25.0, hum = 60.0, lux = 500.0;
int moistureSuit = 100, phSuit = 100, tempSuit = 100, humSuit = 100, lightSuit = 100;
int health = 100;
bool alert = false, irrigation = false;
String suggestion = "Conditions are suitable.";
String inputLine = "";
unsigned long lastRead = 0;
unsigned long lastBeep = 0;
const unsigned long READ_MS = 2000;
const unsigned long BEEP_MS = 5000;

// ---------- FUNCTIONS ----------
int scoreRange(float v, float lo, float hi) {
  if (v >= lo && v <= hi) return 100;
  float width = hi - lo;
  float d = (v < lo) ? lo - v : v - hi;
  if (width <= 0) return 0;
  return constrain(100 - (int)((d / width) * 100.0f), 0, 99);
}

int scoreMoisture(int v, int minV) {
  if (v >= minV) return 100;
  return constrain(100 - (minV - v) * 2, 0, 99);
}

int findCrop(String name) {
  name.trim();
  name.toLowerCase();
  for (byte i = 0; i < cropCount; i++) {
    String n = crops[i].name;
    n.toLowerCase();
    if (n == name) return i;
  }
  return -1;
}

void selectCrop(String name) {
  int i = findCrop(name);
  if (i < 0) return;
  cropIndex = i;
  Serial.print(F("CROP_SELECTED:"));
  Serial.println(crops[cropIndex].name);
}

void readSensors() {
  int soilRaw = analogRead(SOIL_PIN);
  int lightRaw = analogRead(LIGHT_PIN);
  int phRaw = analogRead(PH_PIN);

  moisture = map(soilRaw, 0, 4095, 0, 100);
  lux = (lightRaw / 4095.0f) * 1000.0f;
  ph = 4.0f + (phRaw / 4095.0f) * 5.0f;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  temp = isnan(t) ? 25.0f : t;
  hum = isnan(h) ? 60.0f : h;
}

void evaluate() {
  const Crop &c = crops[cropIndex];
  moistureSuit = scoreMoisture(moisture, c.moistureMin);
  phSuit = scoreRange(ph, c.phMin, c.phMax);
  tempSuit = scoreRange(temp, c.tempMin, c.tempMax);
  humSuit = scoreRange(hum, c.humidityMin, c.humidityMax);
  lightSuit = scoreRange(lux, c.lightMin, c.lightMax);
  health = (moistureSuit + phSuit + tempSuit + humSuit + lightSuit) / 5;

  alert = moisture < c.moistureMin ||
          ph < c.phMin || ph > c.phMax ||
          temp < c.tempMin || temp > c.tempMax ||
          hum < c.humidityMin || hum > c.humidityMax ||
          lux < c.lightMin || lux > c.lightMax;

  if (moisture < c.moistureMin)
    suggestion = "Moisture low - irrigation needed.";
  else if (moisture > 90)
    suggestion = "Moisture high - check drainage.";
  else if (ph < c.phMin)
    suggestion = "pH low - test soil and review suitable correction.";
  else if (ph > c.phMax)
    suggestion = "pH high - test soil and review suitable correction.";
  else if (temp < c.tempMin)
    suggestion = "Temperature low - protect crop from cold stress.";
  else if (temp > c.tempMax)
    suggestion = "Temperature high - check heat and plant protection.";
  else if (hum < c.humidityMin)
    suggestion = "Humidity low - check irrigation and local microclimate.";
  else if (hum > c.humidityMax)
    suggestion = "Humidity high - improve airflow and check moisture.";
  else if (lux < c.lightMin)
    suggestion = "Light low - check shading and plant location.";
  else if (lux > c.lightMax)
    suggestion = "Light high - check heat and direct exposure.";
  else
    suggestion = "Conditions are suitable.";
}

void outputs() {
  const Crop &c = crops[cropIndex];
  irrigation = moisture < c.moistureMin;
  digitalWrite(RELAY_PIN, irrigation ? RELAY_ON : RELAY_OFF);
  digitalWrite(PUMP_LED, irrigation ? HIGH : LOW);
  digitalWrite(RED_LED, alert ? HIGH : LOW);
  digitalWrite(GREEN_LED, alert ? LOW : HIGH);
}

void beep() {
  if (alert && millis() - lastBeep >= BEEP_MS) {
    lastBeep = millis();
    digitalWrite(BUZZER, HIGH);
    delay(180);
    digitalWrite(BUZZER, LOW);
  }
}

void sendJSON() {
  const Crop &c = crops[cropIndex];
  Serial.print(F("{\"device\":\"ESP32\",\"crop\":\"")); Serial.print(c.name);
  Serial.print(F("\",\"type\":\"")); Serial.print(c.type);
  Serial.print(F("\",\"moisture\":")); Serial.print(moisture);
  Serial.print(F(",\"ph\":")); Serial.print(ph, 2);
  Serial.print(F(",\"temperature\":")); Serial.print(temp, 1);
  Serial.print(F(",\"humidity\":")); Serial.print(hum, 1);
  Serial.print(F(",\"light\":")); Serial.print(lux, 1);
  Serial.print(F(",\"moistureSuitability\":")); Serial.print(moistureSuit);
  Serial.print(F(",\"phSuitability\":")); Serial.print(phSuit);
  Serial.print(F(",\"temperatureSuitability\":")); Serial.print(tempSuit);
  Serial.print(F(",\"humiditySuitability\":")); Serial.print(humSuit);
  Serial.print(F(",\"lightSuitability\":")); Serial.print(lightSuit);
  Serial.print(F(",\"health\":")); Serial.print(health);
  Serial.print(F(",\"alert\":")); Serial.print(alert ? "true" : "false");
  Serial.print(F(",\"irrigation\":")); Serial.print(irrigation ? "true" : "false");
  Serial.print(F(",\"suggestion\":\"")); Serial.print(suggestion);
  Serial.println(F("\"}"));
}

void readCommands() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (!inputLine.length()) continue;
      String line = inputLine;
      inputLine = "";
      line.trim();

      // Dashboard/bridge formats: {"crop":"Rice"} or CROP:Rice or Rice
      int key = line.indexOf("\"crop\"");
      if (key >= 0) {
        int colon = line.indexOf(':', key);
        int q1 = line.indexOf('"', colon + 1);
        int q2 = q1 >= 0 ? line.indexOf('"', q1 + 1) : -1;
        if (q1 >= 0 && q2 > q1) {
          selectCrop(line.substring(q1 + 1, q2));
          continue;
        }
      }
      if (line.startsWith("CROP:") || line.startsWith("crop:")) {
        selectCrop(line.substring(5));
        continue;
      }
      if (findCrop(line) >= 0) selectCrop(line);
    } else if (isPrintable(ch) && inputLine.length() < 120) {
      inputLine += ch;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PUMP_LED, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(PUMP_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BUZZER, LOW);

  analogReadResolution(12);
  dht.begin();

  Serial.println(F("================================"));
  Serial.println(F("       AGRISENSE ESP32          "));
  Serial.println(F("================================"));
  Serial.println(F("Live JSON starts below."));
  Serial.println(F("Crop commands: {\"crop\":\"Rice\"}"));
  Serial.print(F("Default crop: ")); Serial.println(crops[cropIndex].name);

  readSensors();
  evaluate();
  outputs();
  sendJSON();
}

void loop() {
  readCommands();

  if (millis() - lastRead >= READ_MS) {
    lastRead = millis();
    readSensors();
    evaluate();
    outputs();
    beep();
    sendJSON();
  }
}
