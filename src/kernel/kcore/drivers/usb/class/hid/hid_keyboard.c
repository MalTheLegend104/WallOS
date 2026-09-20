#include <drivers/usb/class/hid/hid_keyboard.h>

#include <drivers/serial.h>
#include <drivers/usb/usb_core.h>
#include <memory/kernel_alloc.h>
#include <system/timer.h>

#include <stdbool.h>
#include <string.h>

// Boot protocol report is a fixed 8 bytes (USB HID spec Appendix B):
// byte 0: modifier bitmap
// byte 1: reserved
// bytes 2-7: up to 6 keycodes
#define HID_KEYBOARD_REPORT_SIZE 8

// Polling interval for the timer callback
#define HID_KEYBOARD_POLL_INTERVAL_US    (20 * 1000) // 50 Hz
#define HID_KEYBOARD_TRANSFER_TIMEOUT_MS 50

typedef struct hid_keyboard_state {
	usb_interface_t* iface;
	usb_endpoint_t* ep_in;

	uint8_t report[HID_KEYBOARD_REPORT_SIZE];
	uint8_t last_report[HID_KEYBOARD_REPORT_SIZE];

	// Set by the timer ISR, cleared by hid_keyboard_poll_all() once serviced
	// We don't want the ISR to touch execute_transfer() since it can block for a significant amount of time
	volatile bool poll_pending;
	timer_callback_t poll_cb;

	struct hid_keyboard_state* next;
} hid_keyboard_state_t;

static hid_keyboard_state_t* g_keyboards = NULL;

static bool hid_keyboard_report_has_key(const uint8_t report[HID_KEYBOARD_REPORT_SIZE], uint8_t code) {
	for (int i = 2; i < HID_KEYBOARD_REPORT_SIZE; i++) {
		if (report[i] == code) return true;
	}
	return false;
}

#include <input/input_handler.h>

/**
 * Maps standard USB HID modifier bits to WallOS modifier flags.
 * HID Modifiers: bit 0=LCtrl, 1=LShift, 2=LAlt, 3=LGui, 4=RCtrl, 5=RShift, 6=RAlt, 7=RGui
 *
 * Note: on boot-protocol keyboards these bits are the ONLY way modifier
 * state is reported - LCtrl/RCtrl/etc never show up as usage codes in
 * bytes 2-7 of the report, so there's no separate keycode-based path for
 * them (see map_hid_keycode_to_wallos, which deliberately doesn't map
 * 0xE0-0xE7).
 */
static uint32_t map_hid_modifiers(uint8_t hid_mods) {
	uint32_t wallos_mods = WALLOS_MOD_NONE; //

	if (hid_mods & ((1 << 0) | (1 << 4))) {
		wallos_mods |= WALLOS_MOD_CTRL; //
	}
	if (hid_mods & ((1 << 1) | (1 << 5))) {
		wallos_mods |= WALLOS_MOD_SHIFT; //
	}
	if (hid_mods & ((1 << 2) | (1 << 6))) {
		wallos_mods |= WALLOS_MOD_ALT; //
	}
	if (hid_mods & ((1 << 3) | (1 << 7))) {
		wallos_mods |= WALLOS_MOD_META;
	}

	return wallos_mods;
}

/**
 * Maps standard USB HID Keyboard/Keypad usage page (0x07) keycodes to WallOS keys.
 *
 * Covers the standard 104/105-key (100%) layout plus a few other "easy" keys (like f13-24)
 * I didn't feel like bothering with finding out all the obscure ones, the 100% is covered very nicely online.
 * https://github.com/tmk/tmk_keyboard/wiki/USB:-HID-Usage-Table
 *
 * We dont handle 0xE0-0xE7 (LCtrl/LShift/LAlt/LGui/RCtrl/RShift/RAlt/RGui) since boot protocol reports these only via the modifier byte
 *
 * 0x65 is "Keyboard Application". This maps to WALLOS_KEY_MENU since I think that's the windows context menu key on some keyboards.
 * I did little research in this regard, I could be completely wrong.
 *
 * We dont map:
 *     - 0x32 Keyboard Non-US "#" and "~" (different from the ISO_102 key at 0x64, didn't feel like adding to the input enum)
 *     - 0x66 Keyboard Power, not a physical key anyway
 *     - International/language keys (Kana, Kanji, etc)
 */
