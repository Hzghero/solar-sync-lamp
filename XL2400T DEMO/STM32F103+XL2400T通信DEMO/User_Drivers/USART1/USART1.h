#ifndef _USART1_H_
#define _USRTA1_H_
//-------------------------------
#include "main.h"
//-------------------------------
void USART1_Init(void);
void USART1_Send_Byte(uint8_t dat);

void User_USART1_Test(void);

//-------------------------------------------------------------------------------------
#define USART1_Rec_SIZE 16 //接收缓冲区的大小
//-------------------------------------------------------------------------------------
extern uint8_t G_UART1_RecBuffer[USART1_Rec_SIZE];//接收数据缓冲区
extern uint8_t G_UART1_RecFlag;//空闲中断标志 也可以理解为接收完成标志

#endif



