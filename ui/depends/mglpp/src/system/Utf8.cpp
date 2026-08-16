#include "../../include/mglpp/system/Utf8.hpp"

extern "C" {
#include <mgl/system/utf8.h>
}

namespace mgl {
    bool utf8_get_codepoint_length(unsigned char b, size_t *codepoint_length) {
        return mgl_utf8_get_codepoint_length(b, codepoint_length);
    }

    bool utf8_decode(const unsigned char *str, size_t size, uint32_t *decoded_codepoint, size_t *codepoint_length) {
        return mgl_utf8_decode(str, size, decoded_codepoint, codepoint_length);
    }

    size_t utf8_get_start_of_codepoint(const unsigned char *str, size_t size, size_t offset) {
        return mgl_utf8_get_start_of_codepoint(str, size, offset);
    }

    size_t utf8_index_to_byte_index(const unsigned char *str, size_t size, size_t index) {
        return mgl_utf8_index_to_byte_index(str, size, index);
    }

    size_t byte_index_to_utf8_index(const unsigned char *str, size_t size, size_t index) {
        return mgl_byte_index_to_utf8_index(str, size, index);
    }

    size_t utf8_get_character_count(const unsigned char *str, size_t size) {
        return mgl_utf8_get_character_count(str, size);
    }

    size_t utf32_get_utf8_count(const uint32_t *str, size_t size) {
        return mgl_utf32_get_utf8_count(str, size);
    }

    std::u32string utf8_to_utf32(const unsigned char *utf8_str, size_t utf8_size) {
        std::u32string result;
        result.resize(utf8_size);
        if(utf8_size > 0)
            result.resize(mgl_utf8_to_utf32(utf8_str, utf8_size, (uint32_t*)&result[0], result.size()));
        return result;
    }

    std::u32string utf8_to_utf32(const std::string &str) {
        return utf8_to_utf32((const unsigned char*)str.data(), str.size());
    }

    std::string utf32_to_utf8(const uint32_t *utf32_str, size_t utf32_size) {
        std::string result;
        result.resize(utf32_size * 4);
        if(utf32_size > 0)
            result.resize(mgl_utf32_to_utf8(utf32_str, utf32_size, (unsigned char*)&result[0], result.size()));
        return result;
    }

    std::string utf32_to_utf8(const std::u32string &str) {
        return utf32_to_utf8((const uint32_t*)str.data(), str.size());
    }
}
