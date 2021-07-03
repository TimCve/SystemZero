#include "timer.h"
#include "isr.h"
#include "../drivers/utils/ports.h"
#include "../drivers/io/screen.h"

uint32_t tick = 0;

static void timer_cb(registers_t regs) {
	tick++;
}

void init_timer(uint32_t freq) {
	// install the sleep function
	register_interrupt_handler(IRQ0, timer_cb);

	uint32_t divisor = 1193180 / freq;
	uint8_t low = (uint8_t)(divisor & 0xFF);
	uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
	port_byte_out(0x43, 0x36); // command
	port_byte_out(0x40, low);
	port_byte_out(0x40, high);
}

int get_tick() {
	return tick;
}