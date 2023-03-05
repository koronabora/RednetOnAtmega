/*
TODO:
 - move isAlive to Ists
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>

//https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <UniversalTelegramBot.h>

//https://github.com/emelianov/modbus-esp8266
#include <ModbusRTU.h>

//https://github.com/ThingPulse/esp8266-oled-ssd1306
#include <Wire.h>
#include <SH1106Wire.h>

// Add your custom file using "Sketch" -> "Add file"
#include "credentials.h"
#include "common_structs.h"

#define GMT_OFFSET            60 * 60 * 3 // GMT+3
#define INTERVAL              1000        // delay between main loop steps
#define DISPLAY_LOG_CAPACITY  16          // capacity of display messages logs

#define _XDEBUG

namespace Glob {
  X509List cert(TELEGRAM_CERTIFICATE_ROOT);
  WiFiClientSecure securedClient;

  unsigned long currentMillis = 0;
  unsigned long previousMillis = 0;
  String logMessages[DISPLAY_LOG_CAPACITY];
}

/**************
Time & wifi funcs
**************/
namespace Tg {
 void logToChat(String const& message);
}

namespace Misc {
  using namespace Glob;

  String getCurrentDateTime() {
    time_t now;
    tm* timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
             timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buf);
  }
  
  String getCurrentTime() {
    time_t now;
    tm* timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buf);
  }

  void setupTime() {
    configTime(GMT_OFFSET, 0, "pool.ntp.org");  // get UTC time via NTP
    time_t now = time(nullptr);
    while (now < 24 * 3600) {
      delay(100);
      now = time(nullptr);
    }
    Tg::logToChat("Time synchronized: " + getCurrentTime());
  }

  void setupWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    securedClient.setTrustAnchors(&cert);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
  }

  template<typename T>
  String formatArray(T* arr, size_t len) {
    if (!arr)
      return String();
    String res = "[";
    for (size_t i=0; i<len; ++i) {
      res.concat(String(arr[i]));
      if ((i+1) != len)
        res.concat(", ");
    }
    res.concat("]");
    return res;
  }
  
  void addMessageToLog(String const& s) {
    for (uint16_t i=0; i<DISPLAY_LOG_CAPACITY-1; ++i)
      logMessages[i] = logMessages[i+1];
    logMessages[DISPLAY_LOG_CAPACITY-1] = s;
  }
}

/**************
Main board data struct
**************/
namespace Data {
  struct BoardStatusStruct {    
    // Flags
    bool isAlive = false; // mb.Coil
    bool isAlarmed = false; // mb.Ists
    String lastAlarmTimestamp;
    
    // Params
    bool inUpdateParamsState = false; // mb.Coil
    uint16_t thresholdParamValues[CHANNELS]; // mb.Hreg
    uint16_t alarmedInputsParamValue = DEF_NUM_OF_ALARMED_INPUTS_TO_START_PANIC; // mb.Hreg
    
    // Channels statuses and values
    uint16_t channelValues[CHANNELS]; // mb.Iregs
    bool channelStates[CHANNELS]; // Ists
    
    // -----------
    BoardStatusStruct() {
      for (uint8_t i=0; i<CHANNELS; ++i) {
        thresholdParamValues[i] = DEF_ALARM_THRESHOLD_VAL;
        channelValues[i] = 0;
        channelStates[i] = false;
      };
    };
    void updateParam(uint16_t tokenIndex, uint16_t const value) {
      if (value>0) {
        if (tokenIndex == CHANNELS)
          alarmedInputsParamValue = value;
        else if (tokenIndex < CHANNELS)
          thresholdParamValues[tokenIndex] = value;
        else
          Tg::logToChat("Некорректный индекс параметра!");
      }
    }
  };

  BoardStatusStruct boards[MAX_BOARD_ID];
}

/**************
Debug
**************/
namespace Debug {
#ifdef _XDEBUG
  //SoftwareSerial debugSerial(D0, D1);
#endif

  void message(String const& s) {
#ifdef _XDEBUG
    //debugSerial.println(Misc::getCurrentDateTime() + ": " + s);
    Tg::logToChat(Misc::getCurrentDateTime() + ": " + s);
#endif
  }
}

/**************
RS 485
**************/
#define MAX485_RE_DE D2

