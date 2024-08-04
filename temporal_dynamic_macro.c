// Copyright 2024 Jack Bellinger
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//		 https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file temporal_dynamic_macro.c
 * @brief Temporal Dynamic Macro implementation
 *
 * For full documentation, see
 * <https://jackbellinger.github.io/blog/articles/qmk-temporal-dynamic-macro>
 
 users cannot hold a layer key during a delay
Tapdances currently can't be used in a tdm, since I'm using register_code, it doesn't store taps or combos (when the tapdance is) so register code must not trigger taps
Layer keys are filtered out, only the resulting keycode will be stored.
 
 you can't add a delay at the end of a macro without another key following it. If you'd like to end with a delay for a loop and not have any following key before the start of the macro, you'll need to enter a KC_NO
 */

#include "temporal_dynamic_macro.h"
#include "custom_keycodes.h"
#include "rgblight.h"
#define RGBLIGHT_LED_COUNT 19
#if !defined(DEFERRED_EXEC_ENABLE)
#error "temporal_dynamic_macro: Please set `DEFERRED_EXEC_ENABLE = yes` in rules.mk."
#endif

/* User hooks for Temporal Dynamic Macros
 * functions which can be overridden by the user to customize functionality.
 * tdm_is_valid_key_user allows the user to narrow what keys are allowed to be in a macro. 
 *     Normally the only restriction is that only numeric keys can be entered while recording a delay
 * 
 */
// default feedback method
void tdm_led_blink(void) {
#ifdef BACKLIGHT_ENABLE
	backlight_toggle();
	wait_ms(100);
	backlight_toggle();
#endif
#ifdef RGBLIGHT_ENABLE
	tdm_rgb_user();
#endif
}

void tdm_led_double_blink(void) {
	tdm_led_blink();
	wait_ms(100);
	tdm_led_blink();
}
void print_macros(void);
__attribute__((weak)) void tdm_rgb_user() {
	// Turn underglow LEDs off (all LEDs to black)
	for (int i = 0; i < RGBLIGHT_LED_COUNT; i++) {
		rgblight_setrgb_at(0,0,0, i);  // Set individual LED to black (off)
	}

	// Short delay (adjust as needed)
	wait_ms(50);  // Example delay in milliseconds

	// Turn underglow LEDs on (set your desired color)
	for (int i = 0; i < RGBLIGHT_LED_COUNT; i++) {
		rgblight_setrgb_at(RGB_WHITE, i);  // Example, change to your color
	}

	// Short delay (adjust as needed)
	wait_ms(50);  // Example delay in milliseconds
}
__attribute__((weak)) void tdm_init_user(void) {
	tdm_led_blink();
}
__attribute__((weak)) void tdm_record_start_user(uint8_t MACRO_id) {
	tdm_led_blink();
	// print_macros();
}
__attribute__((weak)) bool tdm_is_valid_key_user(uint16_t keycode) {
	return true;
}
__attribute__((weak)) void tdm_record_key_user(uint8_t MACRO_id, uint16_t keycode) {
	// uprintf("recording key: %d\n", keycode);
	print_macros();
	tdm_led_blink();
}
__attribute__((weak)) void tdm_record_end_user(uint8_t MACRO_id) {
	tdm_led_blink();
	print_macros();
}
__attribute__((weak)) void tdm_play_user(uint8_t M_id) {
	uprintf("playing macro: %d\n", M_id);
	tdm_led_blink();
}
__attribute__((weak)) void tdm_play_stop_user(uint8_t M_id) {
	uprintf("done playing macro: %d\n", M_id);
	tdm_led_blink();
}

/* Buffer state
 * stores the recorded macros, their lengths, and iteration position
 * MACRO_buffers: the array of keyrecords.
 * MACRO_ends: pointers to the end of the macro
 * current_pointer: macro iteration pointer
 */
