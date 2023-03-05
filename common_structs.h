#ifndef COMMON_STRUCTS_H
#define COMMON_STRUCTS_H

#define MAX_BOARD_ID 8

#define CHANNELS 4

// Coils as r/w flags
#define COILS_AMOUNT 2 // Setting update flag state, afterboot flag state
#define UPDATE_PARAMS_COIL 0
#define JUST_STARTED_COIL 1

// Discrete inputs as r/o flags
#define DISCRETE_INPUTS_AMOUNT CHANNELS+1 // Alarm statuses by channels, security alarm flag
#define ALARM_DISCRETE_INPUT CHANNELS

// Input registers as r/o variables
#define INPUT_REGISTERS_AMOUNT CHANNELS // ADC values by channels

// Holding registers as r/w variables
#define HOLDING_REGISTERS_AMOUNT CHANNELS+1 // Parameters for channels
#define ALARM_STARTS_BORDER_REGISTER CHANNELS

// Some common default params
#define DEF_ALARM_THRESHOLD_VAL 100 // Default threshold value for alarms in 0-1024 values range
#define DEF_NUM_OF_ALARMED_INPUTS_TO_START_PANIC 3
#define SERIAL_SPEED 115200

inline uint16_t g_getCoilAddress(uint16_t const coil) {
  return 10 + coil;
};

inline uint16_t g_getDiscreteInputAddress(uint16_t const discreteInput) {
  return 100 + discreteInput;
};

inline uint16_t g_getInputRegisterAddress(uint16_t const inputRegister) {
  return 1000 + inputRegister;
};

inline uint16_t g_getHoldingRegisterAddress(uint16_t const holdingRegister) {
  return 10000 + holdingRegister;
};

#endif