namespace RS485 {
  void setupRS485() {
    pinMode(MAX485_RE_DE, OUTPUT);
    digitalWrite(MAX485_RE_DE, LOW); // recieve mode
    Serial.begin(SERIAL_SPEED);
    Serial.swap(); // switch pins to GPIO13 and GPIO15
  }
}

/**************
Modbus
**************/
namespace MBus {
  using namespace Data;
  
  uint8_t const MAX_APPLYING_CYCLES = 3; // how much cycles wait for applying new settings
  
  ModbusRTU mb;
  
  bool alarmStatesBuf[MAX_BOARD_ID];
  bool aliveStatesBuf[MAX_BOARD_ID];
  
  uint8_t cyclesCount = 0;
  
  void setupModbus() {
    mb.begin(&Serial, MAX485_RE_DE);
    mb.setBaudrate(SERIAL_SPEED);
    mb.cbDisable();
    mb.client();
  }
  
  void modbusFetch(uint8_t const& boardId, bool isPartial = true) {
    aliveStatesBuf[boardId] = boards[boardId].isAlive;
    mb.readCoil(boardId, g_getCoilAddress(JUST_STARTED_COIL), &boards[boardId].isAlive);
    alarmStatesBuf[boardId] = boards[boardId].isAlarmed;
    mb.readIsts(boardId, g_getDiscreteInputAddress(ALARM_DISCRETE_INPUT), &boards[boardId].isAlarmed);
    if (!isPartial) {
      mb.readCoil(boardId, g_getCoilAddress(UPDATE_PARAMS_COIL), &boards[boardId].inUpdateParamsState);
      //for (uint8_t i=0; i<CHANNELS; ++i)
      //  mb.readIsts(boardId, g_getDiscreteInputAddress(i), &(boards[boardId].channelStates[i]));
      mb.readIsts(boardId, g_getDiscreteInputAddress(0), &(boards[boardId].channelStates[0]), DISCRETE_INPUTS_AMOUNT-1);
      
      //for (uint8_t i=0; i<CHANNELS; ++i)
      //  mb.readIreg(boardId, g_getInputRegisterAddress(i), &(boards[boardId].channelValues[i]));
      mb.readIreg(boardId, g_getInputRegisterAddress(0), &(boards[boardId].channelValues[0]), INPUT_REGISTERS_AMOUNT);
      
      mb.readHreg(boardId, g_getHoldingRegisterAddress(ALARM_STARTS_BORDER_REGISTER), &boards[boardId].alarmedInputsParamValue);
      //for (uint8_t i=0; i<CHANNELS; ++i)
      //  mb.readHreg(boardId, g_getHoldingRegisterAddress(i), &(boards[boardId].thresholdParamValues[i]));
      mb.readHreg(boardId, g_getHoldingRegisterAddress(0), &(boards[boardId].thresholdParamValues[0]), HOLDING_REGISTERS_AMOUNT-1);
    }
    mb.task();
    yield();
  }
  
  void modbusPush(uint8_t const& boardId) {
    mb.writeHreg(boardId, g_getHoldingRegisterAddress(ALARM_STARTS_BORDER_REGISTER), &boards[boardId].alarmedInputsParamValue);
    //for (uint8_t i=0; i<CHANNELS; ++i) // read register values into our buffer
    //  mb.writeHreg(boardId, g_getHoldingRegisterAddress(i), &(boards[boardId].thresholdParamValues[i]));
    mb.writeHreg(boardId, g_getHoldingRegisterAddress(0), &(boards[boardId].thresholdParamValues[0]), HOLDING_REGISTERS_AMOUNT-1);
    mb.writeCoil(boardId, g_getCoilAddress(UPDATE_PARAMS_COIL), &boards[boardId].inUpdateParamsState); 
    
    mb.task();
    yield();
  }
  
  void syncBoardsAlarmStates() { // regular sync
    Debug::message("Start of reading regular data");
    for (uint8_t i=0; i<MAX_BOARD_ID; ++i)
      modbusFetch(i, true);
    
    for (uint8_t i=0; i<MAX_BOARD_ID; ++i) {
      if (boards[i].isAlarmed) { // check for alarm state cnahges
        boards[i].lastAlarmTimestamp = Misc::getCurrentDateTime();
        if (!alarmStatesBuf[i])
          Misc::addMessageToLog("Сработала плата #" + String(i+1));
      }
      if (boards[i].isAlive && !aliveStatesBuf[i]) // check for boards became online
        Misc::addMessageToLog("Плата #" + String(i+1) + " онлайн!");
      else if (!boards[i].isAlive && aliveStatesBuf[i]) // check for board became offline
        Misc::addMessageToLog("Плата #" + String(i+1) + " оффлайн!");
    }
    Debug::message("End of reading regular data");
  }

