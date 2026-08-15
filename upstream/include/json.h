#ifndef GSR_JSON_H
#define GSR_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../external/sj.h"

bool gsr_json_string_equals(const sj_Value *value, const char *str);
/* Fails if |value| is not a json number without a fractional part */
bool gsr_json_number_to_int64(const sj_Value *value, int64_t *result);
/* An escaped string can become 6 times as large as |str|. The result is truncated when it doesn't fit in |buffer| */
void gsr_json_escape_string(char *buffer, size_t buffer_size, const char *str);

#endif /* GSR_JSON_H */
