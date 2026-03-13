#include "XL2400T.h"

/********************************
 * XL2400T 软件模拟SPI GPIO初始化
********************************/


static void RF_SPI_GPIO_Init(void)
{
	/*使能模块时钟*/
	RF_SPI_DATA_CLK_ENABLE;
	RF_SPI_SCK_CLK_ENABLE;
	RF_SPI_CSN_CLK_ENABLE;


	/************** DATA *******************/
	GPIO_InitTypeDef   GPIO_InitStruct;

	GPIO_InitStruct.Pin   = RF_SPI_DATA_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	
	HAL_GPIO_Init(RF_SPI_DATA_PORT, &GPIO_InitStruct);

	/**************** SCK ********************/
	GPIO_InitStruct.Pin   = RF_SPI_SCK_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	
	HAL_GPIO_Init(RF_SPI_SCK_PORT, &GPIO_InitStruct);

	/**************** CSN ********************/
    GPIO_InitStruct.Pin   = RF_SPI_CSN_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	
	HAL_GPIO_Init(RF_SPI_CSN_PORT, &GPIO_InitStruct);
}

/***********************************
 * XL2400T DATA引脚输出模式
***********************************/
static void RF_SPI_Data_OutPut(void)
{
	RF_SPI_DATA_CLK_ENABLE;//PA

	GPIO_InitTypeDef   GPIO_InitStruct;

	GPIO_InitStruct.Pin   = RF_SPI_DATA_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	
	HAL_GPIO_Init(RF_SPI_DATA_PORT, &GPIO_InitStruct);
}

/***********************************
 * XL2400T DATA引脚输入模式
***********************************/
static void RF_SPI_Data_InPut(void)
{
	RF_SPI_DATA_CLK_ENABLE;//PA
	
	GPIO_InitTypeDef   GPIO_InitStruct;

	GPIO_InitStruct.Pin   = RF_SPI_DATA_PIN;
	GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	
	HAL_GPIO_Init(RF_SPI_DATA_PORT, &GPIO_InitStruct);
}

/*************************
 * XL2400T SPI 通讯初始化
*************************/
void XL2400T_SPI_Init(void)
{
    RF_SPI_GPIO_Init();
	CSN_High();
	SCK_Low();
}

/**********************************************************************************************************************/
/*********************************************************延时函数******************************************************/
/**********************************************************************************************************************/
//不一定要使用这里的 可以使用其他的

//微秒延时函数
void RF_DelayUs(unsigned char Time)
{
	unsigned char a;
	for(a=0;a<Time;a++)
	{
	}
	Time>>=1;
	for(a=0;a<Time;a++)
	{
	}
	Time>>=2;
	for(a=0;a<Time;a++)
	{
	}
} 

//毫秒延时函数
void RF_DelayMs(unsigned char Time)
{
	unsigned char a,b;
	for(a=0;a<Time;a++)
	{	
		for(b=0;b<5;b++)
		{
		 	RF_DelayUs(195); 	
		}
	}
}

/****************************************************************************************************************************/
/***************************************** 软件模拟SPI ***********************************************************************/
/****************************************************************************************************************************/

/**********************
 * SPI写入一个字节的数据
***********************/
static void RF_SPI_Write_Byte(unsigned char buff)
{
	unsigned char i = 0;

	RF_SPI_Data_OutPut();//数据脚切换为输出

    for(i = 0; i < 8; i++)
	{
        SCK_Low();

        if(buff & 0x80)
		{
            SPI_DATA_High();
		}
        else
		{
            SPI_DATA_Low();
		}

        buff = buff << 1;

        SCK_High();
    }
    SPI_DATA_High();
    SCK_Low();
}

