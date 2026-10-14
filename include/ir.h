/*
 * Infrared remote control (user/ir.c)
 *
 * A NEC receiver module (TSOP, VS1838...) is read on GPIO2. Its keys are mapped to the
 * same commands as the UART/telnet interface (see docs/interface.txt).
 */

#ifndef __IR_H__
#define __IR_H__

#define IR_PIN 2		// GPIO of the receiver output

// configure the pin, install the interrupt and start the task that executes the keys
void irInit(void);

#endif
