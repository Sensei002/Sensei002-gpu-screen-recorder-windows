#ifndef MGL_UTF8_H
#define MGL_UTF8_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool mgl_utf8_get_codepoint_length(unsigned char b, size_t *codepoint_length);

/*
    Returns false on failure. |decoded_codepoint| is set to |str[0]| if size > 0 and |codepoint_length| is set to 1
*/
bool mgl_utf8_decode(const unsigned char *str, size_t size, uint32_t *decoded_codepoint, size_t *codepoint_length);
/*
    |str| should be the start of the utf8 string and |size| is the size of the string.
    Returns the index of the start of the codepoint that starts at or before |offset|,
    or if the string contains invalid utf8 then the index to the invalid character is returned.
    Returns 0 if start of codepoint is not found.
*/
size_t mgl_utf8_get_start_of_codepoint(const unsigned char *str, size_t size, size_t offset);
/* Returns |size| if not found. Invalid characters are ignored. */
size_t mgl_utf8_index_to_byte_index(const unsigned char *str, size_t size, size_t index);
/* Invalid characters are ignored. */
size_t mgl_byte_index_to_utf8_index(const unsigned char *str, size_t size, size_t index);
/* Invalid characters are ignored. */
size_t mgl_utf8_get_character_count(const unsigned char *str, size_t size);
/* Invalid characters are ignored. */
size_t mgl_utf32_get_utf8_count(const uint32_t *str, size_t size);

/*
    Returns the actual size of |utf32_str| after converted.
    |utf32_str| should be at least |utf8_size| in size to guarantee that all characters fit.
    Invalid characters are ignored.
*/
size_t mgl_utf8_to_utf32(const unsigned char *utf8_str, size_t utf8_size, uint32_t *utf32_str, size_t utf32_size);
/*
    Returns the actual size of |utf8_str| after converted.
    |utf8_str| should be at least |utf32_size| * 4 in size to guarantee that all characters fit.
    Invalid characters are ignored.
*/
size_t mgl_utf32_to_utf8(const uint32_t *utf32_str, size_t utf32_size, unsigned char *utf8_str, size_t utf8_size);

#endif /* MGL_UTF8_H */