/**********************
 * SPI读取一个字节的数据
 * 返回值: 读取到的数据
***********************/
static unsigned char RF_SPI_Read_Byte(void)
{
	RF_SPI_Data_InPut();//数据脚切换为输入

    unsigned char  buff = 0;
	unsigned char  i = 0;

    for(i = 0; i < 8; i++)
    {
        SCK_Low();
        buff = buff << 1;
        SCK_High();
        if(Read_DATA())
		{
			buff |= 0x01;
		}         
    }
    SCK_Low();

    return buff;
}

/********************************************************************
 * 向XL2400T的寄存器写入一个字节的数据
 * 参数：RF_Reg 写指令|寄存器地址
 * 参数：W_Data 写入的数据
*********************************************************************/
void RF_SPI_Write_Reg(unsigned char RF_Reg,unsigned char W_Data)
{
	CSN_Low();//拉低片选
	RF_SPI_Write_Byte(RF_Reg);//写入地址
	RF_SPI_Write_Byte(W_Data);//写入数据
	CSN_High();//写入完成拉高片选
}

/*******************************************************
 * 从XL2400T寄存器读取一个字节的数据
 * 参数: 读指令|寄存器地址
 * 返回值：读取到的数据
********************************************************/
unsigned char RF_SPI_Read_Reg(unsigned char RF_Reg)
{
	unsigned char rTemp =0;
	CSN_Low();
	RF_SPI_Write_Byte(RF_Reg);//写入地址+读取指令
	rTemp = RF_SPI_Read_Byte();//读取数据
	CSN_High();
	return rTemp;
}

/*****************************************************************************************
 * 向XL2400T的寄存器写入多个数据
 * 参数: RFAdress 写取指令|寄存器地址
 * 参数：*pBuff   需要写入数据的地址
 * 参数：Len      需要写入的长度
******************************************************************************************/
void RF_Write_Buff(unsigned char RF_Reg , unsigned char *pBuff , unsigned char Len)
{
	unsigned char rTemp = 0;
	unsigned char i = 0;
	CSN_Low();
	RF_SPI_Write_Byte(RF_Reg);
	for(i = 0 ; i < Len ; i++)
	{
		rTemp = pBuff[i];
        RF_SPI_Write_Byte(rTemp);
	}
	CSN_High();
}

/*****************************************************************************************
 * 从XL2400T的寄存器读取多个数据
 * 参数: RFAdress 读取指令|寄存器地址
 * 参数：*pBuff   读取到的数据存放的地址
 * 参数：Len      需要读取的长度
******************************************************************************************/
void RF_Read_Buff(unsigned char RF_Reg , unsigned char *pBuff , unsigned char Len)
{
	unsigned char i=0;
	CSN_Low();
	RF_SPI_Write_Byte(RF_Reg);
	for(i = 0 ; i < Len ; i++)
	{
		pBuff[i] = RF_SPI_Read_Byte();
	}
	CSN_High();
}


/****************************************************************************************************************************/
/*********************************************** RF相关配置函数 **************************************************************/
/****************************************************************************************************************************/

/***********************
 * 拉高软件CE
************************/
void RF_CE_High(void)
{
	RF_SPI_Write_Reg(W_REGISTER+CFG_TOP, 0xEF);
}

/**********************
 * 拉低软件CE
**********************/
void RF_CE_Low(void)
{
	RF_SPI_Write_Reg(W_REGISTER+CFG_TOP, 0xEE);
}

/***********************
 * 清除中断标记位
 * 清空TX-FIFO
 * 清空RX-FIFO
************************/
void RF_Refresh_State(void)
{
	RF_SPI_Write_Reg(W_REGISTER+RF_STATUS, 0x70);
	RF_SPI_Write_Reg(FLUSH_TX, CMD_NOP);
	RF_SPI_Write_Reg(FLUSH_RX, CMD_NOP);
}

/**********************
 * 基带复位
**********************/
void RF_Reset(void)
{
   RF_SPI_Write_Reg(W_REGISTER+CFG_TOP,0xEA);
   RF_DelayUs(200);
   RF_SPI_Write_Reg(W_REGISTER+CFG_TOP,0xEE);
   RF_DelayMs(1);
}

