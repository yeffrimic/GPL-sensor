#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <math.h>

// Pines
#define MQ5_PIN 35      // Nuevo pin ADC1
#define RELAY_PIN 16

// Divisor de voltaje (R1 = 10k, R2 = 6.8k)
#define MAX_VOLTAGE 3.3

// Constantes del MQ-5
#define RL 10.0  // kΩ
#define R0 9.83  // kΩ en aire limpio (ajustable según calibración)
#define CURVE_A -0.47
#define CURVE_B 1.63

// EEPROM
#define EEPROM_SIZE 4
float gasLimitPPM = 500.0;
float sensorVoltage = 0.0;
float ppmGLP = 0.0;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// WiFi Access Point
const char* ssid = "GLPx01";
const char* password = "a1b2c3d4e5";
WebServer server(80);

// Timers
unsigned long lastSensorRead = 0;
unsigned long sensorInterval = 1000;
bool ipShown = false;
unsigned long startMillis = 0;

// ========================= EEPROM =========================
void loadLimit() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, gasLimitPPM);
  if (isnan(gasLimitPPM) || gasLimitPPM < 2 || gasLimitPPM > 10000) {
    Serial.println("[EEPROM] Valor inválido. Usando 500 ppm por defecto.");
    gasLimitPPM = 3.0;
  } else {
    Serial.print("[EEPROM] Límite PPM cargado: ");
    Serial.print(gasLimitPPM);
    Serial.println(" ppm");
  }
}

void saveLimit(float value) {
  gasLimitPPM = value;
  EEPROM.put(0, gasLimitPPM);
  EEPROM.commit();
  Serial.print("[EEPROM] Nuevo límite guardado: ");
  Serial.print(gasLimitPPM);
  Serial.println(" ppm");
}

// ========================= Web Server =========================
void handleRoot() {
  Serial.println("[WEB] Página principal abierta");
  String html = "<html><body><h1>MQ-5 GLP Monitor</h1>";
  html += "<p>Voltaje actual: " + String(sensorVoltage, 2) + " V</p>";
  html += "<p>GLP estimado: " + String(ppmGLP, 0) + " ppm</p>";
  html += "<p>Límite actual: " + String(gasLimitPPM, 0) + " ppm</p>";
  html += "<form action='/set'>";
  html += "<input type='number' step='1' name='limit' min='10' max='10000'>";
  html += "<input type='submit' value='Actualizar'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSet() {
  Serial.println("[WEB] Intentando cambiar límite");
  if (server.hasArg("limit")) {
    float newLimit = server.arg("limit").toFloat();
    if (newLimit >= 2 && newLimit <= 10000) {
      saveLimit(newLimit);
      Serial.println("[WEB] Límite actualizado correctamente");
    } else {
      Serial.println("[WEB] Valor fuera de rango (10–10000 ppm)");
    }
  } else {
    Serial.println("[WEB] Argumento 'limit' no encontrado");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// ========================= MQ-5 Calculo PPM =========================
float getGLPppm(float voltage) {
  float Rs = (MAX_VOLTAGE - voltage) * RL / voltage;
  float ratio = Rs / R0;
  float ppm_log = CURVE_A * log10(ratio) + CURVE_B;
  float ppm = pow(10, ppm_log);
  return ppm;
}

// ========================= Setup =========================
void setup() {
  Serial.begin(115200);
  Serial.println("========== INICIO ==========");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(MQ5_PIN, INPUT);
  Wire.begin(25, 27);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");

  loadLimit();

  Serial.print("[WiFi] Iniciando AP: ");
  Serial.println(ssid);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[WiFi] IP local: ");
  Serial.println(IP);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectar a:");
  lcd.setCursor(0, 1);
  lcd.print(IP);
  startMillis = millis();

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
  Serial.println("[WEB] Servidor iniciado");
}

// ========================= Loop =========================
void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();

  if (!ipShown && currentMillis - startMillis >= 5000) {
    lcd.clear();
    ipShown = true;
    Serial.println("[LCD] IP mostrada por 5s. Limpia pantalla.");
  }

  if (currentMillis - lastSensorRead >= sensorInterval) {
    lastSensorRead = currentMillis;

    int adcValue = analogRead(MQ5_PIN);
    sensorVoltage = (adcValue / 1024.0) * MAX_VOLTAGE;
    ppmGLP = getGLPppm(sensorVoltage);

    Serial.print("[SENSOR] ADC: ");
    Serial.print(adcValue);
    Serial.print(" → Volt: ");
    Serial.print(sensorVoltage, 2);
    Serial.print(" V, GLP: ");
    Serial.print(ppmGLP, 0);
    Serial.println(" ppm");

    if (ppmGLP > gasLimitPPM) {
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("[RELAY] ACTIVADO");
    } else {
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("[RELAY] Desactivado");
    }

    if (ipShown) {
      lcd.setCursor(0, 0);
      lcd.print("GLP: ");
      lcd.print(ppmGLP, 0);
      lcd.print("ppm   ");

      lcd.setCursor(0, 1);
      lcd.print("Lim: ");
      lcd.print(gasLimitPPM, 0);
      lcd.print("ppm ");
    }
  }
}
