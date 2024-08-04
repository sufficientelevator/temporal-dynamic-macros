// Copyright 2024 Jack Bellinger
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file temporal_dynamic_macro.h
 * @brief Replay keypresses with delays - record a dynamic macro and add specified delays.
 *
 * Overview
 * --------
 *
 * This library implements a  button that records a new macro,
 * with the option to add specific ms delays between keys.
 *
 * @note Mouse keys and deferred execution must be enabled; in rules.mk set
 * `MOUSEKEY_ENABLE = yes` and `DEFERRED_EXEC_ENABLE = yes`.
 *
 * For full documentation, see
 * <https://getreuer.info/posts/keyboards/mouse-turbo-click>
 */

#pragma once

#include "quantum.h"

#ifdef __cplusplus
extern "C" {
#endif


/* May be overridden with a custom value. Be aware that the effective
 * macro length is half of this value: each keypress is recorded twice
 * because of the down-event and up-event. This is not a bug, it's the
 * intended behavior.
 *
 * Usually it should be fine to set the macro size to at least 256 but
 * there have been reports of it being too much in some users' cases,
 * so 128 is considered a safe default.
 */
#ifndef TDM_BUFFER_SIZE
#	define TDM_BUFFER_SIZE 50
#endif

//how many macros can be recorded. 2 macros in each buffer. I suggest this to be even
#define TDM_NUM_MACROS 2

//if recorded keys output characters to OS.
#define TDM_SILENT_RECORDED_KEYS false

// if invalid keys pressed during recording output characters to OS
#define TDM_SILENT_INVALID_KEYS true

#define TDM_EXIT_STATE_ON_ANY_KEY true

//TODO add 

// milliseconds btw last tap and play/record start, tap in this time to select next macro
// this can be 0 if you don't use tap select macro_id

#define TDM_DEBOUNCE_DELAY 100

/**
 * Handler function for Temporal Dynamic Macro.
 *
 * define a custom keycode to use for TDM. 
 * typedef enum {
 * 	eg = SAFE_RANGE,
 * 	TDM,
 * 	... the rest of your keycodes
 * } custom_keycodes;
 *
 * Then handle tdm from your `process_record_user` function by calling
 * `process_temporal_dynamic_macro`, passing your custom keycode for
 * the `turbo_click_keycode` arg:
 *
 * bool process_record_user(uint16_t keycode, keyrecord_t* record) {
 * 	if (!process_temporal_dynamic_macro(keycode, record, TDM)) {
 * 		return false;
 * 	}
 * 	// Your macros ...
 * 	return true;
 * }
 */

void tdm_init(void);
void tdm_init_user(void);
bool process_temporal_dynamic_macro(uint16_t keycode, keyrecord_t* record);
bool select_macro_id(uint16_t new_macro_id);

void tdm_led_blink(void);
void tdm_rgb_user(void);
void tdm_record_start_user(uint8_t macro_id);
void tdm_play_user(uint8_t macro_id);
void tdm_record_key_user(uint8_t macro_id, uint16_t keycode);
void tdm_record_end_user(uint8_t macro_id);
void tdm_stop_recording(void);

#ifdef __cplusplus
}
#endif
