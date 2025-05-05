#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Servo.h>
#include <DS1302.h>
#include <EEPROM.h> // Include EEPROM library
#define EEPROM_PIR1_ADDR 200  // EEPROM address for PIR status
#define SERVO_START_ANGLE 60
#define SERVO_END_ANGLE   100
// DS1302 Pins
#define RTC_RST D0
#define RTC_DAT D3
#define RTC_CLK D4
// Pin Definitions
#define PIR1_PIN D2
#define LED_LAMP_PIN D1
#define SOIL_MOISTURE_PIN D6
#define SERVO_PIN D7
#define BUZZER_PIN D8
// Web Server
ESP8266WebServer server(80);
// DS1302 RTC instance
DS1302 rtc(RTC_RST, RTC_DAT, RTC_CLK);
// Servo instance
Servo feederServo;
// Feed Timing Structure
struct FeedTime {
  int hour;
  int minute;
  int duration; // duration in seconds
};
FeedTime feedTimes[10];
int feedTimeCount = 0;
// Sensor enable flags (default enabled)
bool pir1Enabled = true;
void checkFeedTimes() {
  Time now = rtc.time();
  for (int i = 0; i < feedTimeCount; i++) {
    if (now.hr == feedTimes[i].hour &&
        now.min == feedTimes[i].minute &&
        now.sec == 0) {
      // Move the servo to the fixed end angle then back to the start angle
      feederServo.write(SERVO_END_ANGLE);
      delay(feedTimes[i].duration * 1000);
      feederServo.write(SERVO_START_ANGLE);
    }
  }
}
void triggerAlert() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);
}
void saveFeedTimesToEEPROM() {
    EEPROM.put(0, feedTimes); // Save feedTimes array starting at EEPROM address 0
    EEPROM.put(sizeof(feedTimes), feedTimeCount); // Save feedTimeCount
    EEPROM.commit(); // Commit changes to EEPROM
}
void loadFeedTimesFromEEPROM() {
    EEPROM.get(0, feedTimes); // Load feedTimes array
    EEPROM.get(sizeof(feedTimes), feedTimeCount); // Load feedTimeCount
    // Validate feedTimeCount to prevent garbage values
    if (feedTimeCount < 0 || feedTimeCount > 10) {
        feedTimeCount = 0; // Reset if invalid
    }
}
void handleGetTime() {
  Time now = rtc.time(); // Get the current time from the DS1302 RTC
  // Format the time as a string: HH:MM:SS
  String timeStr = String(now.hr) + ":" + String(now.min) + ":" + String(now.sec);
  // Send the time as plain text back to the client
  server.send(200, "text/plain", timeStr);
}
void setup() {
  // Initialize EEPROM
  EEPROM.begin();
  loadFeedTimesFromEEPROM(); // Load stored feed times
 // Load stored sensor settings
  pir1Enabled = EEPROM.read(EEPROM_PIR1_ADDR);
  // Initialize Pins
  pinMode(PIR1_PIN, INPUT);
  pinMode(LED_LAMP_PIN , OUTPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  // Initialize Servo
  feederServo.attach(SERVO_PIN);
  feederServo.write(SERVO_START_ANGLE);
  // Initialize LittleFS
  LittleFS.begin();
  // Start Access Point
  WiFi.softAP("Chicken_Feeder", "12345678");
  // Server Endpoints
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/add-feedtime", HTTP_POST, handleAddFeedTime);
  server.on("/delete-feedtime", HTTP_POST, handleDeleteFeedTime);
  server.on("/get-feedtimes", HTTP_GET, handleGetFeedTimes);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/set-rtc", HTTP_POST, handleSetRTC);
  server.on("/toggle-lamp", HTTP_POST, handleToggleLamp);
  server.on("/get-settings", HTTP_GET, handleGetSettings);
  server.on("/get-time", HTTP_GET, handleGetTime);
  server.begin();
}
void loop() {
  server.handleClient();
  checkSensors();
  checkFeedTimes();
}
void checkSensors() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();
  if (pir1Enabled && digitalRead(PIR1_PIN)) {
    triggerAlert();
  }
}
void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  }
}
void handleStatus() {
  int ldrValue = analogRead(A0);  // Read the LDR value (0-1023)
  String ldrStatus;
  // Determine if it's "Bright" or "Dark" based on LDR value
  if (ldrValue < 1000) {  // Adjust threshold as needed
    ldrStatus = "Bright";
    digitalWrite(LED_LAMP_PIN,LOW);
  } else {
    ldrStatus = "Dark";
    digitalWrite(LED_LAMP_PIN,HIGH);
  }
  // Read other sensor statuses
  String soilStatus = digitalRead(SOIL_MOISTURE_PIN) == HIGH ? "WET" : "DRY";
  // Create the JSON response
  String json = "{";
  json += "\"pir\":" + String(digitalRead(PIR1_PIN) == HIGH ? "true" : "false") + ",";
  json += "\"ldr\":\"" + ldrStatus + "\",";
  json += "\"soilMoisture\":\"" + soilStatus + "\"";
  json += "}";
  // Send the JSON response
  server.send(200, "application/json", json);
}
void handleAddFeedTime() {
    if (server.hasArg("hour") && server.hasArg("minute") && server.hasArg("duration")) {
        int hour = server.arg("hour").toInt();
        int minute = server.arg("minute").toInt();
        int duration = server.arg("duration").toInt();
        // Validate duration (must be greater than 0)
        if (duration <= 0) {
            server.send(400, "text/plain", "Error: Duration must be greater than zero.");
            return;
        }
        if (feedTimeCount < 10) {
            feedTimes[feedTimeCount] = { hour, minute, duration };
            feedTimeCount++;
            saveFeedTimesToEEPROM(); // Save to EEPROM
            server.send(200, "text/plain", "Schedule added");
        } else {
            server.send(507, "text/plain", "Maximum schedules reached");
        }
    }
}
void handleDeleteFeedTime() {
    if (server.hasArg("index")) {
        int index = server.arg("index").toInt();
        if (index >= 0 && index < feedTimeCount) {
            for (int i = index; i < feedTimeCount - 1; i++) {
                feedTimes[i] = feedTimes[i + 1];
            }
            feedTimeCount--;
            saveFeedTimesToEEPROM(); // Save changes persistently
            server.send(200, "text/plain", "Schedule deleted");
        } else {
            server.send(404, "text/plain", "Invalid index");
        }
    }
}
void handleGetFeedTimes() {
  String json = "[";
  for (int i = 0; i < feedTimeCount; i++) {
    json += "{";
    json += "\"index\":" + String(i) + ",";
    json += "\"hour\":" + String(feedTimes[i].hour) + ",";
    json += "\"minute\":" + String(feedTimes[i].minute) + ",";
    json += "\"duration\":" + String(feedTimes[i].duration);
    json += "}";
    if (i < feedTimeCount - 1) {
      json += ",";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}
void handleSettings() {
    if (server.hasArg("pir")) {
        pir1Enabled = (server.arg("pir") == "true");
    }
    // Save to EEPROM
    saveSettingsToEEPROM();
    server.send(200, "text/plain", "Settings updated and saved to EEPROM");
}
// Create a function to send the current settings as a response
void handleGetSettings() {
    String json = "{";
    json += "\"pir\":" + String(pir1Enabled ? "true" : "false") + ",";
    json += "}";
    server.send(200, "application/json", json);
}
// Save settings to EEPROM at specific addresses
void saveSettingsToEEPROM() {
    EEPROM.put(EEPROM_PIR1_ADDR, pir1Enabled);
    EEPROM.commit();
}
// Load settings from EEPROM from specific addresses
void loadSettingsFromEEPROM() {
    EEPROM.get(EEPROM_PIR1_ADDR, pir1Enabled);
}
void handleSetRTC() {
  if (server.hasArg("datetime")) {
    String datetime = server.arg("datetime");
    int year = datetime.substring(0, 4).toInt();
    int month = datetime.substring(5, 7).toInt();
    int day = datetime.substring(8, 10).toInt();
    int hour = datetime.substring(11, 13).toInt();
    int minute = datetime.substring(14, 16).toInt();
    int second = datetime.substring(17, 19).toInt();  
    Time newTime(year, month, day, hour, minute, second, Time::kTuesday);
    rtc.time(newTime);
    server.send(200, "text/plain", "RTC updated");
  } else {
    server.send(400, "text/plain", "Missing datetime");
  }
}
void handleToggleLamp() {
  if (server.hasArg("state")) {
    String stateArg = server.arg("state");
    bool newState = (stateArg == "true");
  } else {
    server.send(400, "text/plain", "Missing parameter 'state'");
  }
}