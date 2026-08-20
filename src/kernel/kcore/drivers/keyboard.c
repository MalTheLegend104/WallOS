#include <drivers/keyboard.h>
#include <input/input_handler.h>
#include <system/idt.h>
#include <stdio.h>
#include <klibc/logger.h>
#include <string.h>

extern __attribute__((interrupt)) void keyboard_handler(struct interrupt_frame* frame);

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// General Keyboard & Scancode stuff
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Keyboard state go brrrrr
static KeyboardState currentState = {};

KeyboardState getKeyboardState() {
    return currentState;
}

void printKeyboardState() {
    printf("Left Shift:  %d\n", currentState.l_shift);
    printf("Left Alt:    %d\n", currentState.l_alt);
    printf("Left Ctrl:   %d\n", currentState.l_ctrl);
    printf("Right Shift: %d\n", currentState.r_shift);
    printf("Right Alt:   %d\n", currentState.r_alt);
    printf("Right Shift: %d\n", currentState.r_shift);
    printf("Right Ctrl:  %d\n", currentState.r_ctrl);
    printf("Caps Lock:   %d\n", currentState.caps);
    printf("Num Lock:    %d\n", currentState.numlock);
    printf("Scroll Lock: %d\n", currentState.scroll_lock);
    printf("Alt Pressed: %d\n", currentState.alt_pressed);
    printf("Shifted:     %d\n", currentState.shifted);
    printf("Control:     %d\n", currentState.ctrl_pressed);
    printf("Last SC:     %d\n", currentState.last_scancode);
}

static bool key_held[WALLOS_KEY_MAX_COUNT] = {};

