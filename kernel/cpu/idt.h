#include <stdint.h>

#define KERNEL_CS 0x08

// every interrupt gate/handler is defined like this
typedef struct {	
	uint16_t low_offset; // lower 16 bits of handler function address
	uint16_t sel; // kernel segment selector
	uint8_t always0;
	/*
	Bit 7: Interrupt is present
	Bits 6-5: Privilege level of caller 0 - 3 (0 is highest priv. 3 is lowest priv.)
	Bit 4: Set to 0
	Bits: 3-0: 0b1110 = 0d14 (means "32-bit interrupt gate/handler") 
	*/
	uint8_t flags;
	uint16_t high_offset; // higher 16 bits of handler function address
} __attribute__((packed)) idt_gate_t;

// pointer to the array of interrupt handlers
// lidt will read it
typedef struct {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) idt_register_t;

#define IDT_ENTRIES 256
idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;

#define low_16(address) (uint16_t)((address) & 0xFFFF)
#define high_16(address) (uint16_t)(((address) >> 16) & 0xFFFF)

void set_idt_gate(int n, uint32_t handler);
void set_idt();