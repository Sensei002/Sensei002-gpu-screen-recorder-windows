#include "../../include/mgl/window/key.h"
#ifndef _WIN32
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#endif

const char* mgl_key_to_string(mgl_key key) {
    switch(key) {
        case MGL_KEY_UNKNOWN:            return "";
        case MGL_KEY_A:                  return "A";
        case MGL_KEY_B:                  return "B";
        case MGL_KEY_C:                  return "C";
        case MGL_KEY_D:                  return "D";
        case MGL_KEY_E:                  return "E";
        case MGL_KEY_F:                  return "F";
        case MGL_KEY_G:                  return "G";
        case MGL_KEY_H:                  return "H";
        case MGL_KEY_I:                  return "I";
        case MGL_KEY_J:                  return "J";
        case MGL_KEY_K:                  return "K";
        case MGL_KEY_L:                  return "L";
        case MGL_KEY_M:                  return "M";
        case MGL_KEY_N:                  return "N";
        case MGL_KEY_O:                  return "O";
        case MGL_KEY_P:                  return "P";
        case MGL_KEY_Q:                  return "Q";
        case MGL_KEY_R:                  return "R";
        case MGL_KEY_S:                  return "S";
        case MGL_KEY_T:                  return "T";
        case MGL_KEY_U:                  return "U";
        case MGL_KEY_V:                  return "V";
        case MGL_KEY_W:                  return "W";
        case MGL_KEY_X:                  return "X";
        case MGL_KEY_Y:                  return "Y";
        case MGL_KEY_Z:                  return "Z";
        case MGL_KEY_NUM0:               return "0";
        case MGL_KEY_NUM1:               return "1";
        case MGL_KEY_NUM2:               return "2";
        case MGL_KEY_NUM3:               return "3";
        case MGL_KEY_NUM4:               return "4";
        case MGL_KEY_NUM5:               return "5";
        case MGL_KEY_NUM6:               return "6";
        case MGL_KEY_NUM7:               return "7";
        case MGL_KEY_NUM8:               return "8";
        case MGL_KEY_NUM9:               return "9";
        case MGL_KEY_ESCAPE:             return "Escape";
        case MGL_KEY_LCONTROL:           return "Left Ctrl";
        case MGL_KEY_LSHIFT:             return "Left Shift";
        case MGL_KEY_LALT:               return "Left Alt";
        case MGL_KEY_LSYSTEM:            return "Left System";
        case MGL_KEY_RCONTROL:           return "Right Ctrl";
        case MGL_KEY_RSHIFT:             return "Right Shift";
        case MGL_KEY_RALT:               return "Right Alt";
        case MGL_KEY_RSYSTEM:            return "Right System";
        case MGL_KEY_MENU:               return "Menu";
        case MGL_KEY_LBRACKET:           return "[";
        case MGL_KEY_RBRACKET:           return "]";
        case MGL_KEY_SEMICOLON:          return ";";
        case MGL_KEY_COMMA:              return ",";
        case MGL_KEY_PERIOD:             return ".";
        case MGL_KEY_QUOTE:              return "'";
        case MGL_KEY_SLASH:              return "/";
        case MGL_KEY_BACKSLASH:          return "\\";
        case MGL_KEY_TILDE:              return "~";
        case MGL_KEY_EQUAL:              return "=";
        case MGL_KEY_HYPHEN:             return "-";
        case MGL_KEY_SPACE:              return "Space";
        case MGL_KEY_ENTER:              return "Enter";
        case MGL_KEY_BACKSPACE:          return "Backspace";
        case MGL_KEY_TAB:                return "Tab";
        case MGL_KEY_PAGEUP:             return "PageUp";
        case MGL_KEY_PAGEDOWN:           return "PageDown";
        case MGL_KEY_END:                return "End";
        case MGL_KEY_HOME:               return "Home";
        case MGL_KEY_INSERT:             return "Insert";
        case MGL_KEY_DELETE:             return "Delete";
        case MGL_KEY_ADD:                return "Add";
        case MGL_KEY_SUBTRACT:           return "Subtract";
        case MGL_KEY_MULTIPLY:           return "Multiply";
        case MGL_KEY_DIVIDE:             return "Divide";
        case MGL_KEY_LEFT:               return "Left";
        case MGL_KEY_RIGHT:              return "Right";
        case MGL_KEY_UP:                 return "Up";
        case MGL_KEY_DOWN:               return "Down";
        case MGL_KEY_NUMPAD0:            return "Numpad0";
        case MGL_KEY_NUMPAD1:            return "Numpad1";
        case MGL_KEY_NUMPAD2:            return "Numpad2";
        case MGL_KEY_NUMPAD3:            return "Numpad3";
        case MGL_KEY_NUMPAD4:            return "Numpad4";
        case MGL_KEY_NUMPAD5:            return "Numpad5";
        case MGL_KEY_NUMPAD6:            return "Numpad6";
        case MGL_KEY_NUMPAD7:            return "Numpad7";
        case MGL_KEY_NUMPAD8:            return "Numpad8";
        case MGL_KEY_NUMPAD9:            return "Numpad9";
        case MGL_KEY_F1:                 return "F1";
        case MGL_KEY_F2:                 return "F2";
        case MGL_KEY_F3:                 return "F3";
        case MGL_KEY_F4:                 return "F4";
        case MGL_KEY_F5:                 return "F5";
        case MGL_KEY_F6:                 return "F6";
        case MGL_KEY_F7:                 return "F7";
        case MGL_KEY_F8:                 return "F8";
        case MGL_KEY_F9:                 return "F9";
        case MGL_KEY_F10:                return "F10";
        case MGL_KEY_F11:                return "F11";
        case MGL_KEY_F12:                return "F12";
        case MGL_KEY_F13:                return "F13";
        case MGL_KEY_F14:                return "F14";
        case MGL_KEY_F15:                return "F15";
        case MGL_KEY_PAUSE:              return "Pause";
        case MGL_KEY_PRINTSCREEN:        return "PrintScreen";
        case MGL_KEY_NUMPAD_ENTER:       return "Numpad enter";
        case MGL_KEY_AUDIO_LOWER_VOLUME: return "Audio Lower";
        case MGL_KEY_AUDIO_RAISE_VOLUME: return "Audio Raise";
        case MGL_KEY_AUDIO_PLAY:         return "Audio Play";
        case MGL_KEY_AUDIO_STOP:         return "Audio Stop";
        case MGL_KEY_AUDIO_PAUSE:        return "Audio Pause";
        case MGL_KEY_AUDIO_MUTE:         return "Audio Mute";
        case MGL_KEY_AUDIO_PREV:         return "Audio Prev";
        case MGL_KEY_AUDIO_NEXT:         return "Audio Next";
        case MGL_KEY_AUDIO_REWIND:       return "Audio Rewind";
        case MGL_KEY_AUDIO_FORWARD:      return "Audio Forward";
        case MGL_KEY_DEAD_ACUTE:         return "´";
        case MGL_KEY_APOSTROPHE:         return "'";
        case MGL_KEY_F16:                return "F16";
        case MGL_KEY_F17:                return "F17";
        case MGL_KEY_F18:                return "F18";
        case MGL_KEY_F19:                return "F19";
        case MGL_KEY_F20:                return "F20";
        case MGL_KEY_F21:                return "F21";
        case MGL_KEY_F22:                return "F22";
        case MGL_KEY_F23:                return "F23";
        case MGL_KEY_F24:                return "F24";
        case MGL_KEY_EXCLAM:             return "!";
        case MGL_KEY_QUOTEDBL:           return "\"";
        case MGL_KEY_NUMBERSIGN:         return "#";
        case MGL_KEY_DOLLAR:             return "$";
        case MGL_KEY_PERCENT:            return "%";
        case MGL_KEY_AMPERSAND:          return "&";
        case MGL_KEY_PARENLEFT:          return "(";
        case MGL_KEY_PARENRIGHT:         return ")";
        case MGL_KEY_ASTERISK:           return "*";
        case MGL_KEY_PLUS:               return "+";
        case MGL_KEY_MINUS:              return "-";
        case MGL_KEY_COLON:              return ":";
        case MGL_KEY_LESS:               return "<";
        case MGL_KEY_GREATER:            return ">";
        case MGL_KEY_QUESTION:           return "?";
        case MGL_KEY_BRACKETLEFT:        return "[";
        case MGL_KEY_BRACKETRIGHT:       return "]";
        case MGL_KEY_ASCIICIRCUM:        return "^";
        case MGL_KEY_UNDERSCORE:         return "_";
        case MGL_KEY_GRAVE:              return "`";
        case __MGL_NUM_KEYS__:           return "";
    }
    return "";
}

