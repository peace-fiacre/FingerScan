#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <Preferences.h>

// ============================================================
// CONFIGURATION WI-FI & MQTT
// ============================================================

const char* ssid     = "TCL-394C";
const char* password = "Hurq93F76td9";

const char* mqtt_server = "c0371e0a8d954b50986fc0f064dbe77e.s1.eu.hivemq.cloud";
const int   mqtt_port   = 8883;

const char* mqtt_user   = "fingerscan";
const char* mqtt_pass   = "fingerscan";

// TOPICS MQTT
const char* TOPIC_CMD_STATUS_REQ   = "esp32/cmd/status_request";
const char* TOPIC_CMD_ENROLL_START = "esp32/cmd/enroll_start";
const char* TOPIC_CMD_ENROLL_CONF  = "esp32/cmd/enroll_confirm";
const char* TOPIC_CMD_DELETE        = "esp32/cmd/delete";
const char* TOPIC_CMD_DELETE_ALL   = "esp32/cmd/delete_all";

const char* TOPIC_STATUS = "esp32/status";
const char* TOPIC_RESULT = "esp32/result";

// ============================================================
// MATÉRIEL & ÉCRAN OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

#define OLED_SDA_PIN D4
#define OLED_SCL_PIN D5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define JM101B_RX_PIN 2
#define JM101B_TX_PIN 3

HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

// ============================================================
// VARIABLES GLOBALES
// ============================================================

int lastId = 0;
bool sensorConnected = false;
unsigned long lastSensorCheck = 0;
String pendingUserName = "";
bool enrollPending = false;
volatile bool cancelRequested = false;

// ============================================================
// FONCTIONS OLED
// ============================================================

void printCentered(String text, int y, int textSize = 1) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.println(text);
}

void updateOLED(String statusHeader, String mainText, String subText = "", bool isError = false) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("PRESENCE");
  
  int xStat = SCREEN_WIDTH - (statusHeader.length() * 6);
  display.setCursor(xStat > 60 ? xStat : 60, 0);
  display.print(statusHeader);
  
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  if (isError) {
    display.drawRect(0, 14, SCREEN_WIDTH, 50, SSD1306_WHITE);
    printCentered("! ERREUR !", 18, 1);
    printCentered(mainText, 32, 1);
    if (subText != "") printCentered(subText, 46, 1);
  } else {
    printCentered(mainText, 22, (mainText.length() > 10) ? 1 : 2);
    if (subText != "") printCentered(subText, 48, 1);
  }

  display.display();
}

// ============================================================
// PUBLICATION MQTT
// ============================================================

void publishResult(String message) {
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_RESULT, message.c_str());
  }
  Serial.println("[RESULT] " + message);
}

// Nouvelle fonction de formatage pour l'application
void publishFormattedStatus(bool capteurOk, int total) {
  String statusMsg = "";
  
  if (capteurOk) {
    statusMsg += "🟢 Capteur : En ligne\n";
  } else {
    statusMsg += "🔴 Capteur : Erreur\n";
  }
  
  statusMsg += "🌐 Réseau : Connecté\n";
  statusMsg += "👤 Total : " + String(total) + " empreinte(s)";

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_STATUS, statusMsg.c_str());
  }

  Serial.println("[STATUS]\n" + statusMsg);
}

// ============================================================
// GESTION NVS
// ============================================================

String getNameForID(int id) {
  preferences.begin("users", true);
  String name = preferences.getString(("id_" + String(id)).c_str(), "Inconnu");
  preferences.end();
  return name;
}

void saveNameForID(int id, String name) {
  preferences.begin("users", false);
  preferences.putString(("id_" + String(id)).c_str(), name);
  preferences.end();
}

void deleteNameForID(int id) {
  preferences.begin("users", false);
  preferences.remove(("id_" + String(id)).c_str());
  preferences.end();
}

void updateLastID() {
  if (sensorConnected && finger.getTemplateCount() == FINGERPRINT_OK) {
    lastId = finger.templateCount;
  }
}

// ============================================================
// CONTRÔLE CAPTEUR
// ============================================================

bool checkSensor() {
  if (finger.verifyPassword()) {
    if (!sensorConnected) {
      sensorConnected = true;
      updateLastID();

      updateOLED("WiFi OK", "Capteur Pret", "Placez votre doigt");
      publishFormattedStatus(true, lastId);
    }
    return true;
  } else {
    if (sensorConnected || lastSensorCheck == 0) {
      sensorConnected = false;
      updateOLED("HW ERR", "Capteur Empreinte", "Non Detecte", true);
      publishFormattedStatus(false, 0);
    }
    return false;
  }
}