typedef struct {
	uint16_t keycode;
	uint32_t delay_ms;
	uint8_t flags; //butmask set by tdm_key_flags
	// TODO figure out what other fields I would need to support various qmk features
	//			(combo keys used in tdm, tap dances, etc)
} tdm_keypress_t;

/*
 * bitmask for key metadata flags
 * FLAG_pressed: if the key was pressed down or released
 * 1-8: unused, reserved for future extensions to support 
*/
#define FLAG_pressed (1u)
#define FLAG_1 (1u << 1)
#define FLAG_3 (1u << 2)
#define FLAG_4 (1u << 3)
#define FLAG_5 (1u << 4)
#define FLAG_6 (1u << 5)
#define FLAG_7 (1u << 6)
#define FLAG_8 (1u << 7)
// Function to set or clear one flag inside the bitmask based on the boolean value
static inline void set_flag(tdm_keypress_t *keypress, uint8_t flag, bool is_set) {
	keypress->flags ^= (flag & -is_set) ^ ((keypress->flags) & flag);
}
static inline bool is_set(tdm_keypress_t *keypress, uint8_t flag){
	return (bool)(keypress->flags & flag);
}
static inline void clear_flags(tdm_keypress_t *keypress) {
	keypress->flags = 0;
}

/* Macro Buffers: 2D array that stores the keyrecords for each step of a macro
 * Each macro shares a buffer but read/write on different
 * ends of it.
 *
 * Macro1 is written left-to-right starting from the beginning of
 * the buffer.
 *
 * Macro2 is written right-to-left starting from the end of the
 * buffer.
 *
 * &macro_buffer   MACRO_end
 *  v                   
 * +------------------------------------------------------------+
 * |>>>>>> MACRO1 >>>>>>      <<<<<<<<<<<<< MACRO2 <<<<<<<<<<<<<|
 * +------------------------------------------------------------+
 *                           ^                                 ^
 *                         r_MACRO_end                  r_macro_buffer
 *
 * During the recording when one macro encounters the end of the
 * other macro, the recording is stopped. Apart from this, there
 * are no arbitrary limits for the macros' length in relation to
 * each other: for example one can either have two medium sized
 * macros or one long macro and one short macro. Or even one empty
 * and one using the whole buffer.
 */
static tdm_keypress_t MACRO_buffers[(TDM_NUM_MACROS + 1) / 2][TDM_BUFFER_SIZE];

/* Pointers to the end of each buffer
 * initially each buffer is empty, so each end pointer starts at the beginning.
 * points to the index after the last keycode in the macro
 * (if end points to n, last filled spot is n-1)
 */
static tdm_keypress_t* MACRO_ends[(TDM_NUM_MACROS + (TDM_NUM_MACROS % 2))];

/* Bookkeeping state
 * tracks what process the user currently in
 * validates what actions can be taken next
 */

/* 0   - no macro is being recorded right now
* 1,2,..TDM_NUM_MACROS - macro 1, 2, or n is being recorded or played */
static uint8_t MACRO_id = 0;

// pointer to the start of the current macro buffer
static tdm_keypress_t* MACRO_start;
//pointer to the current macro position (iterator)
static tdm_keypress_t* MACRO_iterator = NULL;
//pointer to the current macro neighbor end (last empty buffer space)
static tdm_keypress_t* MACRO_end;

/* The MACRO_direction of the current macro, even(0,2..) macros go ->LtR->, odd macros go <-RtL<- 
* either 1 or -1
*/
static int8_t MACRO_direction = 1;

// The MACRO_delay_next_key_ms stores the number while inputting a delay
static uint32_t MACRO_delay_next_key_ms = 0;

typedef enum {
	STATE_recording,
	STATE_recording_delay,
	STATE_playing,
	STATE_looping,
	STATE_selecting,
	STATE_idle
} State;

const char* state_to_string(State st) {
	switch (st) {
		case STATE_recording:
			return "recording";
		case STATE_recording_delay:
			return "recorging delay";
		case STATE_playing:
			return "playing";
		case STATE_looping:
			return "looping";
		case STATE_selecting:
			return "selecting";
		case STATE_idle:
			return "idle";
	}
	return "idle";
}

