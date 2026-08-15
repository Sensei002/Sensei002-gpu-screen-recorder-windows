#include "../include/json.h"

#define SJ_IMPL
#include "../external/sj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

bool gsr_json_string_equals(const sj_Value *value, const char *str) {
    const size_t value_size = value->end - value->start;
    return strlen(str) == value_size && memcmp(value->start, str, value_size) == 0;
}

bool gsr_json_number_to_int64(const sj_Value *value, int64_t *result) {
    if(value->type != SJ_NUMBER)
        return false;

    char buffer[32];
    const size_t value_size = value->end - value->start;
    if(value_size == 0 || value_size >= sizeof(buffer))
        return false;

    memcpy(buffer, value->start, value_size);
    buffer[value_size] = '\0';

    char *number_end = NULL;
    errno = 0;
    const long long parsed_value = strtoll(buffer, &number_end, 10);
    if(errno != 0 || number_end != buffer + value_size)
        return false;

    *result = parsed_value;
    return true;
}

void gsr_json_escape_string(char *buffer, size_t buffer_size, const char *str) {
    char escape_buffer[8];
    size_t offset = 0;
    buffer[0] = '\0';

    for(size_t i = 0; str[i] != '\0'; ++i) {
        const unsigned char c = str[i];
        const char *escaped = escape_buffer;
        switch(c) {
            case '"':  escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            case '\n': escaped = "\\n";  break;
            case '\r': escaped = "\\r";  break;
            case '\t': escaped = "\\t";  break;
            default: {
                if(c < 0x20)
                    snprintf(escape_buffer, sizeof(escape_buffer), "\\u%04x", c);
                else
                    snprintf(escape_buffer, sizeof(escape_buffer), "%c", c);
                break;
            }
        }

        const size_t escaped_size = strlen(escaped);
        if(offset + escaped_size >= buffer_size)
            break;

        memcpy(buffer + offset, escaped, escaped_size);
        offset += escaped_size;
        buffer[offset] = '\0';
    }
}