bool mgl_key_is_modifier(mgl_key key) {
    return key >= MGL_KEY_LCONTROL && key <= MGL_KEY_RSYSTEM;
}

uint64_t mgl_key_to_x11_keysym(mgl_key key) {
#ifdef _WIN32
    /* X11 keysyms are meaningless on Win32; the function is only used by the
       X11 window backend. */
    (void)key;
    return 0;
#else
    if(key >= MGL_KEY_A && key <= MGL_KEY_Z)
        return XK_A + (key - MGL_KEY_A);
    if(key >= MGL_KEY_NUM0 && key <= MGL_KEY_NUM9)
        return XK_0 + (key - MGL_KEY_NUM0);
    if(key >= MGL_KEY_NUMPAD0 && key <= MGL_KEY_NUMPAD9)
        return XK_KP_0 + (key - MGL_KEY_NUMPAD0);

    switch(key) {
        case MGL_KEY_SPACE:              return XK_space;
        case MGL_KEY_BACKSPACE:          return XK_BackSpace;
        case MGL_KEY_TAB:                return XK_Tab;
        case MGL_KEY_ENTER:              return XK_Return;
        case MGL_KEY_ESCAPE:             return XK_Escape;
        case MGL_KEY_LCONTROL:           return XK_Control_L;
        case MGL_KEY_LSHIFT:             return XK_Shift_L;
        case MGL_KEY_LALT:               return XK_Alt_L;
        case MGL_KEY_LSYSTEM:            return XK_Super_L;
        case MGL_KEY_RCONTROL:           return XK_Control_R;
        case MGL_KEY_RSHIFT:             return XK_Shift_R;
        case MGL_KEY_RALT:               return XK_Alt_R;
        case MGL_KEY_RSYSTEM:            return XK_Super_R;
        case MGL_KEY_DELETE:             return XK_Delete;
        case MGL_KEY_HOME:               return XK_Home;
        case MGL_KEY_LEFT:               return XK_Left;
        case MGL_KEY_UP:                 return XK_Up;
        case MGL_KEY_RIGHT:              return XK_Right;
        case MGL_KEY_DOWN:               return XK_Down;
        case MGL_KEY_PAGEUP:             return XK_Page_Up;
        case MGL_KEY_PAGEDOWN:           return XK_Page_Down;
        case MGL_KEY_END:                return XK_End;
        case MGL_KEY_F1:                 return XK_F1;
        case MGL_KEY_F2:                 return XK_F2;
        case MGL_KEY_F3:                 return XK_F3;
        case MGL_KEY_F4:                 return XK_F4;
        case MGL_KEY_F5:                 return XK_F5;
        case MGL_KEY_F6:                 return XK_F6;
        case MGL_KEY_F7:                 return XK_F7;
        case MGL_KEY_F8:                 return XK_F8;
        case MGL_KEY_F9:                 return XK_F9;
        case MGL_KEY_F10:                return XK_F10;
        case MGL_KEY_F11:                return XK_F11;
        case MGL_KEY_F12:                return XK_F12;
        case MGL_KEY_F13:                return XK_F13;
        case MGL_KEY_F14:                return XK_F14;
        case MGL_KEY_F15:                return XK_F15;
        case MGL_KEY_INSERT:             return XK_Insert;
        case MGL_KEY_PAUSE:              return XK_Pause;
        case MGL_KEY_PRINTSCREEN:        return XK_Print;
        case MGL_KEY_NUMPAD_ENTER:       return XK_KP_Enter;
        case MGL_KEY_AUDIO_LOWER_VOLUME: return XF86XK_AudioLowerVolume;
        case MGL_KEY_AUDIO_RAISE_VOLUME: return XF86XK_AudioRaiseVolume;
        case MGL_KEY_AUDIO_PLAY:         return XF86XK_AudioPlay;
        case MGL_KEY_AUDIO_STOP:         return XF86XK_AudioStop;
        case MGL_KEY_AUDIO_PAUSE:        return XF86XK_AudioPause;
        case MGL_KEY_AUDIO_MUTE:         return XF86XK_AudioMute;
        case MGL_KEY_AUDIO_PREV:         return XF86XK_AudioPrev;
        case MGL_KEY_AUDIO_NEXT:         return XF86XK_AudioNext;
        case MGL_KEY_AUDIO_REWIND:       return XF86XK_AudioRewind;
        case MGL_KEY_AUDIO_FORWARD:      return XF86XK_AudioForward;
        case MGL_KEY_DEAD_ACUTE:         return XK_dead_acute;
        case MGL_KEY_APOSTROPHE:         return XK_apostrophe;
        case MGL_KEY_F16:                return XK_F16;
        case MGL_KEY_F17:                return XK_F17;
        case MGL_KEY_F18:                return XK_F18;
        case MGL_KEY_F19:                return XK_F19;
        case MGL_KEY_F20:                return XK_F20;
        case MGL_KEY_F21:                return XK_F21;
        case MGL_KEY_F22:                return XK_F22;
        case MGL_KEY_F23:                return XK_F23;
        case MGL_KEY_F24:                return XK_F24;
        case MGL_KEY_EXCLAM:             return XK_exclam;
        case MGL_KEY_QUOTEDBL:           return XK_quotedbl;
        case MGL_KEY_NUMBERSIGN:         return XK_numbersign;
        case MGL_KEY_DOLLAR:             return XK_dollar;
        case MGL_KEY_PERCENT:            return XK_percent;
        case MGL_KEY_AMPERSAND:          return XK_ampersand;
        case MGL_KEY_PARENLEFT:          return XK_parenleft;
        case MGL_KEY_PARENRIGHT:         return XK_parenright;
        case MGL_KEY_ASTERISK:           return XK_asterisk;
        case MGL_KEY_PLUS:               return XK_plus;
        case MGL_KEY_COMMA:              return XK_comma;
        case MGL_KEY_MINUS:              return XK_minus;
        case MGL_KEY_PERIOD:             return XK_period;
        case MGL_KEY_SLASH:              return XK_slash;
        case MGL_KEY_COLON:              return XK_colon;
        case MGL_KEY_SEMICOLON:          return XK_semicolon;
        case MGL_KEY_LESS:               return XK_less;
        case MGL_KEY_EQUAL:              return XK_equal;
        case MGL_KEY_GREATER:            return XK_greater;
        case MGL_KEY_QUESTION:           return XK_question;
        case MGL_KEY_BRACKETLEFT:        return XK_bracketleft;
        case MGL_KEY_BACKSLASH:          return XK_backslash;
        case MGL_KEY_BRACKETRIGHT:       return XK_bracketright;
        case MGL_KEY_ASCIICIRCUM:        return XK_asciicircum;
        case MGL_KEY_UNDERSCORE:         return XK_underscore;
        case MGL_KEY_GRAVE:              return XK_grave;
        default:                         return XK_VoidSymbol;
    }
    return XK_VoidSymbol;
#endif /* _WIN32 */
}
