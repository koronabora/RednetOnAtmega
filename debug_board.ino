#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
//https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <UniversalTelegramBot.h>

// Add your custom file using "Sketch" -> "Add file"
#include "data/credentials.h"

const unsigned long INTERVAL = 1000;  // delay between main loop steps

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long currentMillis = 0;
unsigned long previousMillis = 0;
String foo;

#define GMT_OFFSET 60 * 60 * 3  // GMT+3

/**************
RS 485
**************/
//https://docs.arduino.cc/learn/built-in-libraries/software-serial
//#include <SoftwareSerial.h>
//#define RX_PIN D5
//#define TX_PIN D6
//SoftwareSerial RS485(RX_PIN, TX_PIN, false, 128);

#define MAX485_RE_DE D2

/**************
Modbus
**************/
//https://github.com/emelianov/modbus-esp8266
#include <ModbusRTU.h>
ModbusRTU mb;

#define NUM_COILS 1 // Security alarm relay state
#define NUM_DISCRETE_INPUTS 4 // Channels alarm statuses
#define NUM_INPUT_REGISTERS 4 // Current channels ADC values
#define NUM_HOLDING_REGISTERS 4 // Parameters for calculating alarm for channels

//#include "ModbusMaster.h"
//ModbusMaster node;

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
  /*pinMode(RX_PIN, INPUT);
  pinMode(TX_PIN, OUTPUT);
  RS485.begin(9600, SWSERIAL_8N1);
  if (!RS485) {
    logToChat("Invalid SoftwareSerial pin configuration!");
    while (1) {
      delay(1000);
    }
  }*/
  pinMode(MAX485_RE_DE, OUTPUT);
  digitalWrite(MAX485_RE_DE, LOW); // recieve mode
  Serial.begin(115200, SERIAL_8N1);
  Serial.swap(); // switch pins to D7 and D8
}

/*bool cbWrite(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  Serial.printf_P("Request result: 0x%02X, Mem: %d\n", event, ESP.getFreeHeap());
  return true;
}*/

void setupModbus() {
  mb.begin(&Serial, MAX485_RE_DE);
  mb.setBaudrate(115200);
  mb.client();
  //mb.master();
  //node.begin(16, RS485);
}

void setup() {
  setupWiFi();
  setupTime();
  logToChat("Connected to WiFi: " + String(WIFI_SSID));
  setupDisplay();
  setupRS485();
  setupModbus();
}

uint16_t fooBar;
uint16_t slaveId = 16;
void loop() {
  currentMillis = millis();
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;  

    if (!mb.slave()) {
      uint16_t res = mb.readHreg(slaveId, 1, &fooBar);
      foo = "Register readed: " + String(fooBar) + ". From slave: " + String(slaveId);
      logToChat(foo);
    }
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(20, 30, getCurrentTime());
    display.display();
  } //else
    //delay(INTERVAL - (currentMillis - previousMillis)); // correct because places in else statement

  mb.task();
  yield();
}