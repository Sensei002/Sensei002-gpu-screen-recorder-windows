#ifndef MGLPP_UTF8_HPP
#define MGLPP_UTF8_HPP

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace mgl {
    bool utf8_get_codepoint_length(unsigned char b, size_t *codepoint_length);

    // Returns false on failure. |decoded_codepoint| is set to |str[0]| if size > 0 and |codepoint_length| is set to 1
    bool utf8_decode(const unsigned char *str, size_t size, uint32_t *decoded_codepoint, size_t *codepoint_length);
    /*
        |str| should be the start of the utf8 string and |size| is the size of the string.
        Returns the index of the start of the codepoint that starts at or before |offset|,
        or if the string contains invalid utf8 then the index to the invalid character is returned.
        Returns 0 if start of codepoint is not found.
    */
    size_t utf8_get_start_of_codepoint(const unsigned char *str, size_t size, size_t offset);
    // Returns |size| if not found. Invalid characters are ignored.
    size_t utf8_index_to_byte_index(const unsigned char *str, size_t size, size_t index);
    // Invalid characters are ignored.
    size_t byte_index_to_utf8_index(const unsigned char *str, size_t size, size_t index);
    // Invalid characters are ignored.
    size_t utf8_get_character_count(const unsigned char *str, size_t size);
    // Invalid characters are ignored.
    size_t utf32_get_utf8_count(const uint32_t *str, size_t size);

    // Invalid characters are ignored.
    std::u32string utf8_to_utf32(const unsigned char *utf8_str, size_t utf8_size);
    std::u32string utf8_to_utf32(const std::string &str);
    // Invalid characters are ignored.
    std::string utf32_to_utf8(const uint32_t *utf32_str, size_t utf32_size);
    std::string utf32_to_utf8(const std::u32string &str);
}

#endif /* MGLPP_UTF8_HPP */