static State MACRO_current_state = STATE_idle;

State keycode_to_state(uint16_t keycode){
	//if the keycode isn't a control key, then next state is idle unless it's recording a delay.
	State key_state = STATE_idle; 
	switch (keycode) {
		case TDM_RECORD:
			key_state = STATE_recording;
			break;
		case TDM_DELAY:
			key_state = STATE_recording_delay;
			break;
		case TDM_END:
			key_state = STATE_idle;
			break;
		case TDM_PLAY:
			key_state = STATE_playing;
			break;
		case TDM_LOOP:
			key_state = STATE_looping;
			break;
		case TDM_SELECT:
			key_state = STATE_selecting;
			break;
	}
	return key_state;
}

void reset_state(void);
void tdm_init_state_machine(void);
void tdm_init_user(void);

void tdm_init(void) {
	reset_state();
	tdm_init_state_machine();
	tdm_init_user();
}

int keycode_to_int(uint16_t keycode) {
	// Map values 30-38 to 1-9 and value 39 to 0
	if (keycode >= 30 && keycode <= 39) { //num row keys
		return (keycode == 39) ? 0 : (keycode - 29);
	} else if (keycode >= 89 && keycode <= 98) { //num pad keys
		return (keycode == 98) ? 0 : (keycode - 88);
	} else {
		return -1; // Return an error code for values outside the mapping range
	}
}

static uint8_t MACRO_selection = 0;
void tdm_select_start(void) {
	uprintf("selecting\n");
	MACRO_selection = 0;
}

void tdm_select_macro(uint16_t keycode) {
	if (MACRO_selection >= TDM_NUM_MACROS) {
		return;
	}
	int key_val = keycode_to_int(keycode);
	if (key_val == -1) { 
		uprintf("temporal dynamic macro: only numeric keys are valid in macro select");
		return;
	}
	MACRO_selection *= 10;
	MACRO_selection += key_val;
}

void tdm_select_end(void) {
	clear_keyboard();
	layer_clear();
	uprintf("selection: %d\n", MACRO_selection);
	if (MACRO_selection >= TDM_NUM_MACROS)
		MACRO_id = TDM_NUM_MACROS -1;
	else
		MACRO_id = MACRO_selection;
	uprintf("selected macro: %d\n", MACRO_id);
}

/* Convenience macros used for retrieving state.
 * MACRO_id must be defined
 */
#define DIRECTION(M_id) ((M_id & 1) * -2 + 1)
#define NEIGHBOR(x) ((x) + 1 - 2 * ((x) % 2))
#define TDM_BUFFER_START(M_id) (&MACRO_buffers[M_id / 2][0])
#define TDM_CURRENT_START(M_id) (TDM_BUFFER_START(M_id) + ((M_id & 1) * TDM_BUFFER_SIZE))
#define TDM_CURRENT_LENGTH(POINTER) (DIRECTION(MACRO_id) * (POINTER - TDM_CURRENT_START(MACRO_id)))
#define TDM_CURRENT_CAPACITY(M_id) (DIRECTION(M_id) * (MACRO_ends[NEIGHBOR(M_id)] - TDM_CURRENT_START(M_id)))
#define TDM_ITERATOR_AT_START() (MACRO_iterator == MACRO_start)

void reset_state(void) {
	for (int i = 0; i < TDM_NUM_MACROS; i++) {
		MACRO_ends[i] = TDM_CURRENT_START(i);
	}
	MACRO_start = MACRO_buffers[0];
	MACRO_iterator = NULL;
	MACRO_end = MACRO_ends[0];
}

static bool play_finished;
void tdm_reset_iterator(void) {
	MACRO_direction = DIRECTION(MACRO_id);
	MACRO_start = TDM_CURRENT_START(MACRO_id);
	MACRO_iterator = MACRO_start;
	MACRO_end = MACRO_ends[MACRO_id];
	play_finished = false;
}

