/**
 * XL2400 / XL2400P / XL2400T Driver
 * Ported from py32f0-template, platform-agnostic via xl2400_port.h
 */
#include "xl2400.h"

static uint8_t xl2400_state;
static uint8_t cbuf[2];
static uint8_t xbuf[XL2400_PL_WIDTH_MAX + 1];
static uint8_t xl2400_rx_len;
uint8_t xl2400_rx_buf[XL2400_PL_WIDTH_MAX + 1];

#define SPI_TxRxByte(d)  XL2400_SPI_Transfer(d)
#define LL_mDelay(ms)    XL2400_DelayMs(ms)

static uint8_t xl2400_rx_len;

uint8_t XL2400_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t ret;
    XL2400_NSS_Low();
    SPI_TxRxByte(reg);
    ret = SPI_TxRxByte(value);
    XL2400_NSS_High();
    return ret;
}

uint8_t XL2400_ReadReg(uint8_t reg)
{
    uint8_t ret;
    XL2400_NSS_Low();
    SPI_TxRxByte(reg);
    ret = SPI_TxRxByte(XL2400_CMD_NOP);
    XL2400_NSS_High();
    return ret;
}

void XL2400_WriteFromBuf(uint8_t reg, const uint8_t *pBuf, uint8_t len)
{
    uint8_t i;
    XL2400_NSS_Low();
    SPI_TxRxByte(reg);
    for (i = 0; i < len; i++)
        SPI_TxRxByte(*pBuf++);
    XL2400_NSS_High();
}

void XL2400_ReadToBuf(uint8_t reg, uint8_t *pBuf, uint8_t len)
{
    uint8_t i;
    XL2400_NSS_Low();
    SPI_TxRxByte(reg);
    for (i = 0; i < len; i++)
        pBuf[i] = SPI_TxRxByte(XL2400_CMD_NOP);
    XL2400_NSS_High();
}

/* CE_Low/High 由 xl2400_port.c 实现（GPIO 控制） */

uint8_t XL2400_SPI_Test(void)
{
    uint8_t i;
    const uint8_t *ptr = (const uint8_t *)XL2400_TEST_ADDR;

#ifdef USE_XL2400P
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x02);
    LL_mDelay(2);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x3E);
    LL_mDelay(2);
    XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_ANALOG_CFG3, xbuf, 6);
    xbuf[5] |= 0x6d;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG3, xbuf, 6);
#endif

    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_TX_ADDR, ptr, 5);
    XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_TX_ADDR, xbuf, 5);
    for (i = 0; i < 5; i++) {
        if (xbuf[i] != ptr[i])
            return XL2400_ERR;
    }
    return XL2400_OK;
}

void XL2400_Init(void)
{
#ifdef USE_XL2400P
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x02);
    LL_mDelay(2);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x3E);
    LL_mDelay(2);
    XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_ANALOG_CFG3, xbuf, 6);
    xbuf[5] |= 0x6d;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG3, xbuf, 6);
#else
    XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_ANALOG_CFG0, xbuf, 13);
    xbuf[4] &= ~0x04;
    xbuf[12] |= 0x40;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG0, xbuf, 13);
    xbuf[0] = 0x7E;
    xbuf[1] = 0x82;
    xbuf[2] = 0x0B;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, xbuf, 3);
    XL2400_CE_Low();
    XL2400_ClearStatus();
#endif

    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_EN_AA, 0x3F);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_EN_RXADDR, 0x3F);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_SETUP_AW, 0xAF);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_SETUP_RETR, 0x33);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_RF_SETUP, XL2400_RF_DR_250K | XL2400_RF_0DB);

    cbuf[0] = XL2400_PLOAD_WIDTH;
    cbuf[1] = XL2400_PLOAD_WIDTH;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RX_PW_PX, cbuf, 2);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_DYNPD, 0x00);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_FEATURE, 0x18);

    cbuf[0] = 0x10;
    cbuf[1] = 0x00;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RSSI, cbuf, 2);
}

void XL2400_SetChannel(uint8_t channel)
{
    if (channel > 80)
        channel = 80;
#ifdef USE_XL2400P
    cbuf[0] = XL2400_ReadReg(XL2400_CMD_R_REGISTER | XL2400_REG_EN_AA);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_EN_AA, cbuf[0] & ~0x40);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_RF_CH, 0x60 + channel);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_EN_AA, cbuf[0] | 0x40);
#else
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG0, 0x06);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG0, 0x0E);
    cbuf[0] = 0x60 + channel;
    cbuf[1] = 0x09;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RF_CH, cbuf, 2);
    cbuf[1] |= 0x20;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RF_CH, cbuf, 2);
#endif
}

void XL2400_SetTxAddress(const uint8_t *address)
{
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_TX_ADDR, address, 5);
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RX_ADDR_P0, address, 5);
}

void XL2400_SetRxAddress(const uint8_t *address)
{
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RX_ADDR_P1, address, 5);
}

void XL2400_SetPower(uint8_t power)
{
#ifdef USE_XL2400P
    XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_RF_SETUP, xbuf, 2);
    xbuf[1] = power;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RF_SETUP, xbuf, 2);
#else
    XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_RF_CH, xbuf, 3);
    xbuf[2] = power;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_RF_CH, xbuf, 3);
#endif
}

