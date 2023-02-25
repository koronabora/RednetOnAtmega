#define CHANNELS 4

const int t[CHANNELS+1] = {A4, A5, A7, A6, A2}; // pins + relay
const int tLed[CHANNELS+1] = {8, 7, 4, 3}; // pins
#define RELAY CHANNELS // relay out index in t array

#define INTERVAL 2000 // interval check in miliseconds

/**************
Variables
**************/

unsigned long currentMillis = 0;
unsigned long previousMillis = 0;

// variables for adc vals 
int adc[CHANNELS] = {};

uint8_t alarmed_inputs = 0; //   Переменная которая каждый раз в цикле хранит количество датчиков которые находятся в состоянии тревоги
const uint8_t ALARM_STARTS_BORDER = 3; // Сколько датчиков должны сработать для активации реле
bool isAlarm = false;

const int BUF_CAP = 10; // Длина буфера
const int ADC_MAX_VAL = 1023; // Максимальное значение в буфере. Инициализируем им буфер чтобы алгоритм не сработал в самом начале после старта ллаты
const int ALARM_VAL_COUNT = 70; // Какой процент значений в буфере должны быть ниже порогового для срабатывания тревоги по этому каналу 
const int ALARM_THRESHOLD_VAL = 100; // Пороговое значние, ниже которого считается что пропал сигнал с датчика

int b[CHANNELS][BUF_CAP] = {{}};
uint16_t state = 0, prevState = 0;
String foo;

/**************
Buffers suite
**************/

void initBuffer(int* buf) {
  if (buf!=0)
      for (int i=0; i<BUF_CAP; ++i)
        buf[i] = ADC_MAX_VAL;
}

void updateBuffer(int* buf, const int& v) {
   if (buf!=0) {
       for (int i=0; i<BUF_CAP-1; ++i)
          buf[i] = buf[i+1];
       buf[BUF_CAP-1] = v;
   }
}

bool checkBuffer(const int* buf) {
  int t_vals = 0;
  if (buf!=0)
      for (int i=0; i<BUF_CAP; i++)
        if (buf[i] < ALARM_THRESHOLD_VAL)
          t_vals++;
  if (t_vals>(BUF_CAP*(ALARM_VAL_COUNT/100.0)))
    return true;
  return false; 
}

/**************
RS485
**************/
#define MAX485_RE_DE   A3

void preTransmission() {
  digitalWrite(MAX485_RE_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_RE_DE, LOW);
}
/**************
Modbus
**************/
//https://github.com/emelianov/modbus-esp8266
#include <ModbusRTU.h>

#define SLAVE_ID 1

#define NUM_COILS 1 // Security alarm relay state
#define NUM_DISCRETE_INPUTS 4 // Channels alarm statuses
#define NUM_INPUT_REGISTERS 4 // Current channels ADC values
#define NUM_HOLDING_REGISTERS 4 // Parameters for calculating alarm for channels

ModbusRTU mb;

/**************
Bitmasks
**************/
const uint16_t ALARM_STATE_MASK = 0x0001; // Main alarm
const uint16_t CH_STATE_MASKS[CHANNELS] = {
    0x0002, // Channel 0
    0x0004, // Channel 1
    0x0008, // Channel 2
    0x0010  // Channel 3
  };

int16_t encodeState(bool const isAlarm, bool* const inputStates) {
  int16_t res = 0;
  if (isAlarm) 
    res |= ALARM_STATE_MASK;
  else
    res &= ~ALARM_STATE_MASK;
  for (uint8_t i=0; i<CHANNELS; ++i) {
    if (inputStates[i]) 
      res |= CH_STATE_MASKS[i];
    else
      res &= ~CH_STATE_MASKS[i];
  }
  return res;
}

void decodeStates(uint16_t const state, bool& isAlarm, bool* inputStates) {
  isAlarm = (state & ALARM_STATE_MASK);
  for (uint8_t i=0; i<CHANNELS; ++i)
    inputStates[i] = (state & CH_STATE_MASKS[i]);
}
//--------------

void setupSerial() {
  pinMode(MAX485_RE_DE, OUTPUT);
  Serial.begin(9600);
}

void setupMisc() {
  for (uint8_t i=0; i<CHANNELS; ++i)
    pinMode(tLed[i], OUTPUT); 
  pinMode(t[RELAY], OUTPUT);   
  for (uint8_t i=0; i<CHANNELS; ++i)
    initBuffer(b[i]);
  // Turn on leds for 3secs
  for (uint8_t i=0; i<CHANNELS; ++i)
    digitalWrite(tLed[i], HIGH);
  delay(3000);
  for (uint8_t i=0; i<CHANNELS; ++i)
    digitalWrite(tLed[i], LOW);
}

void setupModbus() {
  mb.begin(&Serial, MAX485_RE_DE);  //or use RX/TX direction control pin (if required)
  mb.setBaudrate(9600);
  mb.slave(SLAVE_ID);
  
  mb.addCoil(1);
}

void setup() {
  setupSerial();
  setupMisc();
  setupModbus();
}

void sendState(int16_t val) {
    preTransmission();
    Serial.println(String(state));
    postTransmission();
}

void sendString(String const& s) {
    preTransmission();
    Serial.println(s);
    postTransmission();
}

void checkADCs() {
  for (uint8_t i=0; i<CHANNELS; ++i)
    adc[i]=analogRead(t[i]);

  for (uint8_t i=0; i<CHANNELS; ++i)
    updateBuffer(b[i], adc[i]);

  for (uint8_t i=0; i<CHANNELS; ++i) {
    if (checkBuffer(b[i])) {
      digitalWrite(tLed[i],HIGH); 
      state |= CH_STATE_MASKS[i]; // enable
    }
    else {
      digitalWrite(tLed[i],LOW);
      state &= ~CH_STATE_MASKS[i]; // disable
    }
  }

  alarmed_inputs = 0;
  for (uint8_t i=0; i<CHANNELS; ++i)
    if (state & CH_STATE_MASKS[i])
      alarmed_inputs++;

  state &= ~ALARM_STATE_MASK; // disable
  if (alarmed_inputs>=ALARM_STARTS_BORDER)
    state |= ALARM_STATE_MASK; // enable
}

bool bar = false;
void loop() {
  currentMillis = millis();
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;  

    checkADCs();
    prevState = state;
    
    bar = !bar;
    mb.writeCoil(SLAVE_ID, 1, bar);
    
    mb.task();
    
  } else
    delay(INTERVAL - (currentMillis - previousMillis)); // correct because places in else statement
}
