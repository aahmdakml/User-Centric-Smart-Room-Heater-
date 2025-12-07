/*
  Room Temperature Controller (ESP32 side, API only)
  - ESP32
  - DHT11
  - LCD 16x2 I2C
  - 4-channel relay
    CH1: empty (optional, future use)
    CH2: PTC heater
    CH3: fan 1
    CH4: fan 2

  Fitur:
  - Target suhu 26°C (bisa diubah di konstanta TARGET_TEMP).
  - Jika panas (di atas target + hysteresis) → fan1 & fan2 ON, heater OFF.
  - Jika dingin (di bawah target - hysteresis) → heater ON, fan OFF.
  - LCD 16x2 menampilkan suhu, kelembapan, mode (AUTO/MAN), dan status relay.
  - REST API (JSON):
      GET /status
      GET /relay?ch=2&state=1
  - Manual mode (prioritas user): setiap user mengklik tombol di web, mode berubah ke MANUAL.
    Setelah tidak ada aktivitas user selama MANUAL_TIMEOUT_MS → balik AUTO.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHTesp.h>
#include <LiquidCrystal_I2C.h>

// ================== WIFI CONFIG ==================
// GANTI SSID & PASSWORD SESUAI JARINGANMU
const char* ssid     = "SSiD";
const char* password = "PASS";

// ================== PIN CONFIG ===================
const int DHT_PIN   = 19;   // pin data DHT11

// NOTE: CH1 pakai GPIO0 → hati-hati, ini strapping pin boot.
// Kalau nanti sering gagal boot, pindahkan ke GPIO lain (misal 23).
const int RELAY_CH1 = 0;
const int RELAY_CH2 = 14;  // PTC Heater
const int RELAY_CH3 = 12;  // Fan 1
const int RELAY_CH4 = 13;  // Fan 2

// I2C LCD: default ESP32 SDA=21, SCL=22
LiquidCrystal_I2C lcd(0x27, 16, 2); // ganti 0x3F kalau I2C address beda

// ================== CONTROL CONFIG ===============
const float TARGET_TEMP = 26.0;      // °C
const float HYSTERESIS  = 0.8;       // °C (deadband biar nggak sering flip)
const unsigned long DHT_INTERVAL_MS   = 2000;   // baca DHT11 tiap 2 detik
const unsigned long LCD_INTERVAL_MS   = 1000;   // update LCD tiap 1 detik
const unsigned long MANUAL_TIMEOUT_MS = 180000; // 3 menit → kembali AUTO

// Relay active level (kebanyakan modul: LOW = ON, HIGH = OFF)
const bool RELAY_ON  = LOW;
const bool RELAY_OFF = HIGH;

// ================== GLOBAL VARS ==================
WebServer server(80);
DHTesp dht;

// sensor
float lastTemperature = NAN;
float lastHumidity    = NAN;
unsigned long lastDhtMillis  = 0;
unsigned long lastLcdMillis  = 0;

// mode
bool manualMode = false;
unsigned long lastUserActionMillis = 0;

// state relay (logika ON/OFF)
bool relayStateCh1 = false;
bool relayStateCh2 = false;
bool relayStateCh3 = false;
bool relayStateCh4 = false;

// ================== HELPER: RELAY ================
void setRelay(int ch, bool on) {
  int pin;
  bool state = on ? RELAY_ON : RELAY_OFF;

  switch (ch) {
    case 1:
      pin = RELAY_CH1;
      relayStateCh1 = on;
      break;
    case 2:
      pin = RELAY_CH2;
      relayStateCh2 = on;
      break;
    case 3:
      pin = RELAY_CH3;
      relayStateCh3 = on;
      break;
    case 4:
      pin = RELAY_CH4;
      relayStateCh4 = on;
      break;
    default:
      return;
  }
  digitalWrite(pin, state);
}

String getModeString() {
  return manualMode ? "MANUAL" : "AUTO";
}

int getManualRemainingSeconds() {
  if (!manualMode) return 0;
  long remaining = (long)MANUAL_TIMEOUT_MS - (long)(millis() - lastUserActionMillis);
  if (remaining < 0) remaining = 0;
  return remaining / 1000;
}

// ================== CONTROL LOGIC ================
void updateAutoControl() {
  // Manual mode → user override, jangan sentuh relay
  if (manualMode) return;

  // Belum ada data valid dari DHT
  if (isnan(lastTemperature)) return;

  if (lastTemperature > TARGET_TEMP + HYSTERESIS) {
    // TERLALU PANAS → Fan 1 & Fan 2 ON, Heater OFF
    setRelay(2, false); // heater OFF
    setRelay(3, true);  // fan1 ON
    setRelay(4, true);  // fan2 ON
  } else if (lastTemperature < TARGET_TEMP - HYSTERESIS) {
    // TERLALU DINGIN → Heater ON, Fans OFF
    setRelay(2, true);  // heater ON
    setRelay(3, false); // fan1 OFF
    setRelay(4, false); // fan2 OFF
  } else {
    // Dalam range nyaman → semua OFF (hemat energi)
    setRelay(2, false);
    setRelay(3, false);
    setRelay(4, false);
  }
}

// ================== LCD DISPLAY ==================
void updateLCD() {
  lcd.clear();

  // Baris 1: "T:26.3C H:60%"
  lcd.setCursor(0, 0);
  lcd.print("T:");
  if (isnan(lastTemperature)) lcd.print("--.-");
  else lcd.print(lastTemperature, 1);
  lcd.print("C ");

  lcd.print("H:");
  if (isnan(lastHumidity)) lcd.print("--");
  else lcd.print((int)lastHumidity);
  lcd.print("%");

  // Baris 2: mode + status heater/fan
  lcd.setCursor(0, 1);
  if (manualMode) lcd.print("MAN ");
  else lcd.print("AUT ");

  // Heater
  lcd.print("H:");
  lcd.print(relayStateCh2 ? "1" : "0");
  lcd.print(" ");

  // Fans: encode 0/1/2/12
  lcd.print("F:");
  if (relayStateCh3 || relayStateCh4) {
    if (relayStateCh3 && relayStateCh4) lcd.print("12");
    else if (relayStateCh3) lcd.print("1 ");
    else lcd.print("2 ");
  } else {
    lcd.print("0 ");
  }
}

// ================== JSON HELPER ==================
String boolToJsonBool(bool v) {
  return v ? "true" : "false";
}

String buildStatusJson() {
  String json = "{";

  json += "\"mode\":\"" + getModeString() + "\",";
  json += "\"manual_remaining_s\":" + String(getManualRemainingSeconds()) + ",";
  json += "\"target_temp\":" + String(TARGET_TEMP, 1) + ",";

  // temperature & humidity
  if (isnan(lastTemperature)) json += "\"temperature\":null,";
  else json += "\"temperature\":" + String(lastTemperature, 1) + ",";

  if (isnan(lastHumidity)) json += "\"humidity\":null,";
  else json += "\"humidity\":" + String(lastHumidity, 1) + ",";

  // relay states
  json += "\"relay\":{";
  json += "\"ch1\":" + boolToJsonBool(relayStateCh1) + ",";
  json += "\"ch2\":" + boolToJsonBool(relayStateCh2) + ",";
  json += "\"ch3\":" + boolToJsonBool(relayStateCh3) + ",";
  json += "\"ch4\":" + boolToJsonBool(relayStateCh4);
  json += "}";

  json += "}";
  return json;
}

void sendJson(const String &json, int code = 200) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(code, "application/json", json);
}

void sendPlain(const String &txt, int code = 200) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(code, "text/plain", txt);
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204); // No Content
}

// ================== HTTP HANDLERS ================
void handleRoot() {
  sendPlain("ESP32 Room Temp Controller API. Use /status or /relay?ch=2&state=1");
}

void handleStatus() {
  String json = buildStatusJson();
  sendJson(json);
}

void handleRelay() {
  if (!server.hasArg("ch") || !server.hasArg("state")) {
    sendJson("{\"error\":\"Missing ch or state\"}", 400);
    return;
  }

  int ch = server.arg("ch").toInt();
  int st = server.arg("state").toInt();

  if (ch < 1 || ch > 4) {
    sendJson("{\"error\":\"ch must be 1-4\"}", 400);
    return;
  }

  bool on = (st != 0);
  setRelay(ch, on);

  // Set manual mode (prioritas user)
  manualMode = true;
  lastUserActionMillis = millis();

  // Balas status terbaru
  String json = buildStatusJson();
  sendJson(json);
}

void handleNotFound() {
  sendJson("{\"error\":\"Not found\"}", 404);
}

// ================== SETUP & LOOP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=== Room Temp Controller Start ===");

  // DHT
  dht.setup(DHT_PIN, DHTesp::DHT11);
  Serial.println("DHT11 init OK");

  // Relay pins
  pinMode(RELAY_CH1, OUTPUT);
  pinMode(RELAY_CH2, OUTPUT);
  pinMode(RELAY_CH3, OUTPUT);
  pinMode(RELAY_CH4, OUTPUT);

  // All relay OFF awal
  setRelay(1, false);
  setRelay(2, false);
  setRelay(3, false);
  setRelay(4, false);
  Serial.println("Relay init (all OFF)");

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Room Temp Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connecting");

  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print("Status: ");
    lcd.print(dotCount++ % 4); // kecil-kecilan indikator
  }

  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(2000);

  // HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/relay", HTTP_GET, handleRelay);
  server.on("/relay", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Baca sensor DHT berkala
  if (now - lastDhtMillis >= DHT_INTERVAL_MS) {
    lastDhtMillis = now;
    TempAndHumidity data = dht.getTempAndHumidity();
    if (!isnan(data.temperature) && !isnan(data.humidity)) {
      lastTemperature = data.temperature;
      lastHumidity    = data.humidity;
      Serial.print("DHT T=");
      Serial.print(lastTemperature);
      Serial.print("C H=");
      Serial.print(lastHumidity);
      Serial.println("%");
    } else {
      Serial.println("Failed to read from DHT11");
    }
  }

  // Timeout manual mode → kembali AUTO
  if (manualMode && (now - lastUserActionMillis >= MANUAL_TIMEOUT_MS)) {
    manualMode = false;
    Serial.println("Manual timeout → back to AUTO");
  }

  // Kontrol otomatis (kalau bukan manual)
  updateAutoControl();

  // Update LCD berkala
  if (now - lastLcdMillis >= LCD_INTERVAL_MS) {
    lastLcdMillis = now;
    updateLCD();
  }
}
