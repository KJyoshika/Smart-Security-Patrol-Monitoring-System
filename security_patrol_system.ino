#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"

// ─── PIN DEFINITIONS ─────────────────────────────────
#define SS_PIN      21
#define RST_PIN     22
#define BUZZER_PIN  25

// ─── WIFI ────────────────────────────────────────────
#define WIFI_SSID     "JK"
#define WIFI_PASSWORD "jkkitty@08"

// ─── NTP ─────────────────────────────────────────────
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   daylightOffset_sec = 0;

// ─── TELEGRAM ────────────────────────────────────────
#define BOT_TOKEN "8445767131:AAGW4OiXXtBBZnGr0s7Mgew7p07kv4_J6Ks"
#define ADMIN_CHAT_ID "6692866909"

// ─── GOOGLE SCRIPT ───────────────────────────────────
const String GOOGLE_SCRIPT_BASE_URL =
"https://script.google.com/macros/s/AKfycby9vqCwyVz_ne6uYwAO6xdNuapxErG4f0_NbBKVNeGuZf1Dlyt9jWia8VBrLfP1uuphlw/exec";
const String LOCATION = "IOT Lab";

// ─── RFID ────────────────────────────────────────────
MFRC522 rfid(SS_PIN, RST_PIN);

// ─── DATABASE ────────────────────────────────────────
#define MAX_STUDENTS 50

struct Student {
  String name;
  String uid;
  String chatID;
  unsigned long lastScan;
  bool alertSent;
  bool reminderSent;
};

Student students[MAX_STUDENTS];
int studentCount = 0;
bool dateStarted = false;
int currentDay = -1;
// ─── URL ENCODE ──────────────────────────────────────
String urlEncode(const String& s) {
  String encoded = "";
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if (isAlphaNumeric(c) || c=='-'||c=='_'||c=='.'||c=='~') {
      encoded += c;
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// ─── TELEGRAM SEND ───────────────────────────────────
void sendTelegramTo(String chat_id, String message) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";
  url += "?chat_id=" + chat_id;
  url += "&text=" + urlEncode(message);

  http.begin(url);
  http.GET();
  http.end();
}

// ─── GOOGLE SHEET LOG ────────────────────────────────
bool sendToGoogleSheet(String name, String uid, String location, String status) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = GOOGLE_SCRIPT_BASE_URL +
               "?target=log&sts=write&sheet=rfid_log" +
               "&name=" + urlEncode(name) +
               "&uid=" + urlEncode(uid) +
               "&location=" + urlEncode(location) +
               "&status=" + urlEncode(status);

  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  http.end();

  return (httpCode == HTTP_CODE_OK);
}

// ─── FETCH DATABASE ──────────────────────────────────
void fetchDatabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = GOOGLE_SCRIPT_BASE_URL + "?target=database&sts=read";

  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, payload)) return;

    studentCount = 0;

    for (JsonArray row : doc.as<JsonArray>()) {
      students[studentCount].name   = row[0].as<String>();
      students[studentCount].uid    = row[1].as<String>();
      students[studentCount].chatID = row[2].as<String>();

      students[studentCount].lastScan = 0;
      students[studentCount].alertSent = false;
      students[studentCount].reminderSent = false;

      students[studentCount].uid.trim();
      students[studentCount].uid.toUpperCase();

      studentCount++;
    }
  }

  http.end();
}

// ─── BUZZER ──────────────────────────────────────────
void beepSuccess() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepUnauthorized() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

// ─── SETUP ───────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);

  SPI.begin(18, 19, 23, SS_PIN);

  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(50);
  digitalWrite(RST_PIN, HIGH);
  delay(50);

  rfid.PCD_Init();
  rfid.PCD_AntennaOn();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  fetchDatabase();

  Serial.println("[READY]");
}

// ─── LOOP ────────────────────────────────────────────
void loop() {

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;

  // ✅ TIME CONDITION ONLY ADDED
  if (hour >= 14 && hour <= 21) { 

    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {

      String cardID = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) cardID += "0";
        cardID += String(rfid.uid.uidByte[i], HEX);
      }
      cardID.toUpperCase();

      bool found = false;

      for (int i = 0; i < studentCount; i++) {
        if (cardID != students[i].uid) continue;

        found = true;

        unsigned long now = millis();
        unsigned long diff = now - students[i].lastScan;

        String status;

        if (students[i].lastScan == 0 || diff < 60000) {
          status = "IN TIME";
        } else {
          status = "MISSED";
           students[i].lastScan = 0;
        }

        students[i].lastScan = now;
        students[i].alertSent = false;
        students[i].reminderSent = false;

        beepSuccess();
        sendToGoogleSheet(students[i].name, cardID, LOCATION, status);
        break;
      }

      if (!found) {
        beepUnauthorized();
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      rfid.PCD_Init();
    }

    unsigned long currentTime = millis();

    for (int i = 0; i < studentCount; i++) {

      if (students[i].lastScan == 0) continue;

      unsigned long diff = currentTime - students[i].lastScan;

      if (!students[i].reminderSent && diff >= 30000) {

        String msg = "⏳ Reminder\n";
        msg += "30 sec left to scan!\n";
        msg += "Location: " + LOCATION;

        sendTelegramTo(students[i].chatID, msg);

        students[i].reminderSent = true;
      }

      if (!students[i].alertSent && diff > 60000) {

        sendToGoogleSheet(students[i].name,
                          students[i].uid,
                          LOCATION,
                          "MISSED");

        String msg = "🚨 SECURITY ALERT 🚨\n";
        msg += "Guard: " + students[i].name + "\n";
        msg += "Missed checkpoint!\n";
        msg += "Location: " + LOCATION;

        sendTelegramTo(ADMIN_CHAT_ID, msg);

        students[i].alertSent = true;
      }
    }

  }else {

  // ✅ ADD HERE ONLY
  if (dateStarted) {
    sendToGoogleSheet("NA", "NA", LOCATION, "NA");
    dateStarted = false;
  }


  delay(500);
}
}