// ============================================================
// ENRÔLEMENT
// ============================================================

void runEnrollmentProcess() {
  String userName = pendingUserName;
  enrollPending = false;
  cancelRequested = false;

  updateOLED("ENROL", userName, "Posez le doigt...");
  publishResult("ENROLLMENT_STARTED:" + userName);
  delay(1500);

  updateLastID();
  int newId = lastId + 1;

  int p = -1;
  while (p != FINGERPRINT_OK && !cancelRequested) {
    mqttClient.loop();
    p = finger.getImage();
    yield();
  }

  if (cancelRequested) {
    updateOLED("ENROL", "Annule", "", true);
    publishResult("CANCELLED: Enrolement interrompu");
    pendingUserName = "";
    delay(2000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    return;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    publishResult("ERROR: Image 1 floue");
    updateOLED("ENROL", "Image Floue", "Ressayez", true);
    delay(2000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    pendingUserName = "";
    return;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    String existingName = getNameForID(finger.fingerID);
    publishResult("ERROR: Existe deja pour " + existingName);
    updateOLED("ENROL", "Existe Deja", existingName, true);
    pendingUserName = "";
    delay(3000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    return;
  }

  updateOLED("ENROL", "Retirez", "le doigt...");
  delay(2000);

  p = 0;
  while (p != FINGERPRINT_NOFINGER && !cancelRequested) {
    mqttClient.loop();
    p = finger.getImage();
    yield();
  }

  updateOLED("ENROL", "Reposez", "le meme doigt");
  publishResult("WAITING_FINGER_2");

  p = -1;
  unsigned long timeout2 = millis();
  while (p != FINGERPRINT_OK && !cancelRequested) {
    mqttClient.loop();
    p = finger.getImage();
    if (millis() - timeout2 > 10000) {
      publishResult("ERROR: Timeout capture 2");
      updateOLED("ENROL", "Temps Ecoule", "", true);
      pendingUserName = "";
      delay(2000);
      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
      return;
    }
    yield();
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    publishResult("ERROR: Image 2 floue");
    updateOLED("ENROL", "Image 2 Floue", "", true);
    pendingUserName = "";
    delay(2000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    return;
  }

  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    publishResult("ERROR: Empreintes non identiques");
    updateOLED("ENROL", "Non Identiques", "Ressayez", true);
    pendingUserName = "";
    delay(2000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    return;
  }

  p = finger.storeModel(newId);
  if (p == FINGERPRINT_OK) {
    saveNameForID(newId, userName);
    lastId = newId;

    publishResult("OK: " + userName + " (ID " + String(newId) + ") enregistre !");
    updateOLED("ENROL", userName, "Enregistre ! (ID " + String(newId) + ")");
    pendingUserName = "";
    delay(3000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
  } else {
    publishResult("ERROR: Stockage impossible");
    updateOLED("ENROL", "Erreur Stockage", "ID: " + String(newId), true);
    pendingUserName = "";
    delay(2000);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
  }
}

// ============================================================
// CALLBACK & RECONNEXION MQTT
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  String receivedTopic = String(topic);
  message.trim();

  if (receivedTopic == TOPIC_CMD_STATUS_REQ) {
    int totalEmpreintes = (sensorConnected && finger.getTemplateCount() == FINGERPRINT_OK) ? finger.templateCount : 0;
    publishFormattedStatus(sensorConnected, totalEmpreintes);
    return;
  }

  if (receivedTopic == TOPIC_CMD_ENROLL_START) {
    if (!sensorConnected) { publishResult("ERROR: Capteur non detecte"); return; }
    if (message.length() == 0) { publishResult("ERROR: Nom manquant"); return; }
    if (enrollPending) { publishResult("ERROR: Un enrolement est deja en attente"); return; }

    pendingUserName = message;
    enrollPending = true;
    cancelRequested = false;

    updateOLED("ENROL", pendingUserName, "Confirmer sur App?");
    publishResult("WAITING_CONFIRMATION: " + pendingUserName);
    return;
  }

  if (receivedTopic == TOPIC_CMD_ENROLL_CONF) {
    if (message == "continue") {
      if (enrollPending) runEnrollmentProcess();
      else publishResult("ERROR: Aucun enrolement en cours");
      return;
    }
    if (message == "cancel") {
      enrollPending = false;
      cancelRequested = true;
      pendingUserName = "";
      updateOLED("ENROL", "Annule", "", true);
      publishResult("CANCELLED: Enrolement annule");
      delay(2000);
      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
      return;
    }
  }

  if (receivedTopic == TOPIC_CMD_DELETE) {
    int idToDelete = message.toInt();
    if (idToDelete <= 0 || !sensorConnected) {
      publishResult("ERROR: Requete invalide ou capteur HS");
      return;
    }

    String deletedName = getNameForID(idToDelete);
    if (deletedName == "Inconnu") {
      publishResult("ERROR: ID " + String(idToDelete) + " non enregistre");
      updateOLED("DEL", "ID Inconnu", "ID: " + String(idToDelete), true);
      delay(2000);
      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
      return;
    }

    if (finger.deleteModel(idToDelete) == FINGERPRINT_OK) {
      deleteNameForID(idToDelete);
      updateLastID();
      publishResult("OK: ID " + String(idToDelete) + " (" + deletedName + ") supprime");
      updateOLED("DEL", deletedName, "Supprime !");
      delay(2000);
      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    } else {
      publishResult("ERROR: Echec suppression hardware");
      updateOLED("DEL", "Echec Effacement", "", true);
      delay(2000);
      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    }
    return;
  }

  if (receivedTopic == TOPIC_CMD_DELETE_ALL) {
    if (!sensorConnected) { publishResult("ERROR: Capteur non detecte"); return; }

    if (finger.emptyDatabase() == FINGERPRINT_OK) {
      preferences.begin("users", false);
      preferences.clear();
      preferences.end();
      lastId = 0;

      publishResult("OK: Base reinitialisee");
      updateOLED("DEL ALL", "Base Videe !", "");
      delay(2000);
      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    } else {
      publishResult("ERROR: Echec reinitialisation");
    }
    return;
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    updateOLED("CONNECT", "HiveMQ Cloud", "Connexion...");
    String clientId = "XIAO_ESP32_" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      mqttClient.subscribe(TOPIC_CMD_STATUS_REQ);
      mqttClient.subscribe(TOPIC_CMD_ENROLL_START);
      mqttClient.subscribe(TOPIC_CMD_ENROLL_CONF);
      mqttClient.subscribe(TOPIC_CMD_DELETE);
      mqttClient.subscribe(TOPIC_CMD_DELETE_ALL);

      int total = (sensorConnected && finger.getTemplateCount() == FINGERPRINT_OK) ? finger.templateCount : 0;
      publishFormattedStatus(sensorConnected, total);

      updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
    } else {
      updateOLED("MQTT ERR", "Echec Connexion", "Nouvelle tentative...", true);
      delay(3000);
    }
  }
}

// ============================================================
// PRÉSENCE
// ============================================================

void checkPresence() {
  if (!sensorConnected || enrollPending) return;

  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) return;

  uint8_t p = finger.fingerFastSearch();

  if (p == FINGERPRINT_OK) {
    String userName = getNameForID(finger.fingerID);
    updateOLED("ACCES", userName, "ID: " + String(finger.fingerID));
    publishResult("PRESENCE_OK:" + userName + ":" + String(finger.fingerID));
    delay(2500);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
  } else if (p == FINGERPRINT_NOTFOUND) {
    updateOLED("ACCES", "Refuse", "Inconnu", true);
    publishResult("PRESENCE_REFUSED:Inconnu");
    delay(2500);
    updateOLED("MQTT OK", "Lecteur Pret", "Placez votre doigt");
  }
}

// ============================================================
// SETUP & LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Erreur OLED");
  }

  updateOLED("INIT", "Demarrage...");

  mySerial.begin(57600, SERIAL_8N1, JM101B_RX_PIN, JM101B_TX_PIN);
  finger.begin(57600);

  updateOLED("WIFI", "Connexion...", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  updateOLED("WIFI OK", "IP Obtenue", WiFi.localIP().toString());
  delay(1500);

  espClient.setInsecure();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  checkSensor();
  delay(1000);
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  if (!sensorConnected) {
    if (millis() - lastSensorCheck >= 2000) {
      lastSensorCheck = millis();
      checkSensor();
    }
  } else {
    checkPresence();
  }
}