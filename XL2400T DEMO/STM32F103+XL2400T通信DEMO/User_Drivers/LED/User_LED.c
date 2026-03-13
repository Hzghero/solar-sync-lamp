#include "User_LED.h"


void LED_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef LED_Init;

    LED_Init.Pin  = GPIO_PIN_12;
    LED_Init.Mode  = GPIO_MODE_OUTPUT_PP;
    LED_Init.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOB,&LED_Init);

    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_SET);
    
}