static wallos_key_t map_hid_keycode_to_wallos(uint8_t keycode) {
	// A-Z (HID: 0x04 to 0x1D)
	if (keycode >= 0x04 && keycode <= 0x1D) {
		return (wallos_key_t) (WALLOS_KEY_A + (keycode - 0x04));
	}

	// Numbers 1-9 and 0 (HID 0x1E to 0x27)
	if (keycode >= 0x1E && keycode <= 0x26) {
		return (wallos_key_t) (WALLOS_KEY_NUM1 + (keycode - 0x1E));
	}
	if (keycode == 0x27) return WALLOS_KEY_NUM0;

	// F1-F12 (HID 0x3A to 0x45)
	if (keycode >= 0x3A && keycode <= 0x45) {
		return (wallos_key_t) (WALLOS_KEY_F1 + (keycode - 0x3A));
	}

	// F13-F24 (HID 0x68 to 0x73)
	if (keycode >= 0x68 && keycode <= 0x73) {
		return (wallos_key_t) (WALLOS_KEY_F13 + (keycode - 0x68));
	}

	// Keypad 1-9 (HID 0x59 to 0x61)
	if (keycode >= 0x59 && keycode <= 0x61) {
		return (wallos_key_t) (WALLOS_KEY_NUMPAD_1 + (keycode - 0x59));
	}

	// Control, punctuation, navigation and keypad keys
	switch (keycode) {
		case 0x28: return WALLOS_KEY_ENTER;
		case 0x29: return WALLOS_KEY_ESCAPE;
		case 0x2A: return WALLOS_KEY_BACKSPACE;
		case 0x2B: return WALLOS_KEY_TAB;
		case 0x2C: return WALLOS_KEY_SPACE;
		case 0x2D: return WALLOS_KEY_MINUS;
		case 0x2E: return WALLOS_KEY_EQUALS;
		case 0x2F: return WALLOS_KEY_LEFTBRACKET;
		case 0x30: return WALLOS_KEY_RIGHTBRACKET;
		case 0x31: return WALLOS_KEY_BACKSLASH;
		case 0x33: return WALLOS_KEY_SEMICOLON;
		case 0x34: return WALLOS_KEY_APOSTROPHE;
		case 0x35: return WALLOS_KEY_TILDE;
		case 0x36: return WALLOS_KEY_COMMA;
		case 0x37: return WALLOS_KEY_PERIOD;
		case 0x38: return WALLOS_KEY_SLASH;
		case 0x39: return WALLOS_KEY_CAPSLOCK;
		case 0x46: return WALLOS_KEY_PRINTSCREEN;
		case 0x47: return WALLOS_KEY_SCROLLLOCK;
		case 0x48: return WALLOS_KEY_PAUSE;
		case 0x49: return WALLOS_KEY_INSERT;
		case 0x4A: return WALLOS_KEY_HOME;
		case 0x4B: return WALLOS_KEY_PAGEUP;
		case 0x4C: return WALLOS_KEY_DELETE;
		case 0x4D: return WALLOS_KEY_END;
		case 0x4E: return WALLOS_KEY_PAGEDOWN;
		case 0x4F: return WALLOS_KEY_RIGHT;
		case 0x50: return WALLOS_KEY_LEFT;
		case 0x51: return WALLOS_KEY_DOWN;
		case 0x52: return WALLOS_KEY_UP;
		case 0x53: return WALLOS_KEY_NUMLOCK;
		case 0x54: return WALLOS_KEY_NUMPAD_DIVIDE;
		case 0x55: return WALLOS_KEY_NUMPAD_MULTIPLY;
		case 0x56: return WALLOS_KEY_NUMPAD_MINUS;
		case 0x57: return WALLOS_KEY_NUMPAD_PLUS;
		case 0x58: return WALLOS_KEY_NUMPAD_ENTER;
		case 0x62: return WALLOS_KEY_NUMPAD_0;
		case 0x63: return WALLOS_KEY_NUMPAD_DECIMAL;
		case 0x64: return WALLOS_KEY_ISO_102;
		case 0x65: return WALLOS_KEY_MENU;
		case 0x67: return WALLOS_KEY_NUMPAD_EQUALS;

		// Locking variants
		// We don't distinguish these internally anyway
		case 0x82: return WALLOS_KEY_CAPSLOCK;
		case 0x83: return WALLOS_KEY_NUMLOCK;
		case 0x84: return WALLOS_KEY_SCROLLLOCK;

		// Boot-protocol volume keys
		// Keyboards can *technically* send these in the usage page rather the consumer controls
		case 0x7F: return WALLOS_KEY_VOLUMEMUTE;
		case 0x80: return WALLOS_KEY_VOLUMEUP;
		case 0x81: return WALLOS_KEY_VOLUMEDOWN;

		default: return WALLOS_KEY_COULDNT_MAP;
	}
}