void tdm_record_start(void);
void tdm_record_key(uint16_t keycode, keyrecord_t* record);
void tdm_record_delay_start(void);
void tdm_record_delay(uint16_t keycode);
void tdm_record_delay_end(void);
void tdm_record_end(void);
/**
 * Start recording of the dynamic macro.
 *
 * @param[in]  record  The record of the key that was pressed
 */
void tdm_record_start(void) {
	uprintf("temporal dynamic macro: recording into macro# %d\n", MACRO_id);

	tdm_record_start_user(MACRO_id);

	clear_keyboard();
	layer_clear();
	tdm_reset_iterator();
}

/**
 * Record a single key in a dynamic macro.
 *
 * @param macro_buffer[in] The start of the used macro buffer.
 * @param current_pointer[in,out] The current buffer position.
 * @param macro2_end[in] The end of the other macro.
 * @param record[in]	 The current keypress.
 */
void tdm_record_key(uint16_t keycode, keyrecord_t* record) {
	/* If we've just started recording, ignore all the key releases. */
	
	static bool got_first_keydown = false;
	if (!record->event.pressed && !got_first_keydown) {
		uprintf("temporal dynamic macro: ignoring a leading key-up event\n");
		return;
	} else {
		got_first_keydown = true;
	}

	//TODO
	//if the pointer is past the neighbor's end, it's overlapping so end the macro and return
	if (MACRO_iterator - MACRO_direction == MACRO_ends[NEIGHBOR(MACRO_id)]) {
		tdm_record_end();
		return;
	}
	
	MACRO_iterator->keycode = keycode;
	
	set_flag(MACRO_iterator, FLAG_pressed, record->event.pressed);
	
	MACRO_iterator += MACRO_direction;
	//clear any old data
	MACRO_iterator->delay_ms = 0;
	clear_flags(MACRO_iterator);
	
	tdm_record_key_user(MACRO_id, keycode);
	// uprintf("temporal dynamic macro: slot %d length: %d/%d\n", MACRO_id, TDM_CURRENT_LENGTH(MACRO_iterator), TDM_CURRENT_CAPACITY(MACRO_id));
}

void tdm_overwrite_alert(uint16_t keycode) {
	//dprintln("temporal dynamic macro: stopping to avoid overwriting");
	//TODO: flash leds or something
}

static inline bool tdm_is_layer_key(uint16_t keycode);
static inline bool tdm_is_control_key(uint16_t keycode);

void tdm_record_delay_start(void){
	MACRO_delay_next_key_ms = 0;
	while (MACRO_iterator != MACRO_start && 
			(tdm_is_layer_key((MACRO_iterator - MACRO_direction)->keycode))) {
		uprintf("temporal dynamic macro: trimming : iter %d, kc %d, flags %d\n", MACRO_iterator, MACRO_iterator->keycode, MACRO_iterator->flags);
		MACRO_iterator -= MACRO_direction;
	}
}

/**
 * Record a single key in a dynamic macro.
 */
void tdm_record_delay(uint16_t keycode) {
	uprintf("recording delay: %d\n", keycode);
	if (MACRO_delay_next_key_ms > 7200000) { //max delay is 2 hours (ms)
		return;
	}
	int key_val = keycode_to_int(keycode);
	if (key_val == -1) { 
		uprintf("temporal dynamic macro: only numeric keys are valid during delay entry");
		return;
	}
	MACRO_delay_next_key_ms *= 10;
	MACRO_delay_next_key_ms += key_val;
}