  void syncBoardInfo(uint16_t const clientId) { // comlete sync on-demand for client
    Debug::message("Start of reading entire data for board #" + String(clientId));
    modbusFetch(clientId, false);
    Debug::message("End of reading entire data for board #" + String(clientId));
  }
  
  bool applyNewParams(uint16_t const clientId) {
    Debug::message("Start of applying new params for board #" + String(clientId));
    boards[clientId].inUpdateParamsState = true;
    modbusPush(clientId);
    
    cyclesCount = 0;
    while (cyclesCount < MAX_APPLYING_CYCLES && boards[clientId].inUpdateParamsState) {
      mb.readCoil(clientId, g_getCoilAddress(UPDATE_PARAMS_COIL), &boards[clientId].inUpdateParamsState);
      mb.task();
      yield();
      ++cyclesCount;
    }
    Debug::message("End of applying new params  for board #" + String(clientId));
    if (!boards[clientId].inUpdateParamsState)
      return false;
    return true;
  }
}

/**************
Telegram funcs
**************/
namespace Tg {
  UniversalTelegramBot bot(BOT_TOKEN, Glob::securedClient);

  String const STATUS_COMMAND = "/status";
  String const DETAILS_COMMAND = "/details";
  String const SET_PARAMS_COMMAND = "/set_params";
  char const PARAMS_DELIM = ' ';
  
  String const LINE_SPLITTER = "----------------------------";
  
  String lastCommand;
  int numNewMessages = 0;
  String fooBar;
  uint16_t pos = 0;
  uint16_t token = 0;
  uint16_t fooVal = 0;
  int8_t index = -1;
  
  void logToChat(String const& message) {
    bot.sendMessage(TG_CHAT_ID, message, "");
  }

  void sendBoardInfo(uint16_t const clientId, String const& chatId) {
    if (clientId >= MAX_BOARD_ID) {
      logToChat("Некорректный номер платы: " + String(clientId));
      return;
    }
    fooBar = "#" + String(clientId+1) + "\n";
    fooBar.concat(" - активен: " + String(Data::boards[clientId].isAlive ? "да" : "нет") + "\n");
    fooBar.concat(" - тревога: " + String(Data::boards[clientId].isAlarmed ? "да" : "нет") + "\n");
    fooBar.concat(" - последняя тревога: " + Data::boards[clientId].lastAlarmTimestamp + "\n");
    fooBar.concat(LINE_SPLITTER + "\n");
    fooBar.concat(" - ожидание применения параметров: " + String(Data::boards[clientId].inUpdateParamsState ? "да" : "нет") + "\n");
    fooBar.concat(" - параметры ADC: " + Misc::formatArray<uint16_t>(&(Data::boards[clientId].thresholdParamValues[0]), CHANNELS) + "\n");
    fooBar.concat(" - каналов в тревоге для срабатывания: " + String(Data::boards[clientId].alarmedInputsParamValue) + "\n");
    fooBar.concat(LINE_SPLITTER + "\n");
    fooBar.concat(" - значения по-каналам: " + Misc::formatArray<uint16_t>(&(Data::boards[clientId].channelValues[0]), CHANNELS) + "\n");
    fooBar.concat(" - состояния по-каналам: " + Misc::formatArray<bool>(&(Data::boards[clientId].channelStates[0]), CHANNELS) + "\n");
    bot.sendMessage(chatId, fooBar, "");
  }

  void sendStatuses(String const& chatId) {
    fooBar = "";
    pos = 0;
    for (uint8_t i=0; i<MAX_BOARD_ID; ++i)
      if (Data::boards[i].isAlive) {
        fooBar = "#" + String(i+1) + "\n";
        fooBar.concat(" - тревога: " + String(Data::boards[i].isAlarmed ? "да" : "нет") + "\n");
        fooBar.concat(" - последняя тревога: " + Data::boards[i].lastAlarmTimestamp + "\n");
        fooBar.concat(LINE_SPLITTER + "\n");
        ++pos;
      }
    fooBar.append("Всего " + String(pos) + " плат");
    bot.sendMessage(chatId, fooBar, "");
  }
  
