#ifndef __UART_ARM_H
#define __UART_ARM_H

#include <stdio.h>

#define UART_DEVNUM_DEBUG 0
#define UART_DEVNUM_MODEM 1
#define UART_DEVNUM_SENSOR 2

int uart_putc_init(void);

#endif
