#include "../../include/mgl/system/utf8.h"

bool mgl_utf8_get_codepoint_length(unsigned char b, size_t *codepoint_length) {
    if((b & 0x80) == 0) {
        *codepoint_length = 1;
        return true;
    } else if((b & 0xE0) == 0xC0) {
        *codepoint_length = 2;
        return true;
    } else if((b & 0xF0) == 0xE0) {
        *codepoint_length = 3;
        return true;
    } else if((b & 0xF8) == 0xF0) {
        *codepoint_length = 4;
        return true;
    } else {
        return false;
    }
}

/* TODO: Optimize (remove branching, etc) */
bool mgl_utf8_decode(const unsigned char *str, size_t size, uint32_t *decoded_codepoint, size_t *codepoint_length) {
    if(size == 0) {
        *decoded_codepoint = 0;
        *codepoint_length = 0;
        return false;
    }

    size_t clen;
    if(!mgl_utf8_get_codepoint_length(str[0], &clen)) {
        *decoded_codepoint = str[0];
        *codepoint_length = 1;
        return false;
    }

    if(size < clen) {
        *decoded_codepoint = str[0];
        *codepoint_length = 1;
        return false;
    }

    for(size_t i = 1; i < clen; ++i) {
        if((str[i] & 0xC0) != 0x80) {
            *decoded_codepoint = str[0];
            *codepoint_length = 1;
            return false;
        }
    }

    uint32_t codepoint;
    switch(clen) {
        case 1:
            codepoint =  (uint32_t)(str[0] & 0x7F);
            break;
        case 2:
            codepoint = ((uint32_t)(str[0] & 0x1F) << 6);
            codepoint |= (uint32_t)(str[1] & 0x3F);
            break;
        case 3:
            codepoint =  ((uint32_t)(str[0] & 0x0F) << 12);
            codepoint |= ((uint32_t)(str[1] & 0x3F) << 6);
            codepoint |=  (uint32_t)(str[2] & 0x3F);
            break;
        case 4:
            codepoint =  ((uint32_t)(str[0] & 0x07) << 18);
            codepoint |= ((uint32_t)(str[1] & 0x3F) << 12);
            codepoint |= ((uint32_t)(str[2] & 0x3F) << 6);
            codepoint |=  (uint32_t)(str[3] & 0x3F);
            break;
    }

    *codepoint_length = clen;
    *decoded_codepoint = codepoint;
    return true;
}

/* TODO: Optimize (remove branching, etc) */
size_t mgl_utf8_get_start_of_codepoint(const unsigned char *str, size_t size, size_t offset) {
    if(size == 0)
        return 0;

    if(offset > size - 1)
        offset = size - 1;

    /* i <= offset is an overflow (underflow?) check */
    for(size_t i = offset; i <= offset; --i) {
        if((str[i] & 0xC0) != 0x80)
            return i;
    }

    return 0;
}

/* TODO: Optimize (remove branching, etc) */
size_t mgl_utf8_index_to_byte_index(const unsigned char *str, size_t size, size_t index) {
    size_t codepoint_index = 0;
    for(size_t i = 0; i < size;) {
        if(codepoint_index >= index)
            return i;

        const unsigned char *cp = &str[i];
        uint32_t codepoint;
        size_t clen;
        if(!mgl_utf8_decode(cp, size - i, &codepoint, &clen)) {
            i += 1;
            continue;
        }

        i += clen;
        ++codepoint_index;
    }
    return size;
}

size_t mgl_byte_index_to_utf8_index(const unsigned char *str, size_t size, size_t index) {
    if(index == 0)
        return 0;

    size_t codepoint_index = 0;
    for(size_t i = 0; i < size;) {
        if(i >= index)
            break;

        const unsigned char *cp = &str[i];
        uint32_t codepoint;
        size_t clen;
        if(!mgl_utf8_decode(cp, size - i, &codepoint, &clen)) {
            i += 1;
            continue;
        }

        i += clen;
        ++codepoint_index;
    }
    return codepoint_index;
}

