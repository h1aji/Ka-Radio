#pragma once

#define GPIO2 2

void gpio2_output_conf(void);
void gpio2_output_set(uint8 value);
void gpio16_output_conf(void);
void gpio16_output_set(uint8 value);
void gpio16_input_conf(void);
uint8 gpio16_input_get(void);

