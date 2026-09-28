/*
  SUJET 12 - Station météorologique locale
  Module IoT et Cloud Computing - Université Ibn Tofail
  Scénario 1 : ESP32 simulé (Wokwi) + broker MQTT public

  Capteurs simulés :
    - DHT22          -> température (°C) + humidité (%)
    - BMP180         -> pression (Pa) + température de recoupement
    - LDR            -> luminosité (%)
    - Potentiomètre  -> vitesse du vent simulée (km/h)
    - Potentiomètre  -> pluie simulée (mm/h)
    - OLED SSD1306   -> affichage local des dernières valeurs

  A FAIRE avant de lancer la simulation :
    1) Remplacer GROUP_ID par l'identifiant réel du groupe (ex: "groupe07")
    2) Vérifier que le broker choisi (broker.emqx.io) est bien celui utilisé par le groupe
*/

#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// CONFIGURATION - à adapter par groupe
// ---------------------------------------------------------------------------
#define GROUP_ID   "groupe10"          // <-- changer ici
#define DEVICE_ID  "esp32-01"
#define TOPIC_BASE "iotcloud/2026/" GROUP_ID "/station-meteo/" DEVICE_ID

const char* WIFI_SSID = "Wokwi-GUEST"; // réseau virtuel fourni par Wokwi (avec accès Internet)
const char* WIFI_PASS = "";

const char* MQTT_SERVER = "broker.emqx.io";
const int   MQTT_PORT   = 1883;

const char* TOPIC_TELEMETRY = TOPIC_BASE "/telemetry";
const char* TOPIC_STATUS    = TOPIC_BASE "/status";
const char* TOPIC_ALERT     = TOPIC_BASE "/alert";
const char* TOPIC_COMMAND   = TOPIC_BASE "/command";
const char* TOPIC_ACK       = TOPIC_BASE "/ack";

// ---------------------------------------------------------------------------
// BROCHES
// ---------------------------------------------------------------------------
#define DHT_PIN   15
#define DHT_TYPE  DHT22
#define LDR_PIN   34
#define WIND_PIN  35
#define RAIN_PIN  32
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP085 bmp;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WiFiClient espClient;
PubSubClient mqtt(espClient);

String getTimestamp();

void verifierAnomalies(float temperature, float vent, float pluie);
void afficherOLED(float t, float h, float p, float vent, float pluie);
// ---------------------------------------------------------------------------
// SEUILS (modifiables à distance via une commande MQTT)
// ---------------------------------------------------------------------------
float seuilTempMax   = 38.0;   // °C
float seuilVentMax   = 70.0;   // km/h
float seuilPluieMax  = 30.0;   // mm/h

// ---------------------------------------------------------------------------
// INTERVALLES
// ---------------------------------------------------------------------------
const unsigned long INTERVALLE_MESURE  = 5000;   // publication télémétrie
const unsigned long INTERVALLE_RESUME  = 60000;  // résumé périodique (status)
unsigned long dernierMesure = 0;
unsigned long dernierResume = 0;

// ---------------------------------------------------------------------------
// STATISTIQUES GLISSANTES (min / max / moyenne depuis le dernier résumé)
// ---------------------------------------------------------------------------
struct Stat {
  float minVal = 1e9;
  float maxVal = -1e9;
  float somme = 0;
  int   nb = 0;
  void ajouter(float v) {
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
    somme += v;
    nb++;
  }
  float moyenne() { return nb > 0 ? somme / nb : 0; }
  void reset() { minVal = 1e9; maxVal = -1e9; somme = 0; nb = 0; }
};

Stat statTemp, statHum, statPression, statLum, statVent, statPluie;