static void hid_keyboard_key_event(hid_keyboard_state_t* kb, uint8_t keycode, uint8_t modifiers, bool pressed) {
	(void) kb;
	wallos_input_event_t event;

	event.timestamp_ms = timer_uptime_ms();
	event.device_id = 2; // TODO: we need actual generated internal device ids. these are supposed to be so we can filter input events for certain devices
	event.type = WALLOS_INPUT_DEVICE_KEYBOARD;

	event.data.keyboard.key = map_hid_keycode_to_wallos(keycode);
	event.data.keyboard.state = pressed ? WALLOS_INPUT_STATE_PRESSED : WALLOS_INPUT_STATE_RELEASED;
	event.data.keyboard.modifiers = map_hid_modifiers(modifiers);

	if (event.data.keyboard.key != WALLOS_KEY_INVALID) {
		input_push_event(&event);
	}

	// printf_serial("[HID][KBD] usage=0x%02x mod=0x%02x %s\r\n", keycode, modifiers, pressed ? "down" : "up");
}

static void hid_keyboard_process_report(hid_keyboard_state_t* kb) {
	// Releases: keys present in last_report but not in the new report.
	for (int i = 2; i < HID_KEYBOARD_REPORT_SIZE; i++) {
		uint8_t code = kb->last_report[i];
		if (code < 4) continue; // 0 = no key, 1 = rollover error, 2-3 reserved
		if (!hid_keyboard_report_has_key(kb->report, code)) {
			hid_keyboard_key_event(kb, code, kb->last_report[0], false);
		}
	}
	// Presses: keys present in the new report but not in last_report.
	for (int i = 2; i < HID_KEYBOARD_REPORT_SIZE; i++) {
		uint8_t code = kb->report[i];
		if (code < 4) continue;
		if (!hid_keyboard_report_has_key(kb->last_report, code)) {
			hid_keyboard_key_event(kb, code, kb->report[0], true);
		}
	}
}

static void hid_keyboard_do_poll(hid_keyboard_state_t* kb) {
	usb_transfer_t xfer;
	memset(&xfer, 0, sizeof(xfer));
	xfer.device = kb->iface->usb_dev;
	xfer.endpoint = kb->ep_in;
	xfer.buffer = kb->report;
	xfer.length = sizeof(kb->report);
	xfer.timeout_ms = HID_KEYBOARD_TRANSFER_TIMEOUT_MS;

	int ret = usb_transfer_sync(&xfer);
	if (ret != 0 || xfer.status != USB_TRANSFER_COMPLETED) {
		// NAK means the device has no new data and is normal for interrupt IN polling
		// How execute_transfer() reports NAK is currebntly HCD-specific
		// I need to make this have a standard interface in usb_transfer_sync()
		// Until then treat anything other than COMPLETED as "no new report."
		return;
	}

	hid_keyboard_process_report(kb);
	memcpy(kb->last_report, kb->report, sizeof(kb->report));
}

void hid_keyboard_poll_all(void) {
	for (hid_keyboard_state_t* kb = g_keyboards; kb; kb = kb->next) {
		if (!kb->poll_pending) continue;
		kb->poll_pending = false;
		hid_keyboard_do_poll(kb);
	}
}

// Runs in an ISR, so we don't want to touch anything in the USB stack directly
// Just set a flag and make our polling loop deal with it
static void hid_keyboard_timer_tick(timer_callback_t* self, void* ctx) {
	(void) self;
	hid_keyboard_state_t* kb = (hid_keyboard_state_t*) ctx;
	kb->poll_pending = true;
}

int hid_keyboard_attach(usb_interface_t* iface) {
	usb_endpoint_t* ep_in = usb_find_endpoint(iface, USB_ENDPOINT_TYPE_INTERRUPT, USB_DIR_IN);
	if (!ep_in) {
		printf_serial("[HID][KBD] no interrupt IN endpoint on interface %u.\r\n", iface->interface_number);
		return -1;
	}

	// Force boot protocol on boot-subclass interfaces
	if (iface->interface_subclass == HID_SUBCLASS_BOOT) {
		hid_set_protocol(iface, HID_PROTOCOL_BOOT);
	}
	hid_set_idle(iface, 0); // report only on change

	hid_keyboard_state_t* kb = (hid_keyboard_state_t*) kcalloc(1, sizeof(hid_keyboard_state_t));
	if (!kb) return -1;

	kb->iface = iface;
	kb->ep_in = ep_in;
	kb->poll_cb.callback_fn = hid_keyboard_timer_tick;
	kb->poll_cb.ctx = kb;

	timer_register_callback(&kb->poll_cb, HID_KEYBOARD_POLL_INTERVAL_US, true);

	kb->next = g_keyboards;
	g_keyboards = kb;

	printf_serial("[HID][KBD] attached on interface %u, ep=0x%02x\r\n", iface->interface_number, ep_in->address);
	return 0;
}