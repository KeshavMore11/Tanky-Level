#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>

// ---------- PINS ----------
#define TRIG_PIN D5
#define ECHO_PIN D6

// ---------- WIFI ----------
const char* ssid = "Tulsi";
const char* password = "456keshqv";

// ---------- TELEGRAM ----------
#define BOT_TOKEN "8665704734:AAEs6ESy56ZepN1IFVvd8Lw-EfJjFeGmQv8"
#define CHAT_ID  "1353900891"

// ---------- FIREBASE ----------
// Paste ONLY the host part, no "https://" and no trailing slash.
// Looks like: your-project-id-default-rtdb.firebaseio.com
#define FIREBASE_HOST "tanky-level-default-rtdb.asia-southeast1.firebasedatabase.app"

// ---------- TANK CALIBRATION ----------
// Distance (cm) from sensor to water surface when tank is completely FULL.
// Set to 0 assuming the sensor sits flush at the very top of the tank
// (tank total depth = 215cm). If there's any standoff/gap above the max
// water line, measure it and put that number here instead of 0.
#define TANK_FULL_CM   0

// Same thresholds you were already using for alerts, reused for the app.
#define TANK_EMPTY_CM  215   // distance considered EMPTY (tank total depth)
#define TANK_LOW_CM    170   // distance considered LOW

// Where the "LOW" mark sits on the 0-100% scale, derived from the above.
const float LOW_MARK_PERCENT =
  (TANK_EMPTY_CM - TANK_LOW_CM) / (float)(TANK_EMPTY_CM - TANK_FULL_CM) * 100.0;

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ---------- GLOBAL VARIABLES ----------
float distance = 0;
float percent = 0;

bool lowAlertSent = false;
bool emptyAlertSent = false;

unsigned long lastFirebasePush = 0;
const unsigned long FIREBASE_PUSH_INTERVAL = 5000; // ms

// ---------- HELPERS ----------
float computePercent(float d) {
  float p = (TANK_EMPTY_CM - d) / (float)(TANK_EMPTY_CM - TANK_FULL_CM) * 100.0;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

void pushToFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure fbClient;
  fbClient.setInsecure();
  HTTPClient https;

  String url = "https://" + String(FIREBASE_HOST) + "/tank.json";

  if (https.begin(fbClient, url)) {
    https.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["distance"] = distance;
    doc["percent"] = percent;
    doc["lowMarkPercent"] = LOW_MARK_PERCENT;
    doc["low"] = lowAlertSent;
    doc["empty"] = emptyAlertSent;
    JsonObject lastSeen = doc.createNestedObject("lastSeen");
    lastSeen[".sv"] = "timestamp"; // Firebase fills this in with server time

    String payload;
    serializeJson(doc, payload);

    int httpCode = https.PUT(payload);
    Serial.print("Firebase push -> HTTP ");
    Serial.println(httpCode);

    https.end();
  } else {
    Serial.println("Firebase: unable to begin connection");
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  client.setInsecure();

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  // Test message
  bot.sendMessage(CHAT_ID, "✅ Tank Bot Connected!", "");
}

// ---------- LOOP ----------
void loop() {

  // -------- SENSOR --------
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    Serial.println("No echo");
    delay(1000);
    return;
  }

  distance = duration * 0.034 / 2;
  percent = computePercent(distance);

  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.print(" cm  |  Level: ");
  Serial.print(percent);
  Serial.println(" %");

  // -------- ALERT LOGIC --------

  // 🚨 EMPTY
  if (distance >= TANK_EMPTY_CM && !emptyAlertSent) {
    bot.sendMessage(CHAT_ID,
      "🚨 TANK EMPTY!\nPANI KHATAM!!\nDistance: " + String(distance) + " cm",
      "");
    emptyAlertSent = true;
    lowAlertSent = true;
  }

  // ⚠️ LOW
  else if (distance >= TANK_LOW_CM && !lowAlertSent) {
    bot.sendMessage(CHAT_ID,
      "⚠️ LOW WATER LEVEL!\nPANI KAM HAI\nDistance: " + String(distance) + " cm",
      "");
    lowAlertSent = true;
  }

  // 🔄 RESET (tank refilled)
  if (distance < 180) {
    lowAlertSent = false;
    emptyAlertSent = false;
  }

  // -------- FIREBASE PUSH (throttled) --------
  if (millis() - lastFirebasePush >= FIREBASE_PUSH_INTERVAL) {
    lastFirebasePush = millis();
    pushToFirebase();
  }

  // -------- TELEGRAM COMMANDS --------
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {

      String text = bot.messages[i].text;

      if (text == "/start") {
        bot.sendMessage(CHAT_ID,
          "🤖 Tank Bot Ready!\n\nUse /status to check water level",
          "");
      }

      if (text == "/status") {
        bot.sendMessage(CHAT_ID,
          "📊 Tank Distance: " + String(distance) + " cm\n💧 Level: " + String(percent, 0) + "%",
          "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }

  delay(2000);
}
