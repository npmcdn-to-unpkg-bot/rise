#include "avrlib.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

void sys_enable_interrupts() {
  sei();
}

void sys_disable_interrupts() {
  cli();
}

void sys_delay_us(int amount) {
  _delay_us(amount);
}