// Maps a base (non-escaped) scancode to a wallos_key_t.
// sc should already have the break bit (0x80) masked off.
static wallos_key_t base_scancode_to_wallos_key(uint8_t sc) {
    switch (sc) {
        case SC_ESCAPE:       return WALLOS_KEY_ESCAPE;
        case SC_BACKSPACE:    return WALLOS_KEY_BACKSPACE;
        case SC_TAB:          return WALLOS_KEY_TAB;
        case SC_ENTER:        return WALLOS_KEY_ENTER;
        case SC_LEFT_SHIFT:   return WALLOS_KEY_LSHIFT;
        case SC_RIGHT_SHIFT:  return WALLOS_KEY_RSHIFT;
        case SC_CAPS_LOCK:    return WALLOS_KEY_CAPSLOCK;
        case SC_NUM_LOCK:     return WALLOS_KEY_NUMLOCK;
        case SC_SCROLL_LOCK:  return WALLOS_KEY_SCROLLLOCK;
        case SC_LEFT_CONTROL: return WALLOS_KEY_LCTRL;
        case SC_LEFT_ALT:     return WALLOS_KEY_LALT;

        case SC_ALT_SYSRQ: return WALLOS_KEY_PRINTSCREEN; // close enough, Alt+SysRq == PrintScreen physically
        case SC_UNUSED:    return WALLOS_KEY_COULDNT_MAP;

        // Special chars
        case SC_LEFT_BRACKET:  return WALLOS_KEY_LEFTBRACKET;
        case SC_RIGHT_BRACKET: return WALLOS_KEY_RIGHTBRACKET;
        case SC_MINUS:         return WALLOS_KEY_MINUS;
        case SC_EQUAL:         return WALLOS_KEY_EQUALS;
        case SC_SEMICOLON:     return WALLOS_KEY_SEMICOLON;
        case SC_APOSTROPHE:    return WALLOS_KEY_APOSTROPHE;
        case SC_BACKTICK:      return WALLOS_KEY_TILDE;
        case SC_COMMA:         return WALLOS_KEY_COMMA;
        case SC_PERIOD:        return WALLOS_KEY_PERIOD;
        case SC_SLASH:         return WALLOS_KEY_SLASH;
        case SC_BACKSLASH:     return WALLOS_KEY_BACKSLASH;
        case SC_SPACE:         return WALLOS_KEY_SPACE;

        // Alphabet
        case SC_A: return WALLOS_KEY_A; case SC_B: return WALLOS_KEY_B;
        case SC_C: return WALLOS_KEY_C; case SC_D: return WALLOS_KEY_D;
        case SC_E: return WALLOS_KEY_E; case SC_F: return WALLOS_KEY_F;
        case SC_G: return WALLOS_KEY_G; case SC_H: return WALLOS_KEY_H;
        case SC_I: return WALLOS_KEY_I; case SC_J: return WALLOS_KEY_J;
        case SC_K: return WALLOS_KEY_K; case SC_L: return WALLOS_KEY_L;
        case SC_M: return WALLOS_KEY_M; case SC_N: return WALLOS_KEY_N;
        case SC_O: return WALLOS_KEY_O; case SC_P: return WALLOS_KEY_P;
        case SC_Q: return WALLOS_KEY_Q; case SC_R: return WALLOS_KEY_R;
        case SC_S: return WALLOS_KEY_S; case SC_T: return WALLOS_KEY_T;
        case SC_U: return WALLOS_KEY_U; case SC_V: return WALLOS_KEY_V;
        case SC_W: return WALLOS_KEY_W; case SC_X: return WALLOS_KEY_X;
        case SC_Y: return WALLOS_KEY_Y; case SC_Z: return WALLOS_KEY_Z;

        // Function keys
        case SC_F1: return WALLOS_KEY_F1;   case SC_F2: return WALLOS_KEY_F2;
        case SC_F3: return WALLOS_KEY_F3;   case SC_F4: return WALLOS_KEY_F4;
        case SC_F5: return WALLOS_KEY_F5;   case SC_F6: return WALLOS_KEY_F6;
        case SC_F7: return WALLOS_KEY_F7;   case SC_F8: return WALLOS_KEY_F8;
        case SC_F9: return WALLOS_KEY_F9;   case SC_F10: return WALLOS_KEY_F10;
        case SC_F11: return WALLOS_KEY_F11; case SC_F12: return WALLOS_KEY_F12;

        // Number row
        case SC_0: return WALLOS_KEY_NUM0; case SC_1: return WALLOS_KEY_NUM1;
        case SC_2: return WALLOS_KEY_NUM2; case SC_3: return WALLOS_KEY_NUM3;
        case SC_4: return WALLOS_KEY_NUM4; case SC_5: return WALLOS_KEY_NUM5;
        case SC_6: return WALLOS_KEY_NUM6; case SC_7: return WALLOS_KEY_NUM7;
        case SC_8: return WALLOS_KEY_NUM8; case SC_9: return WALLOS_KEY_NUM9;

        // Keypad
        case SC_KEYPAD_0: return WALLOS_KEY_NUMPAD_0; case SC_KEYPAD_1: return WALLOS_KEY_NUMPAD_1;
        case SC_KEYPAD_2: return WALLOS_KEY_NUMPAD_2; case SC_KEYPAD_3: return WALLOS_KEY_NUMPAD_3;
        case SC_KEYPAD_4: return WALLOS_KEY_NUMPAD_4; case SC_KEYPAD_5: return WALLOS_KEY_NUMPAD_5;
        case SC_KEYPAD_6: return WALLOS_KEY_NUMPAD_6; case SC_KEYPAD_7: return WALLOS_KEY_NUMPAD_7;
        case SC_KEYPAD_8: return WALLOS_KEY_NUMPAD_8; case SC_KEYPAD_9: return WALLOS_KEY_NUMPAD_9;
        case SC_KEYPAD_MINUS: return WALLOS_KEY_NUMPAD_MINUS;
        case SC_KEYPAD_ASTERISK: return WALLOS_KEY_NUMPAD_MULTIPLY;
        case SC_KEYPAD_PERIOD: return WALLOS_KEY_NUMPAD_DECIMAL;
        case SC_KEYPAD_PLUS: return WALLOS_KEY_NUMPAD_PLUS;

        default: return WALLOS_KEY_COULDNT_MAP;
    }
}

