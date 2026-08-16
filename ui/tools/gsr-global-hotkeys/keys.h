#ifndef KEYS_H
#define KEYS_H

#include <stdbool.h>
#include <stdint.h>

bool is_keyboard_key(uint32_t keycode);
bool is_mouse_button(uint32_t keycode);

#endif /* KEYS_H */
