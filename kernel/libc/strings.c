#include "../drivers/io/screen.h"

int strcmp(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (*p1 && *p1 == *p2) ++p1, ++p2;

    return (*p1 > *p2) - (*p2  > *p1);
}

char *splice(char *string, int index, char delim) {
    static char output[8][512]; static unsigned slot;
    char *result = output[slot++ % 8]; result[0] = 0;
    if (!string || index < 0) return result;
    while (index && *string) { if (*string++ == delim) index--; }
    if (index) return result;
    unsigned n = 0;
    while (*string && *string != delim && n < 511) result[n++] = *string++;
    result[n] = 0; return result;
}

int strlen(char* string) {
	int pos = 0;
	while(string[pos] != 0)	pos++;
	return pos;
}

int atoi(char *text) {
    int value = 0, sign = 1;
    if (*text == '-') { sign = -1; text++; }
    while (*text >= '0' && *text <= '9') {
        int digit = *text++ - '0';
        if (value > (2147483647 - digit) / 10) return sign > 0 ? 2147483647 : -2147483647;
        value = value * 10 + digit;
    }
    return value * sign;
}

uint32_t htoi(char *hex) {
    uint32_t val = 0;
    while (*hex) {
        // get current character then increment
        uint8_t byte = *hex++; 
        // transform hex character to the 4bit equivalent number, using the ascii table indexes
        if (byte >= '0' && byte <= '9') byte = byte - '0';
        else if (byte >= 'a' && byte <='f') byte = byte - 'a' + 10;
        else if (byte >= 'A' && byte <='F') byte = byte - 'A' + 10;    
        // shift 4 to make space for new digit, and add the 4 bits of the new digit 
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}
