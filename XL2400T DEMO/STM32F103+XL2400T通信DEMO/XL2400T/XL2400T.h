#ifndef __XL2400T_H
#define __XL2400T_H
//-------------------------------------------
#include "main.h"
//-------------------------------------------

//XL2400T 连接
//CSN PA4
//SCK PA5
//DATA PA6
#define 	RF_SPI_DATA_PORT        GPIOA       // DATA引脚端口
#define		RF_SPI_DATA_PIN			GPIO_PIN_6	// DATA---PA3

#define 	RF_SPI_SCK_PORT         GPIOA       // SCK引脚端口
#define		RF_SPI_SCK_PIN			GPIO_PIN_5	// SCK---PA2

#define		RF_SPI_CSN_PORT         GPIOA       // CSN引脚端口
#define		RF_SPI_CSN_PIN			GPIO_PIN_4	// CSN---PA1


#define	 	RF_SPI_DATA_CLK_ENABLE  	__HAL_RCC_GPIOA_CLK_ENABLE()  // DATA端口时钟
#define 	RF_SPI_SCK_CLK_ENABLE   	__HAL_RCC_GPIOB_CLK_ENABLE()  // SCK端口时钟
#define 	RF_SPI_CSN_CLK_ENABLE   	__HAL_RCC_GPIOB_CLK_ENABLE()  // CSN端口时钟


#define CSN_Low()		            HAL_GPIO_WritePin(RF_SPI_CSN_PORT, RF_SPI_CSN_PIN, GPIO_PIN_RESET);
#define CSN_High()		            HAL_GPIO_WritePin(RF_SPI_CSN_PORT, RF_SPI_CSN_PIN, GPIO_PIN_SET);

#define SCK_High()                  HAL_GPIO_WritePin(RF_SPI_SCK_PORT, RF_SPI_SCK_PIN, GPIO_PIN_SET);
#define SCK_Low()                   HAL_GPIO_WritePin(RF_SPI_SCK_PORT, RF_SPI_SCK_PIN, GPIO_PIN_RESET);

#define SPI_DATA_High()             HAL_GPIO_WritePin(RF_SPI_DATA_PORT, RF_SPI_DATA_PIN, GPIO_PIN_SET);
#define SPI_DATA_Low()              HAL_GPIO_WritePin(RF_SPI_DATA_PORT, RF_SPI_DATA_PIN, GPIO_PIN_RESET);

#define Read_DATA()                 HAL_GPIO_ReadPin(RF_SPI_DATA_PORT,RF_SPI_DATA_PIN)



/*********************************************************************/
/***************************** SPI函数声明*****************************/
/*********************************************************************/

void XL2400T_SPI_Init(void);
unsigned char RF_SPI_Read_Reg(unsigned char RF_Reg);
void RF_SPI_Write_Reg(unsigned char RF_Reg,unsigned char W_Data);
void RF_Read_Buff(unsigned char RF_Reg , unsigned char *pBuff , unsigned char Len);
void RF_Write_Buff(unsigned char RF_Reg , unsigned char *pBuff , unsigned char Len);


/*********************************************************************/
/************************* 寄存器地址定义 *****************************/
/*********************************************************************/