// ---------------------------------------------------------------------------
// CONNEXION WIFI
// ---------------------------------------------------------------------------
void connecterWifi() {
  Serial.print("Connexion WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" OK, IP = " + WiFi.localIP().toString());
}
// ---------------------------------------------------------------------------
// TRAITEMENT DES COMMANDES REÇUES (topic "command")
// Format attendu :
// { "commandId": "cmd-001", "action": "set_threshold", "param": "temp_max", "value": 35 }
// { "commandId": "cmd-002", "action": "reset_stats" }
// ---------------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.println("JSON de commande invalide");
    return;
  }

  String commandId = doc["commandId"] | "inconnu";
  String action    = doc["action"]    | "";
  bool ok = true;
  String message = "traitée";

  if (action == "set_threshold") {
    String param = doc["param"] | "";
    float value  = doc["value"] | 0.0;
    if (param == "temp_max")       seuilTempMax  = value;
    else if (param == "vent_max")  seuilVentMax  = value;
    else if (param == "pluie_max") seuilPluieMax = value;
    else { ok = false; message = "paramètre inconnu"; }
  } else if (action == "reset_stats") {
    statTemp.reset(); statHum.reset(); statPression.reset();
    statLum.reset();  statVent.reset(); statPluie.reset();
  } else {
    ok = false;
    message = "action inconnue";
  }

  // Publication de l'accusé de réception
  StaticJsonDocument<192> ack;
  ack["commandId"] = commandId;
  ack["status"] = ok ? "ok" : "erreur";
  ack["message"] = message;
  ack["timestamp"] = getTimestamp();
  char buffer[192];
  size_t n = serializeJson(ack, buffer);
  mqtt.publish(TOPIC_ACK, buffer, n);
}

// ---------------------------------------------------------------------------
// RECONNEXION MQTT
// ---------------------------------------------------------------------------
void reconnecterMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connexion MQTT...");
    String clientId = String(DEVICE_ID) + "-" + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" OK");
      mqtt.subscribe(TOPIC_COMMAND);

      // Statut "en ligne"
      StaticJsonDocument<128> statusDoc;
      statusDoc["deviceId"] = DEVICE_ID;
      statusDoc["state"] = "online";
      char buf[128];
      size_t n = serializeJson(statusDoc, buf);
      mqtt.publish(TOPIC_STATUS, (const uint8_t*)buf, n, true); // retained
    } else {
      Serial.print(" échec, code=");
      Serial.print(mqtt.state());
      Serial.println(" -> nouvelle tentative dans 2s");
      delay(2000);
    }
  }
}

// ---------------------------------------------------------------------------
// LECTURE DES CAPTEURS + PUBLICATION DE LA TELEMETRIE
// ---------------------------------------------------------------------------
void mesurerEtPublier() {
  float temperature = dht.readTemperature();
  float humidite    = dht.readHumidity();
  float pression     = bmp.readPressure(); // en Pa

  int ldrBrut = analogRead(LDR_PIN);              // 0..4095
  float luminosite = map(ldrBrut, 0, 4095, 0, 100); // %

  int ventBrut = analogRead(WIND_PIN);
  float vitesseVent = map(ventBrut, 0, 4095, 0, 120); // km/h simulé

  int pluieBrut = analogRead(RAIN_PIN);
  float pluie = map(pluieBrut, 0, 4095, 0, 50); // mm/h simulé

  if (isnan(temperature) || isnan(humidite)) {
    Serial.println("Erreur de lecture DHT22 !");
    return;
  }

  // Mise à jour des statistiques glissantes
  statTemp.ajouter(temperature);
  statHum.ajouter(humidite);
  statPression.ajouter(pression);
  statLum.ajouter(luminosite);
  statVent.ajouter(vitesseVent);
  statPluie.ajouter(pluie);

  // Construction et publication du message de télémétrie
  StaticJsonDocument<384> doc;
  doc["deviceId"] = DEVICE_ID;
  doc["timestamp"] = getTimestamp();
  JsonObject measurements = doc.createNestedObject("measurements");
  measurements["temperature"] = round(temperature * 10) / 10.0;
  measurements["humidity"]    = round(humidite * 10) / 10.0;
  measurements["pressure"]    = round(pression);
  measurements["luminosity"]  = round(luminosite);
  measurements["windSpeed"]   = round(vitesseVent * 10) / 10.0;
  measurements["rain"]        = round(pluie * 10) / 10.0;

  char buffer[384];
  size_t n = serializeJson(doc, buffer);
  mqtt.publish(TOPIC_TELEMETRY, buffer, n);

  Serial.println(buffer);

  verifierAnomalies(temperature, vitesseVent, pluie);
  afficherOLED(temperature, humidite, pression, vitesseVent, pluie);
}