void tdm_record_delay_end(void) {
	print_macros();
	//add delay to last pressed key
	tdm_keypress_t* lookback_iterator = MACRO_iterator;
	// while (lookback_iterator != MACRO_start && !is_set(lookback_iterator, FLAG_pressed)) {
	// 	uprintf("temporal dynamic macro: trimming : iter %d, kc %d, flags %d\n", lookback_iterator, lookback_iterator->keycode, lookback_iterator->flags);
	// 	lookback_iterator -= MACRO_direction;
	// }
	uprintf("temporal dynamic macro: ending record delay : iter %d, kc %d, flags %d\n", lookback_iterator, lookback_iterator->keycode, lookback_iterator->flags);
	lookback_iterator->delay_ms = MACRO_delay_next_key_ms;
	MACRO_delay_next_key_ms = 0;
}
/**
 * End recording of the dynamic macro. Essentially just update the
 * pointer to the end of the macro.
 */
void tdm_record_end(void) {
	/* Do not save the keys being held when stopping the recording,
	* i.e. the keys used to access the layer DM_RSTP is on.
	*/
	uprintf("temporal dynamic macro: ending record : iter %d, kc %d, flags %d\n", MACRO_iterator, MACRO_iterator->keycode, MACRO_iterator->flags);
	print_macros();
	while (MACRO_iterator != MACRO_start && 
			(!is_set((MACRO_iterator - MACRO_direction), FLAG_pressed) || 
			 tdm_is_control_key((MACRO_iterator - MACRO_direction)->keycode) || 
			 tdm_is_layer_key((MACRO_iterator - MACRO_direction)->keycode))) 
	{
		uprintf("temporal dynamic macro: trimming : iter %d, kc %d, flags %d\n", (MACRO_iterator - MACRO_direction), (MACRO_iterator - MACRO_direction)->keycode, (MACRO_iterator - MACRO_direction)->flags);
		MACRO_iterator -= MACRO_direction;
	}
	uprintf("temporal dynamic macro: slot %d saved, length: %d\n", MACRO_id, TDM_CURRENT_LENGTH(MACRO_iterator));
	MACRO_ends[MACRO_id] = MACRO_iterator;
	tdm_record_end_user(MACRO_id);
}
bool tdm_state_transition(State next_state);
void tdm_play_start(void);
void tdm_play(void);

typedef uint32_t (*DeferCallback)(uint32_t trigger_time, void* cb_arg);
static uint32_t tdm_delay_callback(uint32_t trigger_time, void* cb_arg);
static uint32_t tdm_loop_callback(uint32_t trigger_time, void* cb_arg);

// we could save this defer_exec slot when not using tap selecto MACRO_id
// if there's a way to interrupt a blocking loop, or if we disable looping
static deferred_token play_token = INVALID_DEFERRED_TOKEN;
static deferred_token delay_token = INVALID_DEFERRED_TOKEN;
static inline void tdm_clear_tokens(void) {
	if (play_token != INVALID_DEFERRED_TOKEN) {
		cancel_deferred_exec(play_token);
		play_token = INVALID_DEFERRED_TOKEN;
	}
	if (delay_token != INVALID_DEFERRED_TOKEN) {
		cancel_deferred_exec(delay_token);
		delay_token = INVALID_DEFERRED_TOKEN;
	}
}

void tdm_play_start(void) {
	clear_keyboard();
	layer_clear();
	uprintf("SAFE_RANGE: %d\n", TURBO);
	tdm_play_user(MACRO_id);
	tdm_reset_iterator();
	uprintf("play start: %d -> %d (Macro_iterator) %d\n", MACRO_start, MACRO_end, MACRO_iterator);
	tdm_play();
	if (play_finished){ //only go to idle if it's not waiting on a delay
		uprintf("not in a delay\n");
		tdm_clear_tokens();
		tdm_state_transition(STATE_idle);
	}
}

void tdm_loop_start(void) {
	tdm_play_user(MACRO_id);
	if (play_token != INVALID_DEFERRED_TOKEN) { //restart if already looping or delayed
		tdm_clear_tokens();
	}
	tdm_reset_iterator();
	uprintf("loop start: %d -> %d (Macro_iterator) %d\n", MACRO_start, MACRO_end, MACRO_iterator);
	play_token = defer_exec(TDM_DEBOUNCE_DELAY, tdm_loop_callback, NULL);
}

