# temporal-dynamic-macros
Dynamic macro system for QMK which can delay keypresses

# Features
The Temporal Dynamic Macro (TDM) module provides users with the ability to record and play back sequences of keystrokes, known as macros, on their keyboards.
- **Recording and Playback**: Users can record a sequence of keystrokes and play them back at any time.
- **Delay Insertion**: The ability to insert delays between keystrokes allows for precise timing and synchronization with other actions.
- **Looping**: Macros can be set to loop continuously, useful for tasks that require repetitive execution.
- **Macro Selection**: Support for multiple macros, with the ability to select and play back specific ones.

# Usage

## Step 1: Add the Temporal Dynamic Macros feature code
In the directory containing your keymap.c, create a features subdirectory and copy `temporal_dynamic_macro.h` and `temporal_dynamic_macro.c` there.

## Step 2: Create the custom keycodes
Add the custom keycodes for activating the TDM features and use the new keycode somewhere in your keymap. If you'd like to rename these keys, you'll need to update the names in the source code as well.

```c:custom_keycodes.h
typedef enum {
	TDM_RECORD = SAFE_RANGE,
	TDM_DELAY,
	TDM_END,
	TDM_PLAY,
	TDM_LOOP,
	TDM_SELECT,
	... any other custom keys you want to
} custom_keycodes;
```

I placed this enum in a separate file to include it in various places, but if you're not using any custom keycodes elsewhere, you can append this to the end of `temporal_dynamic_macro.h`

## Step 3: Handle TDM related keystrokes in process_record_user()
This will pass the key presses to the TDM code to check if it should process the key, and do the TDM stuff if so.

```c:keymap.c
#include "temporal_dynamic_macro.h"

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
	if (!process_temporal_dynamic_macro(keycode, record)) { return false; }
	//The rest of the function
}
```

## Step 4: Compile the sources!
In your rules.mk file, add
```c:rules.mk
SRC += features/temporal_dynamic_macro.c

DEFERRED_EXEC_ENABLE = yes
```
