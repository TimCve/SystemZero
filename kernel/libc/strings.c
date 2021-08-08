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
	return sizeof(string) / sizeof(char);
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