#define CFG_TOP				(0X00)
#define	EN_AA				(0X01)
#define	EN_RXADDR			(0X02)
#define	SETUP_AW			(0X03)
#define	SETUP_RETR			(0X04)
#define	RF_CH				(0X05)
#define	RF_SETUP			(0X06)
#define	RF_STATUS			(0X07)
#define	OBSERVE_TX			(0X08)
#define	RSSI				(0X09)
#define	RX_ADDR_P0			(0X0A)
#define	RX_ADDR_P1			(0X0B)
#define	RX_ADDR_P2			(0X2)
#define	RX_ADDR_P3			(0X3)
#define	RX_ADDR_P4			(0X4)
#define	RX_ADDR_P5			(0X5)
#define	RX_ADDR_P2TOP5		(0X0C)
#define	BER_RESULT			(0X0D)
#define	AGC_SETTING			(0X0E)
#define	PGA_SETTING			(0X0F)
#define	TX_ADDR				(0X10)
#define	RX_PW_PX			(0X11)
#define	ANALOG_CFG0			(0X12)
#define	ANALOG_CFG1			(0X13)
#define	ANALOG_CFG2			(0X14)
#define	ANALOG_CFG3			(0X15)
#define	STATUS_FIFO			(0X17)
#define	RSSIREC				(0X18)
#define	TXPROC_CFG			(0X19)
#define	RXPROC_CFG			(0X1A)	
#define	DYNPD				(0X1C)
#define	FEATURE				(0X1D)
#define	RAMP_CFG			(0X1E)

/*********************************************************************/
/*************************** 操作指令定义 *****************************/
/*********************************************************************/

#define	R_REGISTER		    0x00 //读寄存器指令                        				
#define	W_REGISTER		    0x20 //写寄存器指令
#define R_RX_PLOAD 			0x61 //读接收数据指令
#define W_TX_PLOAD			0xA0 //写发射数据指令
#define FLUSH_TX			0xE1 //清空TX-FIFO指令   
#define FLUSH_RX			0xE2 //清空RX-FIFO指令 
#define R_RX_PL_WID			0x60 //读RX-FIFO数据长度指令
#define W_ACK_PLOAD			0xA8
#define W_TX_PLOAD_NOACK	0xB0
#define CMD_NOP				0xFF //空操作

/*********************************************************************/
/********************* 状态寄存器标记位定义 ****************************/
/*********************************************************************/
#define RX_DR    		(0x40) //接收到数据中断标志位
#define TX_DS    		(0x20) //发送数据完成中断标志位
#define MAX_RT   		(0x10) //达到最大发送次数中断标志位

/*********************************************************************/
/*************************** 通讯速率令定义****************************/
/*********************************************************************/

#define C_DR_1M                 0x02						//1Mpbs					
#define C_DR_250K               0x22					   //250Kpbs

/***********************************************************************************************************/
/******************************************** 发射功率定义 **************************************************/
/***********************************************************************************************************/

/*******************
1M速率只能使用0dBm
250K可以使用全部的功率
*******************/

//XL2400T	发射功率
#define C_RF13dBm 		36 				// 13dbm
#define C_RF12dBm 		30 				// 12dbm
#define C_RF11dBm 		24 				// 11dbm
#define C_RF10dBm 		19				// 10dbm
#define C_RF9dBm 		16 				// 9dbm
#define C_RF8dBm 		14 				// 8dbm
#define C_RF7dBm 		12 				// 7dbm
#define C_RF5dBm 		9 				// 5dbm
#define C_RF3dBm 		8 				// 3dBm
#define C_RF0dBm 		6 				// 0dBm
#define C_RF_4dBm 		4 				// -4dBm
#define C_RF_9dBm 		2 				// -9dBm
#define C_RF_10dBm 		1 				// -10dBm




void RF_Reset(void);
void RF_Sleep(void);
void RF_Rx_Mode(void);
void RF_Tx_Mode(void);
void RF_Set_Chn(unsigned char Chn);
void RF_Set_Power(unsigned char Power);
void RF_Refresh_State(void);
void XL2400T_Init(void);
unsigned char RF_TX_Data(unsigned char* tx_buff);
unsigned char RF_RX_Data(unsigned char* rx_buff);

/***********************************************************************************************************/
/******************************************* 用户自定义参数 *************************************************/
/***********************************************************************************************************/

#define RF_TX_Power             C_RF7dBm    //功率配置
#define RF_BAUD                 C_DR_250K   //速率配置
#define RF_PACKET_SIZE			8           //包长配置中

#define RF_Mode 1                           //模式配置 只对main函数有效





#endif