/**********************************
 * 设置通讯频点
 * 参数：频点
**********************************/
void RF_Set_Chn(unsigned char Chn)
{
	/*解锁*/
   unsigned temp=0;
   temp = RF_SPI_Read_Reg(R_REGISTER + EN_AA);
   temp &= ~(1<<6);
	RF_SPI_Write_Reg(W_REGISTER + EN_AA, temp);
    
	/*写入频段*/
	RF_SPI_Write_Reg(W_REGISTER + RF_CH, Chn + 0x60);
   
   /*锁定*/
   temp=0;
   temp = RF_SPI_Read_Reg(0x00|0x01);
   temp |= (1<<6);
	RF_SPI_Write_Reg(W_REGISTER + EN_AA, temp);							
}

/********************************************
 * 配置发送与接收地址
********************************************/
void RF_Set_Address(unsigned char *AddrBuff)
{
	RF_Write_Buff(W_REGISTER+TX_ADDR,AddrBuff , 5);/*配置发送地址*/
	RF_Write_Buff(W_REGISTER+RX_ADDR_P0, AddrBuff ,5);/*配置接收数据通道0的地址*/
	/*如果还需要使用其他的数据接收通道 还需要在对应的寄存器写入地址*/
}

/***************************************
 * 配置发射功率
****************************************/
void RF_Set_Power(unsigned char Power)
{
	unsigned char Power_Buff[3]={0};
	RF_Read_Buff(R_REGISTER+RF_SETUP , Power_Buff, 2);
	Power_Buff[1] = Power;
	RF_Write_Buff(W_REGISTER+RF_SETUP , Power_Buff ,2);
}

/**************************************
 * RF休眠
 * 此功能仅用于低功耗模式
 * 唤醒需要重新初始化
***************************************/
void RF_Sleep(void)
{
	RF_SPI_Write_Reg(W_REGISTER+CFG_TOP, 0x00);
}

/*************************
 * 配置RF为发送模式
*************************/
void RF_Tx_Mode(void)
{
   unsigned char Mode_Buff[3]={0};
   Mode_Buff[0] = 0xee;//启用CRC校验 2字节
   Mode_Buff[1] = 0x80; 
   RF_Write_Buff(W_REGISTER+CFG_TOP, Mode_Buff ,2);
   RF_Refresh_State();
   RF_DelayMs(1);
}

/************************
 * 配置RF为接收模式
************************/
void RF_Rx_Mode(void)
{
   unsigned char Mode_Buff[3]={0};
   Mode_Buff[0] = 0xee;//启用CRC校验 2字节
   Mode_Buff[1] = 0xc0; 
   RF_Write_Buff(W_REGISTER+CFG_TOP , Mode_Buff ,2);
   RF_Refresh_State();
   RF_CE_High();
   RF_DelayMs(1);
}


/****************************************************************************************************************************/
/************************************************* RF参数初始化 **************************************************************/
/****************************************************************************************************************************/

unsigned char RF_Test_Adrress[5]={0xcc,0xcc,0xcc,0xcc,0xcc};//RF地址

