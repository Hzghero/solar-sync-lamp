#include "main.h"
#include "System_Clock.h"
#include "USART1.h"
#include "XL2400T.h"


uint8_t RF_TX_Test[8]={1,2,3,4,5,6,7,8};//测试数据
uint8_t RF_RX_Tset[RF_PACKET_SIZE]={0};

int main(void)
{
    HAL_Init(); //HAL库初始化
	
    System_Clock_Init();	//配置系统时钟到72Mhz 8x9 
    USART1_Init();        //串口1初始化
    printf("hello\r\n");  //串口打印测试
	/******************** RF ************************/
	XL2400T_SPI_Init(); //XL2400P 软件模拟SPI初始化
	XL2400T_Init();     //XL2400P 参数初始化
	
#if RF_Mode == 1
RF_Tx_Mode(); 	
RF_Set_Chn(76);//配置通讯频段
  HAL_Delay(200);
  while(1)
  {
	RF_TX_Data(RF_TX_Test);//发送数据
	HAL_Delay(200);
  }


#else
	
    RF_Rx_Mode();    //设置为接收模式
	RF_Set_Chn(76-1);//配置频段
	while(1)
	{
		if(RF_RX_Data(RF_RX_Tset)==1)
		{
			printf("2.4G Received: ");
			for(uint8_t i=0; i<RF_PACKET_SIZE; i++)
			{
				printf("%d",RF_RX_Tset[i]);//将接收到的数据打印
			}
			printf("\r\n");//回车换行
		}

	}


#endif
}


/************************************************************
XL2400T 连接
CSN PA1
SCK PA2
DATA PA3

串口接线

USART1_TX PA9
USART1_RX PA10


使用AC5编译器！！！！！！！！！
****************************************************************/


