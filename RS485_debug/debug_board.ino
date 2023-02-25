#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
//https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <UniversalTelegramBot.h>

// Add your custom file using "Sketch" -> "Add file"
#include "data/credentials.h"

const unsigned long BOT_MTBS = 5000;  // delay between main loop steps

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long bot_lasttime;
String foo;

#define GMT_OFFSET 60 * 60 * 3  // GMT+3

/**************
RS 485
**************/
//https://docs.arduino.cc/learn/built-in-libraries/software-serial
#include <SoftwareSerial.h>

#define RX_PIN D5
#define TX_PIN D6
SoftwareSerial RS485(RX_PIN, TX_PIN);

/**************
I2C Display
**************/
//https://github.com/ThingPulse/esp8266-oled-ssd1306
#include <Wire.h>
#include <SH1106Wire.h>
SH1106Wire display(0x3c, SDA, SCL);  // ADDRESS, SDA, SCL

/**************/

void logToChat(String message) {
  bot.sendMessage(TG_CHAT_ID, message, "");
}

String getCurrentTime() {
  time_t now;
  tm* timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  char buf[20];
  snprintf(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
           timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  return String(buf);
}

void setupTime() {
  configTime(GMT_OFFSET, 0, "pool.ntp.org");  // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    delay(100);
    now = time(nullptr);
  }
  logToChat("Time synchronized: " + getCurrentTime());
}

void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  secured_client.setTrustAnchors(&cert);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void setupDisplay() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.clear();
}

void setupRS485() {
  pinMode(RX_PIN, INPUT);
  pinMode(TX_PIN, OUTPUT);
  RS485.begin(9600);
  if (!RS485) {
    logToChat("Invalid SoftwareSerial pin configuration!");
    while (1) {
      delay(1000);
    }
  }
}

void setup() {
  setupWiFi();
  setupTime();
  logToChat("Connected to WiFi: " + String(WIFI_SSID));
  setupDisplay();
  setupRS485();
}

void loop() {
  if (millis() - bot_lasttime > BOT_MTBS) {
    bot_lasttime = millis();

    foo.clear();
    while (RS485.available() > 0) {
      foo.concat(RS485.read());
    }
    if (foo.length() > 0)
      logToChat("Readed from RS485: <" + foo + ">");

    RS485.println(getCurrentTime());

    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(20, 30, getCurrentTime());
    display.display();
  }
}