/**********************
 * RF初始化
**********************/
void XL2400T_Init(void)
{
	
/////////////////////////////////////// 公共参数 ////////////////////////////////////////////
	RF_SPI_Write_Reg(W_REGISTER+CFG_TOP,0x02);
	RF_DelayMs(2);
    RF_SPI_Write_Reg(W_REGISTER+CFG_TOP,0x3E);
	RF_DelayMs(2);

	
	 /* 配置模拟寄存器 XL2400T	*/
	unsigned char gRfBuffer[8]={0};
	gRfBuffer[5] = ((gRfBuffer[5] & 0xff) | 0x6d);
	RF_Read_Buff(PGA_SETTING , gRfBuffer , 5);
	//RF_Read_Buff(PGA_SETTING , gRfBuffer , 5);
	RF_DelayMs(1);
	gRfBuffer[0] = 0x44;
	gRfBuffer[1] = 0x3E;
	gRfBuffer[2] = 0x38;
	gRfBuffer[3] = 0x32;
	gRfBuffer[4] = 0x2A;
	RF_Write_Buff(W_REGISTER + PGA_SETTING , gRfBuffer , 5);
	RF_DelayMs(1);

    /* 配置地址长度 */
	RF_SPI_Write_Reg(W_REGISTER+SETUP_AW,  0xAF);//地址长度5字节
	/* 配置RF地址 */
	RF_Set_Address(RF_Test_Adrress);
    /* 配置通讯速率 */
	RF_SPI_Write_Reg(W_REGISTER+RF_SETUP, C_DR_250K);
/////////////////////////////////////// 接收参数 ////////////////////////////////////////////
    /*使能接收数据通道*/
    RF_SPI_Write_Reg(W_REGISTER + EN_RXADDR, 0x01);/*启用数据通道0*/
    /*配置数据通道0接收包长度*/
	RF_SPI_Write_Reg(W_REGISTER+RX_PW_PX,RF_PACKET_SIZE);
    /* 配置应答数据通道道 */
	RF_SPI_Write_Reg(W_REGISTER+EN_AA,0x00);/*关闭所有数据通道的应答*/
    /*配置动态数据长度&&长数据包功能*/
    RF_SPI_Write_Reg(W_REGISTER+DYNPD, 0x00);//关闭所有数据通道的动态长度功能
	RF_SPI_Write_Reg(W_REGISTER+FEATURE, 0x18);//不使能动态长度功能
/////////////////////////////////////// 发送参数 ////////////////////////////////////////////
    /* 配置重传次数和时间间隔 */
	RF_SPI_Write_Reg(W_REGISTER + SETUP_RETR,0x33);//重传3次 重传间隔1ms
    /* 配置发射功率 */
    RF_Set_Power(RF_TX_Power);
}


/****************************************************************************************************************************/
/************************************************* 通讯测试 ******************************************************************/
/****************************************************************************************************************************/


/*****************************************************
 * 发送数据函数
 * 参数：需要发送的数据的地址指针
*****************************************************/
unsigned char RF_TX_Data(unsigned char* tx_buff)
{
    RF_Refresh_State();//刷新状态

	RF_Write_Buff(W_TX_PLOAD,tx_buff,RF_PACKET_SIZE);//填写发送内容

	RF_CE_High();//拉高CE
	RF_DelayUs(100);//延时100微妙
	RF_CE_Low();//拉低CE
    RF_DelayMs(100);//延时
	  
	/*获取状态寄存器的状态*/
	if(RF_SPI_Read_Reg(R_REGISTER+RF_STATUS)&TX_DS)//触发发送中断
	{
		printf("Send OK!\r\n");
		RF_Refresh_State();//清空FIFO 清除中断标记位
		return 0x20;
	}
	else
	{
		printf("Send Error!\r\n");
		RF_Refresh_State();//清空FIFO 清除中断标记位
		return 0;	
	}
}

/*************************************************
 * 接收数据函数
 * 参数：接收到的数据 存放的地址
 * 返回值：接收到数据返回1 没接收到数据返回0
**************************************************/
unsigned char RF_RX_Data(unsigned char* rx_buff)
{
    unsigned char RfPlwid=0;
	if(RF_SPI_Read_Reg(RF_STATUS)&RX_DR) //触发接收中断
	{
		RF_CE_Low();//拉高CE
		RfPlwid = RF_SPI_Read_Reg(R_RX_PL_WID);   //读取数据长度
		RF_Read_Buff(R_RX_PLOAD,rx_buff,RfPlwid); //读取有效数据
		RF_Refresh_State();                       //清空FIFO 清除中断标记位
		RF_CE_High();
		return 1;
	}
	else
	{
		return 0;
	}
}