static uint32_t tdm_delay_callback(uint32_t trigger_time, void* cb_arg) {
	uprintf("play debounce\n");
	tdm_play();
	if (play_finished) { //only go to idle if it's not waiting on a delay
		uprintf("done with delay\n");
		tdm_state_transition(STATE_idle);
		tdm_clear_tokens();
	}
	return 0;
}

static uint32_t tdm_loop_callback(uint32_t trigger_time, void* cb_arg) {
	uprintf("play loop: t= %d\n", trigger_time);
	tdm_play();
	//since a delay ends tdm_play and schedules another one, looping needs to pause
	// 
	if (play_finished) {
		tdm_reset_iterator(); // start loop at beginning
		return TDM_DEBOUNCE_DELAY;
	} else {
		return 0;
	}
}
void tdm_play_key(tdm_keypress_t* keypress) {
	if(is_set(keypress, FLAG_pressed)) {
		register_code(keypress->keycode);
	} else if( !is_set(keypress, FLAG_pressed)) {
		unregister_code(keypress->keycode);
	}
}
/**
 * Play the dynamic macro.
 */
void tdm_play() {
	uprintf("temporal dynamic macro: playing slot %d \n", MACRO_id);
	uprintf("play start: %d -> %d (Macro_iterator) %d\n", MACRO_start, MACRO_end, MACRO_iterator);
	
	//iterates until the end of the macro or until there's a delay
	while (MACRO_iterator != MACRO_end) {
		uprintf("iter %d KC: %d, down? %d, delay: %d\n", MACRO_iterator, MACRO_iterator->keycode, (MACRO_iterator->flags)&FLAG_pressed, MACRO_iterator->delay_ms);
		tdm_play_key(MACRO_iterator);
		MACRO_iterator += MACRO_direction;
		if(MACRO_iterator->delay_ms) {
			uprintf("delaying: %d\n", MACRO_iterator->delay_ms);
			//continue playing or looping the macro after delaying, but don't block TODO: profiling
			// use defer exec instead of wait so it's possible to cancel play/loop
			DeferCallback tdm_continue = MACRO_current_state == STATE_looping ? tdm_loop_callback : tdm_delay_callback;
			delay_token = defer_exec(MACRO_iterator->delay_ms, tdm_continue, NULL);
			return; //skip clearing the token
		} 	
	}
	uprintf("play finished %d\n", play_finished);
	play_finished = true;
}

// Stops playing (or looping), cancels the callback.
static void tdm_play_stop(void) {
	clear_keyboard();
	layer_clear();
	tdm_clear_tokens();
	tdm_play_stop_user(MACRO_id);
}

static inline bool tdm_is_control_key(uint16_t keycode);
void tdm_invalid_transition(State next_state);
static inline bool tdm_is_valid_key(uint16_t keycode);
static inline bool tdm_is_valid_number(uint16_t keycode);
/* 
 * Handle the key events related to the dynamic macros. 
 * Determines if TDM should record, transition states, or pass the key press.
 * Should be called from process_record_user() like this:
 *
 *   bool process_record_user(uint16_t keycode, keyrecord_t *record) {
 *	   if (!process_record_dynamic_macro(keycode, record)) {
 *		   return false;
 *	   }
 *	   <...THE REST OF THE FUNCTION...>
 *   }
 */
