/**
 * XL2400 Port Layer - Platform abstraction
 * Implement these for your MCU (STM32, PY32, etc.)
 */
#ifndef __XL2400_PORT_H__
#define __XL2400_PORT_H__

#include <stdint.h>

/* NSS (CS) - active low */
void XL2400_NSS_Low(void);
void XL2400_NSS_High(void);

/* CE (Chip Enable) - controls TX/RX state */
void XL2400_CE_Low(void);
void XL2400_CE_High(void);

/* SPI transfer one byte, return received byte */
uint8_t XL2400_SPI_Transfer(uint8_t data);

/* Delay in milliseconds */
void XL2400_DelayMs(uint32_t ms);

/* Optional: debug print (can be empty macro if no UART) */
#define XL2400_DEBUG_PRINTF(...)  /* empty, override in port impl */

#endif