static wallos_key_t escaped_scancode_to_wallos_key(uint8_t sc) {
    switch (sc) {
        case SC_ENTER:        return WALLOS_KEY_NUMPAD_ENTER;
        case SC_SLASH:        return WALLOS_KEY_NUMPAD_DIVIDE;
        case SC_LEFT_CONTROL: return WALLOS_KEY_RCTRL;
        case SC_LEFT_ALT:     return WALLOS_KEY_RALT;

        // Navigation cluster
        case SC_ESC_HOME:      return WALLOS_KEY_HOME;
        case SC_ESC_UP:        return WALLOS_KEY_UP;
        case SC_ESC_PAGE_UP:   return WALLOS_KEY_PAGEUP;
        case SC_ESC_LEFT:      return WALLOS_KEY_LEFT;
        case SC_ESC_RIGHT:     return WALLOS_KEY_RIGHT;
        case SC_ESC_END:       return WALLOS_KEY_END;
        case SC_ESC_DOWN:      return WALLOS_KEY_DOWN;
        case SC_ESC_PAGE_DOWN: return WALLOS_KEY_PAGEDOWN;
        case SC_ESC_INSERT:    return WALLOS_KEY_INSERT;
        case SC_ESC_DELETE:    return WALLOS_KEY_DELETE;

        // Meta / Apps
        case SC_ESC_LEFT_META:  return WALLOS_KEY_LMETA;
        case SC_ESC_RIGHT_META: return WALLOS_KEY_RMETA;
        case SC_ESC_APPS:       return WALLOS_KEY_MENU;

        // ACPI power keys
        case SC_ESC_POWER: return WALLOS_KEY_POWER;
        case SC_ESC_SLEEP: return WALLOS_KEY_SLEEP;
        case SC_ESC_WAKE:  return WALLOS_KEY_WAKE;

        // Multimedia keys
        case SC_ESC_PREV_TRACK:   return WALLOS_KEY_MEDIAPREVTRACK;
        case SC_ESC_NEXT_TRACK:   return WALLOS_KEY_MEDIANEXTTRACK;
        case SC_ESC_MUTE:         return WALLOS_KEY_VOLUMEMUTE;
        case SC_ESC_VOLUME_DOWN:  return WALLOS_KEY_VOLUMEDOWN;
        case SC_ESC_VOLUME_UP:    return WALLOS_KEY_VOLUMEUP;
        case SC_ESC_PLAY_PAUSE:   return WALLOS_KEY_MEDIA_PLAYPAUSE;
        case SC_ESC_STOP:         return WALLOS_KEY_MEDIASTOP;
        case SC_ESC_MEDIA_SELECT: return WALLOS_KEY_MEDIASELECT;
        case SC_ESC_CALCULATOR:   return WALLOS_KEY_CALCULATOR;
        case SC_ESC_MY_COMPUTER:  return WALLOS_KEY_MYCOMPUTER;
        case SC_ESC_EMAIL:        return WALLOS_KEY_MAIL;

        // WWW/browser keys
        case SC_ESC_WWW_HOME:      return WALLOS_KEY_BROWSER_HOME;
        case SC_ESC_WWW_SEARCH:    return WALLOS_KEY_BROWSER_SEARCH;
        case SC_ESC_WWW_FAVORITES: return WALLOS_KEY_BROWSER_FAVORITES;
        case SC_ESC_WWW_REFRESH:   return WALLOS_KEY_BROWSER_REFRESH;
        case SC_ESC_WWW_STOP:      return WALLOS_KEY_BROWSER_STOP;
        case SC_ESC_WWW_FORWARD:   return WALLOS_KEY_BROWSER_FORWARD;
        case SC_ESC_WWW_BACK:      return WALLOS_KEY_BROWSER_BACK;

        default: return WALLOS_KEY_COULDNT_MAP;
    }
}

static wallos_key_t scancode_to_wallos_key(uint8_t sc, bool escaped) {
    return escaped ? escaped_scancode_to_wallos_key(sc) : base_scancode_to_wallos_key(sc);
}

static uint32_t build_modifier_mask() {
    uint32_t mods = WALLOS_MOD_NONE;
    if (currentState.shifted) 		mods |= WALLOS_MOD_SHIFT;
    if (currentState.ctrl_pressed) mods |= WALLOS_MOD_CTRL;
    if (currentState.alt_pressed) 	mods |= WALLOS_MOD_ALT;
    if (currentState.caps) 		mods |= WALLOS_MOD_CAPS;
    return mods;
}

#include <system/timer.h>

