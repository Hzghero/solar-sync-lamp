/**
 * XL2400 初步通讯验证 - TX/RX 环回测试
 * 将本文件逻辑合并到 STM32Cube 工程的 main.c
 * 编译时定义 XL2400_APP_MODE: 0=RX, 1=TX
 */
#if 0  /* 复制到 main.c 并取消此注释块 */

#include "xl2400.h"

#define XL2400_APP_MODE  0  /* 0=RX, 1=TX */

/* 同步协议用地址 "XL240" */
static const uint8_t SYNC_ADDR[5] = {0x58, 0x4C, 0x32, 0x34, 0x30};
static uint8_t tx_buf[8] = {0xAA, 0x55, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};

void XL2400_App_Init(void)
{
    while (XL2400_SPI_Test() != XL2400_OK)
        HAL_Delay(1000);

    XL2400_Init();
    XL2400_SetPower(XL2400_RF_0DB);
    XL2400_SetTxAddress(SYNC_ADDR);
    XL2400_SetRxAddress(SYNC_ADDR);

#if XL2400_APP_MODE == 0
    XL2400_SetChannel(0);   /* RX 用 channel 0 (比 TX 低 1MHz) */
    XL2400_SetRxMode();
#else
    XL2400_SetChannel(1);   /* TX 用 channel 1 */
    XL2400_SetTxMode();
#endif
}

void XL2400_App_Poll(void)
{
#if XL2400_APP_MODE == 0
    if (XL2400_Rx() & XL2400_FLAG_RX_DR) {
        uint8_t len = XL2400_RxGetLength();
        /* 通过串口打印 xl2400_rx_buf[0..len-1] */
        (void)len;
    }
#else
    tx_buf[2]++;
    XL2400_TxNoAck(tx_buf, 8);
    HAL_Delay(1000);
#endif
}

/* 在 main() 中调用：
 * XL2400_App_Init();
 * while(1) { XL2400_App_Poll(); ... }
 */

#endif