// ---------------------------------------------------------------------------
// DETECTION D'ANOMALIES -> publication sur le topic "alert"
// ---------------------------------------------------------------------------
void verifierAnomalies(float temperature, float vent, float pluie) {
  StaticJsonDocument<192> alerte;
  bool declenchee = false;

  if (temperature > seuilTempMax) {
    alerte["type"] = "chaleur_excessive";
    alerte["value"] = temperature;
    declenchee = true;
  } else if (vent > seuilVentMax) {
    alerte["type"] = "vent_fort";
    alerte["value"] = vent;
    declenchee = true;
  } else if (pluie > seuilPluieMax) {
    alerte["type"] = "pluie_forte";
    alerte["value"] = pluie;
    declenchee = true;
  }

  if (declenchee) {
    alerte["deviceId"] = DEVICE_ID;
    alerte["timestamp"] = getTimestamp();
    char buffer[192];
    size_t n = serializeJson(alerte, buffer);
    mqtt.publish(TOPIC_ALERT, buffer, n);
    Serial.println("ALERTE -> " + String(buffer));
  }
}

// ---------------------------------------------------------------------------
// RESUME PERIODIQUE (min / max / moyenne) -> topic "status"
// ---------------------------------------------------------------------------
void publierResume() {
  StaticJsonDocument<512> doc;
  doc["deviceId"] = DEVICE_ID;
  doc["timestamp"] = millis();
  doc["type"] = "resume_periodique";

  JsonObject temp = doc.createNestedObject("temperature");
  temp["min"] = statTemp.minVal; temp["max"] = statTemp.maxVal; temp["avg"] = statTemp.moyenne();

  JsonObject hum = doc.createNestedObject("humidity");
  hum["min"] = statHum.minVal; hum["max"] = statHum.maxVal; hum["avg"] = statHum.moyenne();

  JsonObject pres = doc.createNestedObject("pressure");
  pres["min"] = statPression.minVal; pres["max"] = statPression.maxVal; pres["avg"] = statPression.moyenne();

  JsonObject vent = doc.createNestedObject("windSpeed");
  vent["min"] = statVent.minVal; vent["max"] = statVent.maxVal; vent["avg"] = statVent.moyenne();

  JsonObject pluie = doc.createNestedObject("rain");
  pluie["min"] = statPluie.minVal; pluie["max"] = statPluie.maxVal; pluie["avg"] = statPluie.moyenne();

  char buffer[512];
  size_t n = serializeJson(doc, buffer);
  mqtt.publish(TOPIC_STATUS, buffer, n);
  Serial.println("RESUME -> " + String(buffer));

  // On repart sur une nouvelle fenêtre de calcul
  statTemp.reset(); statHum.reset(); statPression.reset();
  statLum.reset();  statVent.reset(); statPluie.reset();
}

// ---------------------------------------------------------------------------
// AFFICHAGE OLED
// ---------------------------------------------------------------------------
void afficherOLED(float t, float h, float p, float vent, float pluie) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Station meteo locale");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 16);
  display.print("Temp: "); display.print(t, 1); display.println(" C");
  display.setCursor(0, 26);
  display.print("Hum : "); display.print(h, 0); display.println(" %");
  display.setCursor(0, 36);
  display.print("Pres: "); display.print(p / 100.0, 0); display.println(" hPa");
  display.setCursor(0, 46);
  display.print("Vent: "); display.print(vent, 0); display.println(" km/h");
  display.setCursor(0, 56);
  display.print("Pluie: "); display.print(pluie, 0); display.println(" mm/h");

  display.display();
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------

String getTimestamp() {
  time_t now = time(nullptr);

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[30];

  strftime(
    buffer,
    sizeof(buffer),
    "%Y-%m-%dT%H:%M:%SZ",
    &timeinfo
  );

  return String(buffer);
}


void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin();

  if (!bmp.begin()) {
    Serial.println("BMP180 non détecté, vérifier le câblage !");
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED non détecté !");
  }
  display.clearDisplay();
  display.display();

connecterWifi();

configTime(0, 0, "pool.ntp.org", "time.nist.gov");

mqtt.setServer(MQTT_SERVER, MQTT_PORT);
mqtt.setCallback(mqttCallback);
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
  if (!mqtt.connected()) {
    reconnecterMQTT();
  }
  mqtt.loop();

  unsigned long maintenant = millis();

  if (maintenant - dernierMesure >= INTERVALLE_MESURE) {
    dernierMesure = maintenant;
    mesurerEtPublier();
  }

  if (maintenant - dernierResume >= INTERVALLE_RESUME) {
    dernierResume = maintenant;
    publierResume();
  }
}