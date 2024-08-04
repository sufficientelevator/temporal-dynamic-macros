#pragma once
#include QMK_KEYBOARD_H

typedef enum {
	TURBO = SAFE_RANGE,
	LLOCK,
	TDM_RECORD,
	TDM_DELAY,
	TDM_END,
	TDM_PLAY,
	TDM_LOOP,
	TDM_SELECT,
	MACRO_RANGE_START
} custom_keycodes;


/* Key event container for recording */
// typedef struct {
//     keyevent_t event;
//     tap_t tap; //if tap enabled
//     uint16_t keycode; // if combo or repeat key enabled
// } keyrecord_t;

/* key event */
// typedef struct {
//     keypos_t        key;
//     uint16_t        time;
//     keyevent_type_t type;
//     bool            pressed;
// } keyevent_t;

/* key matrix position */
// typedef struct {
//     uint8_t col;
//     uint8_t row;
// } keypos_t;

// typedef enum keyevent_type_t { 
//     TICK_EVENT = 0, 
//     KEY_EVENT = 1, 
//     ENCODER_CW_EVENT = 2, E
//     NCODER_CCW_EVENT = 3, 
//     COMBO_EVENT = 4 
// } keyevent_type_t;

/* tapping count and state */
// typedef struct {
//     bool    interrupted : 1;
//     bool    reserved2 : 1;
//     bool    reserved1 : 1;
//     bool    reserved0 : 1;
//     uint8_t count : 4;
// } tap_t;