  bool updateParamsFromString(String const& s) {
    pos = 0;
    fooBar = "";
    fooVal = 0;
    index = -1;
    while (pos<s.length()) {
      if (s[pos] == PARAMS_DELIM) {
        if (fooBar.length() > 0) {
          Debug::message("Parsing token: " + fooBar);
          fooVal = fooBar.toInt();
          if (index>=0) { // adc borders and other params
            Data::boards[index].updateParam(index, fooVal);
            ++index;
          } else {
            index = fooVal; // first param - id of board
          }
        } else {
          Debug::message("Empty token at pos <" + String(pos) + "> in string <" + s + ">");
        }
      } else {
        fooBar.concat(s[pos]);
      }
      ++pos;
    }
    return MBus::applyNewParams(index);
  }
  
  void handleNewMessages() {
    for (int i = 0; i < numNewMessages; i++) {
      String const& chatId = bot.messages[i].chat_id;
      String const& text = bot.messages[i].text;
      if (text.length() > 0) {
        if (text == STATUS_COMMAND) {
          sendStatuses(chatId); // modbus values already updated in main loop
        } else if (text == DETAILS_COMMAND) {
          bot.sendMessage(chatId, "Введите номер платы");
        } else if (text == SET_PARAMS_COMMAND) {
          bot.sendMessage(chatId, "Введите через пробел номер платы и параметры (сначала границы ADC, потом кол-во входов для срабатывания). Ноль не применяется.");
        } else { // Try to parse board id after details or set_params command
          if (lastCommand == DETAILS_COMMAND) { // send board info
            index = static_cast<uint16_t>(text.toInt());
            MBus::syncBoardInfo(index);
            sendBoardInfo(index, chatId);
          } else if (lastCommand == SET_PARAMS_COMMAND) {
            if (!updateParamsFromString(text))
              fooBar = "Не удалось применить параметры для платы #" + String(index+1);
            else
              fooBar = "Параметры для платы #" + String(index+1) + " применены!";
            Misc::addMessageToLog(fooBar);
            bot.sendMessage(chatId, fooBar);
          } else {
            bot.sendMessage(chatId, "Некорректный запрос <" + text + ">");
          }
        }
        lastCommand = text;
      }
    }
  }

  void checkForBotUpdates() {
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages>0) {
      handleNewMessages();
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
  }
}

/**************
I2C Display
**************/
namespace Display {
  using namespace Glob;
  
  SH1106Wire display(0x3c, SDA, SCL);  // ADDRESS, SDA, SCL
  
  uint8_t const maxX = 128;
  uint8_t const maxY = 64;
  
  uint8_t const padding = 4;
  uint8_t const stringHieght = (maxX-padding*2)/(DISPLAY_LOG_CAPACITY+1); // display is flipped
  
  void setupDisplay() {
    display.init();
    display.flipScreenVertically();
    display.clear();
  }
  
  uint8_t justifyToCenter(String const& s) { // don't add padding
    return (maxY - padding * 2 - s.length())/2;
  }
  
  void updateDisplay() {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    // Draw datetime
    String const currentDateTime = Misc::getCurrentDateTime();
    display.drawString(padding+justifyToCenter(currentDateTime), padding+stringHieght*1, currentDateTime);
    // Draw messages
    for (uint16_t i=0; i<DISPLAY_LOG_CAPACITY; ++i)
      if (logMessages[i].length() > 0)
        display.drawString(padding, padding+stringHieght*(i+1), logMessages[i]);
    display.display();
  }
}


/**************/

void setup() {
#ifdef _XDEBUG
  //Debug::debugSerial.begin(9600);
#endif
  
  Debug::message("Initialization started");
  Misc::setupWiFi();
  Misc::setupTime();
  Tg::logToChat("Connected to WiFi: " + String(WIFI_SSID));
  Display::setupDisplay();
  RS485::setupRS485();
  MBus::setupModbus();
  Debug::message("Initialization finished");
}

void loop() {
  Glob::currentMillis = millis();
  if (Glob::currentMillis - Glob::previousMillis >= INTERVAL) {
    Glob::previousMillis = Glob::currentMillis;  
    Debug::message("Sync step");
    MBus::syncBoardsAlarmStates();
    Tg::checkForBotUpdates();
    Display::updateDisplay();
  } 
  MBus::mb.task();
  yield();
}