bool process_temporal_dynamic_macro(uint16_t keycode, keyrecord_t* record) {
	// const char* str = state_to_string(MACRO_current_state);
	// uprintf("current_state: %s\n", str);
	if (tdm_is_control_key(keycode)) {
		if(!record->event.pressed) { //is a control key in idle state
			State next_state = keycode_to_state(keycode);
			tdm_state_transition(next_state);
		} 
	} else if (tdm_is_layer_key(keycode)) {
		return true; // don't handle layer keys, only the resulting keycode
	} else {
		switch (MACRO_current_state) {
			case STATE_idle:
				 return true;
				break;
			case STATE_recording:
				if(tdm_is_valid_key(keycode)) {
					tdm_record_key(keycode, record);
				} else if(record->event.pressed){
					tdm_state_transition(STATE_idle);
					return !TDM_SILENT_INVALID_KEYS; // user decides if invalid keys continue processing
				}
				break;
			case STATE_recording_delay:
				if (tdm_is_valid_number(keycode) && record->event.pressed) {
					tdm_record_delay(keycode);
				} else if (!record->event.pressed && !tdm_is_valid_number(keycode)) {
					tdm_state_transition(STATE_recording);
					return !TDM_SILENT_RECORDED_KEYS; //make sure the keyup is processed if keydown was in record_delay
				}
				break;
			case STATE_selecting: 
				if(!record->event.pressed)
					return !TDM_SILENT_RECORDED_KEYS;
				if(tdm_is_valid_number(keycode)) {
					tdm_select_macro(keycode);
				} else {
					tdm_state_transition(STATE_idle);
					return !TDM_SILENT_INVALID_KEYS;
				}
				break;
// #if TDM_EXIT_STATE_ON_ANY_KEY
// 			case STATE_playing:
// 			case STATE_looping: 
// 				if(!record->event.pressed) {
// 					tdm_state_transition(STATE_idle);
// 					return !TDM_SILENT_INVALID_KEYS;
// 				}
// 				break;
// #endif
			default:
				return true;
		}
	}
	return !TDM_SILENT_RECORDED_KEYS; // user decides if recorded keys continue processing
}

static inline bool tdm_is_valid_key(uint16_t keycode) {
	return !tdm_is_control_key(keycode) && tdm_is_valid_key_user(keycode);
}
static inline bool tdm_is_valid_number(uint16_t keycode) {
	return (keycode >= KC_1 && keycode <= KC_9) || keycode == KC_0
	     ||(keycode >= KC_P1 && keycode <= KC_P9) || keycode == KC_P0;
}
static inline bool tdm_is_layer_key(uint16_t keycode) {
	// uprintf("%d < kc < %d\n", QK_MOMENTARY, QK_MOMENTARY_MAX);
	return(keycode > QK_MOMENTARY && keycode < QK_MOMENTARY_MAX) ||
			(keycode > QK_TO && keycode < QK_TO_MAX) ||
			(keycode > QK_TOGGLE_LAYER && keycode < QK_TOGGLE_LAYER_MAX) ||
			(keycode > QK_LAYER_TAP_TOGGLE && keycode < QK_LAYER_TAP_TOGGLE_MAX) ||
			(keycode > QK_ONE_SHOT_LAYER && keycode < QK_ONE_SHOT_LAYER_MAX) ||
			(keycode > QK_TRI_LAYER_LOWER && keycode < QK_TRI_LAYER_UPPER) ||
			(keycode == QK_ONE_SHOT_MOD);
}

static inline bool tdm_is_control_key(uint16_t keycode) {
	return (keycode == TDM_SELECT ||
	        keycode == TDM_RECORD ||
	        keycode == TDM_DELAY  ||
	        keycode == TDM_END    ||
	        keycode == TDM_PLAY   ||
	        keycode == TDM_LOOP)  ;
}

//TODO: compare compilation and performance of using a lookup table to function pointer for state transitions, with a switch on current state and if for each next state
/* State Machine
* controls all persistent state, other than buffers & macro pointer
* tracks what state the system is in to validate the control keys (record, play, etc)
*/
typedef void (*TransitionFunction)(void);
// Define the transition matrix
TransitionFunction transition_matrix[STATE_idle+1][STATE_idle+1];

