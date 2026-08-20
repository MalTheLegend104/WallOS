#ifndef WALLOS_INPUT_HANDLER_H
#define WALLOS_INPUT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO: The rest of this should probably dynamically generate input device IDs.
// Im too lazy for this now, and there will only ever be 1 PS/2 and 1 serial input device, so these can be static
#define PS2_KEYBOARD_DEVICE_ID 0
#define SERIAL_KEYBOARD_DEVICE_ID 1


// we are going to try to make interfaces for most generic interface devices
// - mouse
// - touch (likely a part of mouse)
// - keyboard (including serial)
// - controllers
// There are some other weird HID devices, but those should all likely have vendor_id:device_id binding anyway.

// Device types to identify the source of the event
	typedef enum {
		WALLOS_INPUT_DEVICE_KEYBOARD = 0,
		WALLOS_INPUT_DEVICE_MOUSE,
		WALLOS_INPUT_DEVICE_TOUCH,
		WALLOS_INPUT_DEVICE_CONTROLLER,

		WALLOS_INPUT_DEVICE_MAX = WALLOS_INPUT_DEVICE_CONTROLLER
	} wallos_input_device_type_t;

	typedef enum {
		WALLOS_KEY_A,
		WALLOS_KEY_B,
		WALLOS_KEY_C,
		WALLOS_KEY_D,
		WALLOS_KEY_E,
		WALLOS_KEY_F,
		WALLOS_KEY_G,
		WALLOS_KEY_H,
		WALLOS_KEY_I,
		WALLOS_KEY_J,
		WALLOS_KEY_K,
		WALLOS_KEY_L,
		WALLOS_KEY_M,
		WALLOS_KEY_N,
		WALLOS_KEY_O,
		WALLOS_KEY_P,
		WALLOS_KEY_Q,
		WALLOS_KEY_R,
		WALLOS_KEY_S,
		WALLOS_KEY_T,
		WALLOS_KEY_U,
		WALLOS_KEY_V,
		WALLOS_KEY_W,
		WALLOS_KEY_X,
		WALLOS_KEY_Y,
		WALLOS_KEY_Z,
		WALLOS_KEY_NUM0,
		WALLOS_KEY_NUM1,
		WALLOS_KEY_NUM2,
		WALLOS_KEY_NUM3,
		WALLOS_KEY_NUM4,
		WALLOS_KEY_NUM5,
		WALLOS_KEY_NUM6,
		WALLOS_KEY_NUM7,
		WALLOS_KEY_NUM8,
		WALLOS_KEY_NUM9,
		WALLOS_KEY_NUMPAD_0,
		WALLOS_KEY_NUMPAD_1,
		WALLOS_KEY_NUMPAD_2,
		WALLOS_KEY_NUMPAD_3,
		WALLOS_KEY_NUMPAD_4,
		WALLOS_KEY_NUMPAD_5,
		WALLOS_KEY_NUMPAD_6,
		WALLOS_KEY_NUMPAD_7,
		WALLOS_KEY_NUMPAD_8,
		WALLOS_KEY_NUMPAD_9,
		WALLOS_KEY_NUMPAD_DIVIDE,
		WALLOS_KEY_NUMPAD_MULTIPLY,
		WALLOS_KEY_NUMPAD_MINUS,
		WALLOS_KEY_NUMPAD_PLUS,
		WALLOS_KEY_NUMPAD_DECIMAL,
		WALLOS_KEY_NUMPAD_EQUALS,
		WALLOS_KEY_NUMPAD_ENTER,
		WALLOS_KEY_TILDE,
		WALLOS_KEY_SLASH,
		WALLOS_KEY_PERIOD,
		WALLOS_KEY_COMMA,
		WALLOS_KEY_APOSTROPHE,
		WALLOS_KEY_SEMICOLON,
		WALLOS_KEY_RIGHTBRACKET,
		WALLOS_KEY_LEFTBRACKET,
		WALLOS_KEY_BACKSLASH,
		WALLOS_KEY_ISO_102, // Non-US keyboards tend to have an additional key next to left shift, ISO key 102
		WALLOS_KEY_EQUALS,
		WALLOS_KEY_MINUS,
		WALLOS_KEY_F1,
		WALLOS_KEY_F2,
		WALLOS_KEY_F3,
		WALLOS_KEY_F4,
		WALLOS_KEY_F5,
		WALLOS_KEY_F6,
		WALLOS_KEY_F7,
		WALLOS_KEY_F8,
		WALLOS_KEY_F9,
		WALLOS_KEY_F10,
		WALLOS_KEY_F11,
		WALLOS_KEY_F12,
		WALLOS_KEY_F13,
		WALLOS_KEY_F14,
		WALLOS_KEY_F15,
		WALLOS_KEY_F16,
		WALLOS_KEY_F17,
		WALLOS_KEY_F18,
		WALLOS_KEY_F19,
		WALLOS_KEY_F20,
		WALLOS_KEY_F21,
		WALLOS_KEY_F22,
		WALLOS_KEY_F23,
		WALLOS_KEY_F24,
		WALLOS_KEY_ESCAPE,
		WALLOS_KEY_BACKSPACE,
		WALLOS_KEY_TAB,
		WALLOS_KEY_CAPSLOCK,
		WALLOS_KEY_SCROLLLOCK,
		WALLOS_KEY_NUMLOCK,
		WALLOS_KEY_PAUSE, // PS/2 has special handling for this, but the key *does* exist
		WALLOS_KEY_ENTER,
		WALLOS_KEY_LSHIFT,
		WALLOS_KEY_RSHIFT,
		WALLOS_KEY_LCTRL,
		WALLOS_KEY_RCTRL,
		WALLOS_KEY_LALT,
		WALLOS_KEY_RALT,
		WALLOS_KEY_LMETA,
		WALLOS_KEY_RMETA,
		WALLOS_KEY_SPACE,
		WALLOS_KEY_DELETE,
		WALLOS_KEY_INSERT,
		WALLOS_KEY_HOME,
		WALLOS_KEY_END,
		WALLOS_KEY_PAGEUP,
		WALLOS_KEY_PAGEDOWN,
		WALLOS_KEY_PRINTSCREEN,
		WALLOS_KEY_MENU, // "context menu" button on windows
		WALLOS_KEY_UP,
		WALLOS_KEY_DOWN,
		WALLOS_KEY_LEFT,
		WALLOS_KEY_RIGHT,
		WALLOS_KEY_VOLUMEUP,
		WALLOS_KEY_VOLUMEDOWN,
		WALLOS_KEY_VOLUMEMUTE,
		WALLOS_KEY_MEDIANEXTTRACK,
		WALLOS_KEY_MEDIAPREVTRACK,
		WALLOS_KEY_MEDIASTOP,
		WALLOS_KEY_MEDIA_PLAYPAUSE,
		WALLOS_KEY_MEDIASELECT,
		WALLOS_KEY_MAIL,
		WALLOS_KEY_CALCULATOR,
		WALLOS_KEY_MYCOMPUTER,
		WALLOS_KEY_SLEEP,
		WALLOS_KEY_WAKE,
		WALLOS_KEY_POWER,
		WALLOS_KEY_BROWSER_HOME,
		WALLOS_KEY_BROWSER_REFRESH,
		WALLOS_KEY_BROWSER_BACK,
		WALLOS_KEY_BROWSER_FAVORITES,
		WALLOS_KEY_BROWSER_FORWARD,
		WALLOS_KEY_BROWSER_STOP,
		WALLOS_KEY_BROWSER_SEARCH,
		WALLOS_KEY_FN, // Most keyboards themselves will intercept this and do something with it, but some will report it to the OS

		// These are technically physical keys
		// I was going to go into international layouts, but really don't want to deal with it until I need to
		// WALLOS_KEY_JISHO,
		// WALLOS_KEY_KANA,
		// WALLOS_KEY_KANJI,
		// WALLOS_KEY_NONCONVERT,
		// WALLOS_KEY_MODECHANGE,

		WALLOS_KEY_COUNT, // explicitly the last key, can check for valid values using (if < WALLOS_KEY_COUNT)
		WALLOS_KEY_INVALID, // No meaningful key was associated with the event. This is mostly meant for internal driver use, the input handler will discard this if received
		WALLOS_KEY_COULDNT_MAP, // Means that there was a physical button event, but the driver couldn't map it to anything in the list above
		WALLOS_KEY_UNKNOWN, // The driver knows this is a keyboard key, but doesn't know what key it is.

		/* Everything after the WALLOS_KEY_COUNT is meant for diagnostics.
		 * Each has a very specific meaning:
		 * - INVALID is meant for an internal driver problem. If given to the input handler it will be discarded.
		 * - COULDNT_MAP is a problem with the input handler. The driver has identified the key, but it could not be mapped to a WallOS key defined above.
		 * - UNKNOWN is essentially the driver version of COULDNT_MAP. The driver knows that a physical key input occurred, but could not identify or map it within its own input protocol.
		 * Only INVALID is discarded by the input handler. UNKNOWN and COULDNT_MAP are still valid diagnostic events and must be processed appropriately.
		 */
		WALLOS_KEY_MAX_COUNT // count should be used as a barrier, this should be used for sizing
	} wallos_key_t;

	// Key/Button States
	typedef enum {
		WALLOS_INPUT_STATE_RELEASED = 0,
		WALLOS_INPUT_STATE_PRESSED,
		WALLOS_INPUT_STATE_REPEATED
	} wallos_input_state_t;

	// Modifier Flags (Bitmask)
	typedef enum {
		WALLOS_MOD_NONE = 0,
		WALLOS_MOD_SHIFT = (1 << 0),
		WALLOS_MOD_CTRL = (1 << 1),
		WALLOS_MOD_ALT = (1 << 2),
		WALLOS_MOD_CAPS = (1 << 3)
	} wallos_modifier_flags_t;

	// Mouse Buttons
	typedef enum {
		WALLOS_MOUSE_BTN_LEFT = 0,
		WALLOS_MOUSE_BTN_RIGHT,
		WALLOS_MOUSE_BTN_MIDDLE,
		WALLOS_MOUSE_BTN_SIDE1,
		WALLOS_MOUSE_BTN_SIDE2
	} wallos_mouse_button_t;

	typedef struct {
		wallos_key_t key;
		wallos_input_state_t state;
		uint32_t modifiers; // Bitmask of wallos_modifier_flags_t
	} wallos_keyboard_event_t;

	typedef struct {
		int32_t x;
		int32_t y;
		int32_t delta_x;
		int32_t delta_y;
		int32_t scroll_wheel;
		wallos_mouse_button_t button;
		wallos_input_state_t state;
	} wallos_mouse_event_t;

	typedef struct {
		uint32_t touch_id; // For multi-touch tracking
		int32_t x;
		int32_t y;
		float pressure;
		wallos_input_state_t state;
	} wallos_touch_event_t;

	typedef struct {
		uint8_t axis_or_button_id;
		wallos_input_state_t state;
		float value; // 0.0 to 1.0 for analog triggers, -1.0 to 1.0 for joysticks
	} wallos_controller_event_t;

	// Unified Input Event
	typedef struct {
		uint64_t timestamp_ms;
		uint32_t device_id; // Unique ID for hotplugging multiple devices
		wallos_input_device_type_t type;

		union {
			wallos_keyboard_event_t keyboard;
			wallos_mouse_event_t    mouse;
			wallos_touch_event_t    touch;
			wallos_controller_event_t controller;
		} data;
	} wallos_input_event_t;


	// Underlying drivers call this to push events into the OS queue.
	// The input handler will *copy* this event into it's own queue, this does not need to be dynamically allocated, it can be pushed on the stack
	void input_push_event(const wallos_input_event_t* event);
	// Poll the next event from the queue. Returns true if an event was populated.
	bool input_poll_event(wallos_input_device_type_t t, wallos_input_event_t* event);
	// Wait for the next event of a certain type
	// This can be used for blocking input like getc()
	bool input_wait_event(wallos_input_device_type_t t, wallos_input_event_t* event);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_INPUT_HANDLER_H