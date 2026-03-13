/**
 * XL2400 / XL2400P / XL2400T 2.4G Driver
 * Ported from py32f0-template (IOsetting), adapted for STM32
 * Options: #define USE_XL2400P (default) or USE_XL2400
 */
#ifndef __XL2400_H__
#define __XL2400_H__

#include <stdint.h>
#include "xl2400_port.h"

#define USE_XL2400P  /* XL2400T compatible with XL2400P */

#define XL2400_PLOAD_WIDTH 32
#define XL2400_PL_WIDTH_MAX 64
#define XL2400_TEST_ADDR "XL240"

/* Result: 0=OK, 1=Error */
#define XL2400_OK   0
#define XL2400_ERR  1

/* SPI Commands */
#define XL2400_CMD_R_REGISTER      0x00
#define XL2400_CMD_W_REGISTER      0x20
#define XL2400_CMD_R_RX_PAYLOAD    0x61
#define XL2400_CMD_W_TX_PAYLOAD    0xA0
#define XL2400_CMD_W_TX_PAYLOAD_NOACK 0xB0
#define XL2400_CMD_FLUSH_TX        0xE1
#define XL2400_CMD_FLUSH_RX        0xE2
#define XL2400_CMD_R_RX_PL_WID     0x60
#define XL2400_CMD_NOP             0xFF

/* Registers */
#define XL2400_REG_CFG_TOP     0x00
#define XL2400_REG_EN_AA       0x01
#define XL2400_REG_EN_RXADDR   0x02
#define XL2400_REG_SETUP_AW    0x03
#define XL2400_REG_SETUP_RETR  0x04
#define XL2400_REG_RF_CH       0x05
#define XL2400_REG_RF_SETUP    0x06
#define XL2400_REG_STATUS      0x07
#define XL2400_REG_TX_ADDR     0x10
#define XL2400_REG_RX_ADDR_P0  0x0A
#define XL2400_REG_RX_ADDR_P1  0x0B
#define XL2400_REG_RX_PW_PX    0x11
#define XL2400_REG_ANALOG_CFG0 0x12
#define XL2400_REG_ANALOG_CFG3 0x15
#define XL2400_REG_FIFO_STATUS 0x17
#define XL2400_REG_FEATURE     0x1D
#define XL2400_REG_DYNPD       0x1C
#define XL2400_REG_RSSI        0x09

/* Status flags */
#define XL2400_FLAG_RX_DR  0x40
#define XL2400_FLAG_TX_DS  0x20
#define XL2400_FLAG_MAX_RT 0x10
#define XL2400_FLAG_TX_FULL 0x01

/* RF params */
#define XL2400_RF_DR_250K  0x20
#define XL2400_RF_DR_1M    0x00
#define XL2400_RF_0DB      0x10
#define XL2400_RF__12DB    0x04

/* API */
uint8_t XL2400_WriteReg(uint8_t reg, uint8_t value);
uint8_t XL2400_ReadReg(uint8_t reg);
void XL2400_WriteFromBuf(uint8_t reg, const uint8_t *pBuf, uint8_t len);
void XL2400_ReadToBuf(uint8_t reg, uint8_t *pBuf, uint8_t len);

void XL2400_CE_Low(void);
void XL2400_CE_High(void);

uint8_t XL2400_SPI_Test(void);  /* 0=pass, 1=fail */

void XL2400_Init(void);
void XL2400_SetChannel(uint8_t channel);
void XL2400_SetTxAddress(const uint8_t *address);
void XL2400_SetRxAddress(const uint8_t *address);
void XL2400_SetPower(uint8_t power);

void XL2400_Sleep(void);
void XL2400_WakeUp(void);
void XL2400_Reset(void);

void XL2400_SetTxMode(void);
void XL2400_SetRxMode(void);

uint8_t XL2400_Tx(const uint8_t *ucPayload, uint8_t length);
uint8_t XL2400_TxNoAck(const uint8_t *ucPayload, uint8_t length);
uint8_t XL2400_Rx(void);

uint8_t XL2400_ReadStatus(void);
void XL2400_ClearStatus(void);
void XL2400_FlushRxTX(void);

/* RX payload buffer (read by caller after XL2400_Rx returns RX_DR) */
extern uint8_t xl2400_rx_buf[XL2400_PL_WIDTH_MAX + 1];
uint8_t XL2400_RxGetLength(void);

#endif
