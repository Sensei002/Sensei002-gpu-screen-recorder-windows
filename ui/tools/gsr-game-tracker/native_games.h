#ifndef NATIVE_GAMES_H
#define NATIVE_GAMES_H

#include <stdbool.h>
#include <stddef.h>

/* Checks for native games (or emulators) that are not steam games and neither ends with .x86_64, .x86 or .x64 */
bool is_process_name_native_game(const char *process_name, size_t size);

#endif /* NATIVE_GAMES_H */
