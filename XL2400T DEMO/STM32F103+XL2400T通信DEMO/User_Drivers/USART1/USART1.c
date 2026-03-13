#include "USART1.h"
#include "string.h"
//USRTA1-->TX PA9
//USRTA1-->RX PA10

UART_HandleTypeDef USART1_Handle;//USART1句柄
uint8_t G_USART1_RecBuffer[USART1_Rec_SIZE];//接收数据缓冲区
uint8_t G_USART1_Rec_Count=0;               //接收计数
uint8_t G_USART1_RecFlag=0; 
//USART1 GPIO初始化
void USART1_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();  //开启GPIOA模块时钟

    /*配置GPIO参数*/
    GPIO_InitTypeDef USART1_IO;

    USART1_IO.Pin       = GPIO_PIN_9;     
    USART1_IO.Mode      = GPIO_MODE_AF_PP;           //推免复用
    USART1_IO.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOA,&USART1_IO);//根据GPIO结构体初始化引脚

    USART1_IO.Pin       = GPIO_PIN_10;     
    USART1_IO.Mode      = GPIO_MODE_AF_INPUT;           //推免复用
    USART1_IO.Pull      = GPIO_PULLUP;               //上拉模式

    HAL_GPIO_Init(GPIOA,&USART1_IO);//根据GPIO结构体初始化引脚
}


//USART1初始化
void USART1_Init(void)
{
	USART1_GPIO_Init();
  //USART1_GPIO_Init();
  /*使能时钟*/
  __HAL_RCC_USART1_CLK_ENABLE(); //开启USART1模块时钟

  /*配置USART1参数*/
  USART1_Handle.Instance        = USART1;              //串口基地址为USART1
  USART1_Handle.Init.BaudRate   = 115200;              //波特率设置为115200
  USART1_Handle.Init.WordLength = UART_WORDLENGTH_8B;  //数据位
  USART1_Handle.Init.StopBits   = UART_STOPBITS_1;     //停止位
  USART1_Handle.Init.Parity     = UART_PARITY_NONE;    //无校验
  USART1_Handle.Init.Mode       = UART_MODE_RX|UART_MODE_TX ;     //工作模式选择发送与接收
  USART1_Handle.Init.HwFlowCtl  = UART_HWCONTROL_NONE; //无硬件控制流
  
  HAL_UART_Init(&USART1_Handle);//根据结构体配置初始化USART1

  /********************************************************************/
  //由于HAL的机制 比较难用也难理解 所以这里采用操作寄存器的方式
  USART1->CR1 |= (1<<2);//使能 接收
  USART1->CR1 |= (1<<3);//使能 发送
  USART1->CR1 |= (1<<4);//使能 空闲中断
  USART1->CR1 |= (1<<5);//使能 接收中断
	USART1->CR1 |= USART_CR1_UE; // 外设总使能

  /* 使能NVIC */
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
	
  
}

void USART1_Send_Byte(uint8_t dat)
{
    USART1->DR = dat;//将数据写入DR寄存器
    while( (USART1->SR&(1<<6)) ==0);//等待上一个数据发送完成
}

/*********************************
 * 重定向Printf
*********************************/
int fputc(int ch, FILE *f)
{
  USART1_Send_Byte(ch);
  return ch;
}


/******************************
 * 串口中断服务函数
 * 使用中断接收数据
******************************/
void USART1_IRQHandler(void)
{
    if( (USART1->SR & USART_SR_RXNE) != 0)//产生接收中断 
    {
        G_USART1_RecBuffer[G_USART1_Rec_Count]= (uint8_t)USART1->DR;//将数据寄存器的值转移
        G_USART1_Rec_Count++;
        /*清除中断标志位*/
//		USART1->SR &= ~(1<<5);
    }
    if( (USART1->SR & USART_SR_IDLE) != 0)//产生空闲中断  
    {
        G_USART1_RecFlag=1;//接收完成标志置1
        /*清除中断标志位*/
        USART1->SR;
        USART1->DR;
    }

}


/***********************************
 * 串口测试程序
 * 将接收到的数据 发送给上位机
***********************************/
void User_USART1_Test(void)
{
    if(G_USART1_RecFlag)//产生空闲中断
    {
        uint16_t Len = strlen((char *)G_USART1_RecBuffer);//计算出接收到的数据长度

        for(uint8_t i=0; i<Len ; i++)
        {
            printf("%c" , G_USART1_RecBuffer[i]);//将接收到的数据打印出来
        }
        printf("\r\n");//回车换行以便区分
        
        memset(G_USART1_RecBuffer , 0 , Len);//清空接收缓冲区
        G_USART1_RecFlag=0;//接收完成标志归0
        G_USART1_Rec_Count=0;//接收计数归0
    }
}