size_t mgl_utf8_get_character_count(const unsigned char *str, size_t size) {
    size_t character_count = 0;
    for(size_t i = 0; i < size;) {
        uint32_t codepoint;
        size_t clen;
        if(!mgl_utf8_decode(&str[i], size - i, &codepoint, &clen)) {
            i += 1;
            continue;
        }

        i += clen;
        ++character_count;
    }
    return character_count;
}

size_t mgl_utf32_get_utf8_count(const uint32_t *str, size_t size) {
    size_t character_count = 0;
    for(size_t i = 0; i < size; ++i) {
        const uint32_t codepoint = str[i];
        if(codepoint <= 0x7F)
            character_count += 1;
        else if(codepoint <= 0x7FF)
            character_count += 2;
        else if(codepoint <= 0xFFFF)
            character_count += 3;
        else if(codepoint <= 0x10FFFF)
            character_count += 4;
        else {
            /* Invalid character, what do? for now just skip the character */
        }
    }
    return character_count;
}

size_t mgl_utf8_to_utf32(const unsigned char *utf8_str, size_t utf8_size, uint32_t *utf32_str, size_t utf32_size) {
    size_t codepoint_index = 0;
    for(size_t i = 0; i < utf8_size;) {
        if(codepoint_index >= utf32_size)
            break;

        const unsigned char *cp = &utf8_str[i];
        uint32_t codepoint;
        size_t clen;
        if(!mgl_utf8_decode(cp, utf8_size - i, &codepoint, &clen)) {
            /* Invalid character, what do? for now just skip the character */
            i += 1;
            continue;
        }

        utf32_str[codepoint_index] = codepoint;
        i += clen;
        ++codepoint_index;
    }
    return codepoint_index;
}

size_t mgl_utf32_to_utf8(const uint32_t *utf32_str, size_t utf32_size, unsigned char *utf8_str, size_t utf8_size) {
    size_t codepoint_index = 0;
    for(size_t i = 0; i < utf32_size; ++i) {
        const uint32_t codepoint = utf32_str[i];
        if(codepoint <= 0x7F) {
            if(codepoint_index >= utf8_size)
                break;

            utf8_str[codepoint_index] = codepoint; /* & 0x7F */
            codepoint_index += 1;
        } else if(codepoint <= 0x7FF) {
            if(codepoint_index + 1 >= utf8_size)
                break;

            utf8_str[codepoint_index + 0] = 0xC0 | ((codepoint & 0x7C0) >> 6);
            utf8_str[codepoint_index + 1] = 0x80 | (codepoint & 0x3F);
            codepoint_index += 2;
        } else if(codepoint <= 0xFFFF) {
            if(codepoint_index + 2 >= utf8_size)
                break;

            utf8_str[codepoint_index + 0] = 0xE0 | ((codepoint & 0xF000) >> 12);
            utf8_str[codepoint_index + 1] = 0x80 | ((codepoint & 0xFC0) >> 6);
            utf8_str[codepoint_index + 2] = 0x80 | (codepoint & 0x3F);
            codepoint_index += 3;
        } else if(codepoint <= 0x10FFFF) {
            if(codepoint_index + 3 >= utf8_size)
                break;

            utf8_str[codepoint_index + 0] = 0xF0 | ((codepoint & 0x1C0000) >> 18);
            utf8_str[codepoint_index + 1] = 0x80 | ((codepoint & 0x3F000) >> 12);
            utf8_str[codepoint_index + 2] = 0x80 | ((codepoint & 0xFC0) >> 6);
            utf8_str[codepoint_index + 3] = 0x80 | (codepoint & 0x3F);
            codepoint_index += 4;
        } else {
            /* Invalid character, what do? for now just skip the character */
        }
    }
    return codepoint_index;
}