static void push_keyboard_event(wallos_key_t key, bool is_break) {
    if (key == WALLOS_KEY_INVALID) return;

    wallos_input_state_t state;
    if (is_break) {
        state = WALLOS_INPUT_STATE_RELEASED;
        key_held[key] = false;
    } else if (key_held[key]) {
        state = WALLOS_INPUT_STATE_REPEATED;
    } else {
        state = WALLOS_INPUT_STATE_PRESSED;
        key_held[key] = true;
    }

    wallos_input_event_t event = {};
    event.timestamp_ms = timer_uptime_ms();
    event.device_id = PS2_KEYBOARD_DEVICE_ID;
    event.type = WALLOS_INPUT_DEVICE_KEYBOARD;
    event.data.keyboard.key = key;
    event.data.keyboard.state = state;
    event.data.keyboard.modifiers = build_modifier_mask();

    input_push_event(&event);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Init and handler.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void handle_scancode(uint8_t sc) {
    // escaped codes are pain in the ass
    if (sc == SC_ESCAPED_0 || sc == SC_ESCAPED_1) {
        currentState.last_scancode = sc;
        return;
    }

    // This has to be computed BEFORE we touch last_scancode below, since it reflects whether the byte we're processing right now was preceded by an escape prefix.
    bool escaped = (currentState.last_scancode == SC_ESCAPED_0 || currentState.last_scancode == SC_ESCAPED_1);

    bool is_break = (sc & 0x80) != 0;
    uint8_t base_sc = sc & 0x7F;

    switch (sc) {
        case SC_LEFT_SHIFT:         currentState.l_shift = true;            break;
        case SC_LEFT_SHIFT + 0x80:  currentState.l_shift = false;           break; // Key released
        case SC_RIGHT_SHIFT:        currentState.r_shift = true;            break;
        case SC_RIGHT_SHIFT + 0x80: currentState.r_shift = false;           break; // Key released
        case SC_CAPS_LOCK:          currentState.caps = !currentState.caps; break;
        case SC_NUM_LOCK:           currentState.numlock = true;            break;
        case SC_NUM_LOCK + 0x80:    currentState.numlock = false;           break; // Key released
        case SC_SCROLL_LOCK:        currentState.scroll_lock = true;        break;
        case SC_SCROLL_LOCK + 0x80: currentState.scroll_lock = false;       break; // Key released
        // Right alt ruins everything
        case SC_LEFT_ALT: {
                if (!escaped)
                    currentState.l_alt = true;
                else
                    currentState.r_alt = true;
                break;
            }
        case SC_LEFT_ALT + 0x80: {
                if (!escaped)
                    currentState.l_alt = false;
                else
                    currentState.r_alt = false;
                break;
            }
                               // Right control also ruins everything.
        case SC_LEFT_CONTROL: {
                if (!escaped)
                    currentState.l_ctrl = true;
                else
                    currentState.r_ctrl = true;
                break;
            }
        case SC_LEFT_CONTROL + 0x80: {
                if (!escaped)
                    currentState.l_ctrl = false;
                else
                    currentState.r_ctrl = false;
                break;
            }
        default: break;
    }

    // Update the state for Alt, Ctrl, and Shift
    if (currentState.l_alt || currentState.r_alt)       currentState.alt_pressed = true;
    if (!currentState.l_alt && !currentState.r_alt)     currentState.alt_pressed = false;
    if (currentState.l_shift || currentState.r_shift)   currentState.shifted = true;
    if (!currentState.l_shift && !currentState.r_shift) currentState.shifted = false;
    if (currentState.l_ctrl || currentState.r_ctrl)     currentState.ctrl_pressed = true;
    if (!currentState.l_ctrl && !currentState.r_ctrl)   currentState.ctrl_pressed = false;

    currentState.escaped = escaped;

    // Resolve which wallos key this scancode represents and push it
    wallos_key_t key = scancode_to_wallos_key(base_sc, escaped);
    push_keyboard_event(key, is_break);

    currentState.last_scancode = sc;
}

void keyboard_init() {
    // If we need to enable the 8042 we should probably do it here
    // In qemu we dont, so I wont.
    add_interrupt_handler(0x21, keyboard_handler, 0, 0x8E);
    currentState.last_scancode = 0x00;

    // Everything needs to come before this, these start interrupt for the keyboard
    irq_enable(1);
}