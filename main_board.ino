// Add your custom file using "Sketch" -> "Add file"
#include "common_structs.h" //CHANNELS defined here 

const int t[CHANNELS+1] = {A4, A5, A7, A6, A2}; // pins + relay
const int tLed[CHANNELS+1] = {8, 7, 4, 3}; // pins
#define RELAY CHANNELS // relay out index in t array

#define INTERVAL 200 // interval check in miliseconds
#define ALARM_RELAY_ENABLE_PERIOD 3000 // 3 seconds of enabled relay

/**************
Variables
**************/

unsigned long currentMillis = 0;
unsigned long previousMillis = 0;
unsigned long alarmStartedMillis = 0;

// variables for adc vals 
int adc[CHANNELS] = {};

uint8_t alarmed_inputs = 0; //   Переменная которая каждый раз в цикле хранит количество датчиков которые находятся в состоянии тревоги
uint16_t ALARM_STARTS_BORDER = DEF_NUM_OF_ALARMED_INPUTS_TO_START_PANIC; // Сколько датчиков должны сработать для активации реле. Меняется через modbus
bool isAlarm = false;

const int BUF_CAP = 10; // Длина буфера
const int ADC_MAX_VAL = 1023; // Максимальное значение в буфере. Инициализируем им буфер чтобы алгоритм не сработал в самом начале после старта ллаты
const int ALARM_VAL_COUNT = 70; // Какой процент значений в буфере должны быть ниже порогового для срабатывания тревоги по этому каналу 
uint16_t ALARM_THRESHOLD_VAL[CHANNELS] = {DEF_ALARM_THRESHOLD_VAL, DEF_ALARM_THRESHOLD_VAL, DEF_ALARM_THRESHOLD_VAL, DEF_ALARM_THRESHOLD_VAL}; // Пороговое значние, ниже которого считается что пропал сигнал с датчика. Может управляться через modbus

int b[CHANNELS][BUF_CAP] = {{}};
uint16_t state = 0, prevState = 0;
String foo;

/**************
Buffers suite
**************/

void initBuffer(uint8_t const index) {
  for (int i=0; i<BUF_CAP; ++i)
    b[index][i] = ADC_MAX_VAL;
}

void updateBuffer(uint8_t const index) {
  for (int i=0; i<BUF_CAP-1; ++i)
    b[index][i] = b[index][i+1];
  b[index][BUF_CAP-1] = adc[index];
}

bool checkBuffer(uint8_t const index) {
  int t_vals = 0;
  for (int i=0; i<BUF_CAP; i++)
    if (b[index][i] < ALARM_THRESHOLD_VAL[index])
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
ModbusRTU mb;

#define SLAVE_ID 0 // Each board have to have it's own id. Not grater than MAX_BOARD_ID

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
  Serial.begin(SERIAL_SPEED);
}

void setupMisc() {
  for (uint8_t i=0; i<CHANNELS; ++i)
    pinMode(tLed[i], OUTPUT); 
  pinMode(t[RELAY], OUTPUT);   
  for (uint8_t i=0; i<CHANNELS; ++i)
    initBuffer(i);
  // Turn on leds for 3secs
  for (uint8_t i=0; i<CHANNELS; ++i)
    digitalWrite(tLed[i], HIGH);
  
  
  digitalWrite(t[RELAY], HIGH);
  delay(3000);
  digitalWrite(t[RELAY], LOW);
  
  for (uint8_t i=0; i<CHANNELS; ++i)
    digitalWrite(tLed[i], LOW);
}

void setupModbus() {
  // modbus io
  mb.begin(&Serial, MAX485_RE_DE);
  mb.setBaudrate(SERIAL_SPEED);
  mb.cbDisable();
  mb.server(SLAVE_ID);
  
  // modbus data
  mb.addCoil(g_getCoilAddress(0), false, COILS_AMOUNT); // COILS_AMOUNT
  mb.addIsts(g_getDiscreteInputAddress(0), 0, DISCRETE_INPUTS_AMOUNT); // DISCRETE_INPUTS_AMOUNT
  mb.addIreg(g_getInputRegisterAddress(0), 0, INPUT_REGISTERS_AMOUNT); // INPUT_REGISTERS_AMOUNT
  mb.addHreg(g_getHoldingRegisterAddress(0), 0, HOLDING_REGISTERS_AMOUNT); // HOLDING_REGISTERS_AMOUNT
}

void setup() {
  setupSerial();
  setupMisc();
  setupModbus();
  
  setStartFlag();
}

void syncParamsFromModbus() {
  if (mb.Coil(g_getCoilAddress(UPDATE_PARAMS_COIL))) {
    ALARM_STARTS_BORDER = mb.Hreg(g_getHoldingRegisterAddress(ALARM_STARTS_BORDER_REGISTER)); // update amount inputs for activating alarm
    for (uint8_t i=0; i<CHANNELS; ++i) // read register values into our buffer
      ALARM_THRESHOLD_VAL[i] = mb.Hreg(g_getHoldingRegisterAddress(i));
    mb.Coil(g_getCoilAddress(UPDATE_PARAMS_COIL), false); // change flag status to notify that new values readed and applied
  }
}

void setStartFlag() {
  mb.Coil(g_getCoilAddress(JUST_STARTED_COIL), true);
}

void checkADCs() {
  for (uint8_t i=0; i<CHANNELS; ++i) {
    adc[i]=analogRead(t[i]);
    mb.Ireg(g_getInputRegisterAddress(i), adc[i]);
  }

  for (uint8_t i=0; i<CHANNELS; ++i)
    updateBuffer(i);

  for (uint8_t i=0; i<CHANNELS; ++i) {
    if (checkBuffer(i)) {
      digitalWrite(tLed[i],HIGH); 
      state |= CH_STATE_MASKS[i]; // enable
      mb.Ists(g_getDiscreteInputAddress(i), false);
    }
    else {
      digitalWrite(tLed[i],LOW);
      state &= ~CH_STATE_MASKS[i]; // disable
      mb.Ists(g_getDiscreteInputAddress(i), true);
    }
  }

  alarmed_inputs = 0;
  for (uint8_t i=0; i<CHANNELS; ++i)
    if (state & CH_STATE_MASKS[i])
      alarmed_inputs++;

  if (alarmed_inputs>=ALARM_STARTS_BORDER) {
    state |= ALARM_STATE_MASK; // enable
    mb.Ists(ALARM_DISCRETE_INPUT, true);
  } else {
    mb.Ists(ALARM_DISCRETE_INPUT, false);
    state &= ~ALARM_STATE_MASK; // disable
  }
}

void loop() {
  currentMillis = millis();
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;  
    syncParamsFromModbus();
    checkADCs();
    prevState = state;
    if (state & ALARM_STATE_MASK) { // alarm started or active now
      alarmStartedMillis = currentMillis;
      digitalWrite(t[RELAY], HIGH); // turn on the alarm relay
    } else if (currentMillis - alarmStartedMillis >= ALARM_RELAY_ENABLE_PERIOD) {
      digitalWrite(t[RELAY], LOW);  // turn off the alarm relay
    }    
  }
  mb.task();
  yield();
}
