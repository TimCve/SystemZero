#include "ports.h"

unsigned char port_byte_in(unsigned short port) {
	unsigned char result;
	__asm__ volatile("in %%dx, %%al" : "=a" (result) : "d" (port) : "memory");
	return result;
}

void port_byte_out(unsigned short port, unsigned char data) {
	__asm__ volatile("out %%al, %%dx" : : "a" (data), "d" (port) : "memory");
}

unsigned short port_word_in(unsigned short port) {
	unsigned short result;
	__asm__ volatile("in %%dx, %%ax" : "=a" (result) : "d" (port) : "memory");
	return result;
}

void port_word_out(unsigned short port, unsigned short data) {
	__asm__ volatile("out %%ax, %%dx" : : "a" (data), "d" (port) : "memory");
}

unsigned long port_long_in(unsigned short port) {
	unsigned long result;
	__asm__ volatile("in %%dx, %%eax" : "=a" (result) : "d" (port) : "memory");
	return result;
}

void port_long_out(unsigned short port, unsigned long data) {
	__asm__ volatile("out %%eax, %%dx" : : "a" (data), "d" (port) : "memory");
}
