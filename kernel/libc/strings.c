#include "../drivers/io/screen.h"

int strcmp(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (*p1 && *p1 == *p2) ++p1, ++p2;

    return (*p1 > *p2) - (*p2  > *p1);
}

char* splice(char* string, int index, char delim) {
	char cur_char;
	int i = 0;
	int j = 0;

	static char output[512];
	for(i = 0; i < 512; i++) output[i] = 0;

	i = 0;

	if(index == 0) {
		i = 0;
		do {
			cur_char = string[j];
			if(cur_char == 0) break;
			output[i] = cur_char;
			i++;
			j++;
		} while(cur_char != delim);
		
		if(string[i]) output[i-1] = 0;
		output[i] = 0;

	} else {
		do {
			do {
				cur_char = string[j];
				if(cur_char == 0) break;
				j++;
			} while(cur_char != delim);
			i++;
		} while(i != index);

		i = 0;
		do {
			cur_char = string[j];	
			if(cur_char == 0) break;
			output[i] = cur_char;
			i++;
			j++;
		} while(cur_char != delim);
		output[i] = 0;
	}

	if(string[j]) output[i-1] = 0;

	return output;
}

int strlen(char* string) {
	int pos = 0;
	while(string[pos] != 0)	pos++;
	return pos;
}

int atoi(char* txt)
{   
    int sum, digit, i;
    sum = 0;
    i = 0;
    char cur_char;
    cur_char = 0;

    do {
    	cur_char = txt[i + 1];
        digit = txt[i] - 0x30;
        sum = (sum * 10) + digit;
        i++;
    } while(cur_char != 0);

    return sum;
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