void tdm_init_state_machine(void){
	transition_matrix[STATE_idle][STATE_recording] = tdm_record_start;
	transition_matrix[STATE_recording][STATE_recording_delay] = tdm_record_delay_start;
	transition_matrix[STATE_recording_delay][STATE_recording] = tdm_record_delay_end;
	transition_matrix[STATE_recording][STATE_idle] = tdm_record_end;
	transition_matrix[STATE_idle][STATE_playing] = tdm_play_start;
	transition_matrix[STATE_playing][STATE_idle] = tdm_play_stop;
	transition_matrix[STATE_idle][STATE_looping] = tdm_loop_start;
	transition_matrix[STATE_looping][STATE_looping] = tdm_loop_start;
	transition_matrix[STATE_looping][STATE_idle] = tdm_play_stop;
	transition_matrix[STATE_idle][STATE_selecting] = tdm_select_start;
	transition_matrix[STATE_selecting][STATE_idle] = tdm_select_end;
}

bool tdm_state_transition(State next_state) {
	bool valid_transition = true;
	TransitionFunction transition = transition_matrix[MACRO_current_state][next_state];
	if (transition == NULL) {
		tdm_invalid_transition(next_state);
		valid_transition = false;
	} else {
		uprintf("transitioning to state: %d\n", next_state);
		uprintf("MacroEnds: %d[", MACRO_buffers[MACRO_id]);
		for (int i = 0; i < TDM_NUM_MACROS; i++) {
			uprintf("%d, ", MACRO_ends[i]);
		}
		uprintf("]\n");
		MACRO_current_state = next_state;
		transition();
	}
	return valid_transition;
}

void tdm_invalid_transition(State next_state){
	uprintf("temporal dynamic macro: invalid transition: %d to %d\n", MACRO_current_state, next_state);
}

void print_macros(void) {
	// if(MACRO_iterator MACRO_ends[MACRO_id] == TDM_CURRENT_START(MACRO_id)) //update macro end if we're currently recording
		MACRO_ends[MACRO_id] = MACRO_iterator; //since macro ends normally doesn't update until record end
	uprintf("\n==========\n");
	uprintf("MACRO_ends[%d] = [", TDM_NUM_MACROS);
	for( int e; e < TDM_NUM_MACROS; e++) {
		uprintf("%d,", MACRO_ends[e] - TDM_BUFFER_START(e));
	}
	uprintf("]\n");
	int buffer_length = TDM_BUFFER_SIZE;
	uprintf("%d\n", buffer_length);
	for (int i = 0; i < TDM_NUM_MACROS; i++) {
		uprintf("\nMacro# %d\n", i);
		for (tdm_keypress_t* iter = TDM_CURRENT_START(i); iter != MACRO_ends[i]; iter += DIRECTION(i)) {
			uprintf("KC: %d, down? %d, delay: %d\n", iter->keycode, (iter->flags)&FLAG_pressed, iter->delay_ms);
		}
	}
	uprintf("==========\n");
	// for (int i = 0; i < (TDM_NUM_MACROS+1)/2; i++) {
	// 	tdm_keypress_t* buffer_start = MACRO_buffers[i];
	// 	uprintf("buffer #%d %d\n", i, &MACRO_buffers[i]);
	// 	// Print recorded key information
	// 	for (int j = 0; j < TDM_BUFFER_SIZE; j++) {
	// 		uprintf("%05d,", buffer_start[j].keycode);
	// 	}
	// 	uprintf("\n");
	// 	// print the pointer indicators
	// 	for (int k = 0; k < TDM_BUFFER_SIZE; k++) {
	// 		// Print buffer's left macro end
	// 		if (&buffer_start[k] == MACRO_ends[i*2]) {
	// 			uprintf("^%dend ", i*2);
	// 		}// Print buffer's right macro end
	// 		else if (&buffer_start[k] == MACRO_ends[(i*2)+1]) {
	// 			uprintf(" end%d^", (i*2)+1);  // Pointer for current macro's start
	// 		}
	// 		else if (&buffer_start[k] == MACRO_iterator) {  // Current iterator's position
	// 			uprintf("^i[%d] ", k);
	// 		} else {
	// 			uprintf("     ,");
	// 		}
	// 	}

	// 	// Print "macro X last recorded key"
	// 	uprintf("\n");
	// }
}