void XL2400_Sleep(void)
{
#ifdef USE_XL2400P
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x00);
#else
    XL2400_CE_Low();
    XL2400_ClearStatus();
    xbuf[0] = 0x7C;
    xbuf[1] = 0x82;
    xbuf[2] = 0x03;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, xbuf, 3);
#endif
}

void XL2400_WakeUp(void)
{
#ifdef USE_XL2400
    xbuf[0] = 0x7E;
    xbuf[1] = 0x82;
    xbuf[2] = 0x0B;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, xbuf, 3);
    XL2400_CE_Low();
    XL2400_ClearStatus();
#endif
}

void XL2400_Reset(void)
{
#ifdef USE_XL2400P
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0xEA);
    LL_mDelay(1);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0xEE);
    LL_mDelay(1);
#endif
}

static int XL2400_RxCalibrate(void)
{
    uint8_t i, j;
    for (i = 0; i < 10; i++) {
        LL_mDelay(2);
        XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_ANALOG_CFG3, cbuf, 2);
        cbuf[1] |= 0x90;
        cbuf[1] &= ~0x20;
        XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG3, cbuf, 2);
        cbuf[1] |= 0x40;
        XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG3, cbuf, 2);
        LL_mDelay(1);
        XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_FIFO_STATUS, cbuf, 2);
        if (cbuf[1] & 0x20) {
            j = cbuf[1] << 3;
            XL2400_ReadToBuf(XL2400_CMD_R_REGISTER | XL2400_REG_ANALOG_CFG3, cbuf, 2);
            cbuf[1] &= 0x8F;
            cbuf[1] |= 0x20;
            cbuf[0] &= 0x07;
            cbuf[0] |= j;
            XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_ANALOG_CFG3, cbuf, 2);
            return 0;
        }
    }
    return -1;
}

void XL2400_SetTxMode(void)
{
#ifdef USE_XL2400P
    cbuf[0] = 0xee;
    cbuf[1] = 0x80;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, cbuf, 2);
    XL2400_ClearStatus();
#else
    XL2400_CE_Low();
    XL2400_ClearStatus();
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x7E);
    XL2400_RxCalibrate();
#endif
    LL_mDelay(1);
}

void XL2400_SetRxMode(void)
{
#ifdef USE_XL2400P
    cbuf[0] = 0xee;
    cbuf[1] = 0xc0;
    XL2400_WriteFromBuf(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, cbuf, 2);
    XL2400_ClearStatus();
#else
    XL2400_CE_Low();
    XL2400_ClearStatus();
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_CFG_TOP, 0x7F);
#endif
    XL2400_CE_High();
    LL_mDelay(1);
}

uint8_t XL2400_Tx(const uint8_t *ucPayload, uint8_t length)
{
    uint8_t y = 16;
    uint8_t status = 0;
    XL2400_ClearStatus();
    XL2400_WriteFromBuf(XL2400_CMD_W_TX_PAYLOAD, ucPayload, length);
    XL2400_CE_High();
    while (y--) {
        status = XL2400_ReadStatus();
        if ((status & (XL2400_FLAG_MAX_RT | XL2400_FLAG_TX_DS)) != 0)
            break;
        LL_mDelay(1);
    }
    return status;
}

uint8_t XL2400_TxNoAck(const uint8_t *ucPayload, uint8_t length)
{
    XL2400_ClearStatus();
    XL2400_NSS_Low();
    SPI_TxRxByte(XL2400_CMD_W_TX_PAYLOAD_NOACK);
    while (length--)
        SPI_TxRxByte(*ucPayload++);
    XL2400_NSS_High();
    XL2400_CE_High();  /* 触发发送 */
    XL2400_DelayMs(1);
    return XL2400_ReadStatus();
}

uint8_t XL2400_Rx(void)
{
    uint8_t status;
    uint8_t rxplWidth;
    status = XL2400_ReadStatus();
    if (status & XL2400_FLAG_RX_DR) {
        XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_STATUS, status);
        rxplWidth = XL2400_ReadReg(XL2400_CMD_R_RX_PL_WID);
        if (rxplWidth > XL2400_PL_WIDTH_MAX)
            rxplWidth = XL2400_PL_WIDTH_MAX;
        xl2400_rx_len = rxplWidth;
        XL2400_ReadToBuf(XL2400_CMD_R_RX_PAYLOAD, xl2400_rx_buf, rxplWidth);
    }
    return status;
}

uint8_t XL2400_RxGetLength(void)
{
    return xl2400_rx_len;
}

uint8_t XL2400_ReadStatus(void)
{
    xl2400_state = XL2400_ReadReg(XL2400_CMD_R_REGISTER | XL2400_REG_STATUS);
    return xl2400_state;
}

void XL2400_ClearStatus(void)
{
    XL2400_WriteReg(XL2400_CMD_FLUSH_TX, XL2400_CMD_NOP);
    XL2400_WriteReg(XL2400_CMD_FLUSH_RX, XL2400_CMD_NOP);
    XL2400_WriteReg(XL2400_CMD_W_REGISTER | XL2400_REG_STATUS, 0x70);
}

void XL2400_FlushRxTX(void)
{
    XL2400_WriteReg(XL2400_CMD_FLUSH_TX, XL2400_CMD_NOP);
    XL2400_WriteReg(XL2400_CMD_FLUSH_RX, XL2400_CMD_NOP);
}
