/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "XL2400T.h"
#include "rf_xl2400.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* v1.8.1: 测试版 - 取消传输延迟补偿，观察原始同步效果 */
#define SYNC_CYCLE_MS       900U    /* 同步周期 900ms */
#define SYNC_LED_ON_MS      100U    /* LED 亮灯时间 100ms */
#define SYNC_TX_TIME_MS     450U    /* TX 发送时间：周期中点，所有节点相同 */
#define SYNC_TX_DELAY_MS    6U      /* 传输延迟补偿：基于测试结果(11ms/2≈6ms) */
#define SYNC_PKT_SIZE       4U      /* 精简包：AA 55 + 2字节相位 */

/* 日/夜检测 (规格书 §5)：太阳能板电压 PA0/ADC1_IN0，滞回避免抖动 */
#define SOLAR_ADC_NIGHT_THRESHOLD   360U   /* raw < 此值判为夜间，开灯 (0.29V @ 3.3Vref) */
#define SOLAR_ADC_DAY_THRESHOLD    496U   /* raw > 此值判为白天，关灯 (0.4V @ 3.3Vref) */
#define DAYNIGHT_SAMPLE_INTERVAL_MS 500U  /* 日/夜 ADC 采样间隔 */
#define DAYNIGHT_HOLD_MS            3000U /* 翻转前需持续满足条件的时间，避免误触发 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

IWDG_HandleTypeDef hiwdg;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* XL2400T TX/RX buffers (8-byte payload) */
static uint8_t RF_TX_Buf[RF_PACKET_SIZE]   = {0};
static uint8_t RF_RX_Buf[RF_PACKET_SIZE]   = {0};

/* 同步闪灯本地时间：900ms 周期 (100ms ON + 800ms OFF) */
static uint32_t g_cycle = 0;          /* 周期计数 */
static uint16_t g_phase_ms = 0;       /* 周期内偏移 0..899 ms */
static uint32_t g_last_tick_ms = 0;   /* 上一次更新时间的 HAL_GetTick() 值 */
static uint32_t g_last_tx_cycle = (uint32_t)-1; /* 上一次已经广播过的周期号 */

/* v1.7.2: 单固件多节点 - 固定 TX 时间 */
static uint8_t  g_rf_mode = 0;        /* 0=RX, 1=TX (当前 RF 模式) */
static uint8_t  g_led_state = 0;      /* LED 状态，用于边沿检测 */
static uint32_t g_led_on_tick = 0;    /* LED 点亮的时间戳，用于保证最少亮 100ms */

/* 日/夜检测：1=夜间(闪灯)，0=白天(关灯)，滞回 0.29V/0.4V */
static uint8_t  g_is_night = 1;       /* 默认夜间，上电可闪 */
static uint32_t g_last_daynight_tick = 0;  /* 上次日/夜采样时刻 */
static uint32_t g_daynight_hold_until_tick = 0;  /* 翻转确认：满足条件持续到该 tick 才切换，0=未在确认中 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_IWDG_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
static void DebugPrint(const char *s);
static void DebugPrintHex(const uint8_t *buf, uint8_t len);
static void DebugPrintDec(uint16_t val);
static void SyncTime_Update(void);
static void SyncLamp_Update(void);
static void BuildSyncPacket(uint8_t *pkt);
static uint16_t ParseSyncPacket(const uint8_t *pkt);
static void Sync_AdjustFromPacket(uint16_t rx_phase_ms);
static void Sync_MainLoop(void);
static void DayNight_Update(void);  /* 读太阳能板 ADC，滞回更新 g_is_night */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_IWDG_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  DebugPrint("\r\n[FW] " FW_VERSION "\r\n");

  /* v2.0.0: 启用升压电路供电 (BOOST_EN = HIGH) */
  HAL_Delay(100);  /* 等待时钟稳定 */
  HAL_GPIO_WritePin(BOOST_EN_GPIO_Port, BOOST_EN_Pin, GPIO_PIN_SET);
  DebugPrint("BOOST_EN: HIGH\r\n");

  /* XL2400T 3-wire SPI + RF init（通过通用接口） */
  RF_Link_Init();

  /* v1.7.2: 所有节点使用相同的固定 TX 时间，避免"假同步" */
  DebugPrint("=== SINGLE-FW MULTINODE ===\r\n");
  DebugPrint("TX time: ");
  DebugPrintDec(SYNC_TX_TIME_MS);
  DebugPrint(" ms (fixed)\r\n");
  DebugPrint("LED: PWM 124kHz 60%\r\n");

  /* 默认进入 RX 模式 (XL2400T: TX=76, RX=75) */
  RF_Link_ConfigRx(75);
  g_rf_mode = 0;

  /* 在所有耗时初始化完成后，再初始化时间基准（避免启动延迟导致相位跳变） */
  g_last_tick_ms = HAL_GetTick();
  g_cycle = 0;
  g_phase_ms = 0;
  g_led_state = 0;

  DebugPrint("IWDG: ON (1.6s timeout)\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* v2.1.0: 日/夜检测 + 单固件多节点主循环 + 看门狗喂狗 */
    DayNight_Update();
    if (g_is_night) {
      Sync_MainLoop();
    } else {
      /* 白天：关灯，不跑同步 */
      HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }

    /* 喂狗：正常运行时每个主循环都会执行到这里 */
    HAL_IWDG_Refresh(&hiwdg);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV2;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload = 1000;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 192;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 115;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_ENABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, BOOST_EN_Pin|RF_CSN_Pin|RF_SCK_Pin|RF_DATA_Pin
                          |CHG_MOS_Pin|DIV_MOS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BOOST_EN_Pin RF_CSN_Pin RF_SCK_Pin RF_DATA_Pin
                           CHG_MOS_Pin DIV_MOS_Pin */
  GPIO_InitStruct.Pin = BOOST_EN_Pin|RF_CSN_Pin|RF_SCK_Pin|RF_DATA_Pin
                          |CHG_MOS_Pin|DIV_MOS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

static void DebugPrint(const char *s)
{
  if (s == NULL) return;
  uint16_t len = 0;
  while (s[len]) len++;
  if (len > 0)
    HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 100);
}

static void DebugPrintHex(const uint8_t *buf, uint8_t len)
{
  static const char hex[] = "0123456789ABCDEF";
  char s[4];
  for (uint8_t i = 0; i < len && i < 32; i++) {
    s[0] = hex[(buf[i] >> 4) & 0x0F];
    s[1] = hex[buf[i] & 0x0F];
    s[2] = ' ';
    HAL_UART_Transmit(&huart1, (uint8_t *)s, 3, 10);
  }
}

static void DebugPrintDec(uint16_t val)
{
  char buf[6];
  int i = 5;
  buf[i] = '\0';
  if (val == 0) {
    buf[--i] = '0';
  } else {
    while (val > 0 && i > 0) {
      buf[--i] = '0' + (val % 10);
      val /= 10;
    }
  }
  DebugPrint(&buf[i]);
}

/* 更新本地 900ms 周期时间：根据 HAL_GetTick 计算经过的毫秒数 */
static void SyncTime_Update(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t delta = now - g_last_tick_ms;
  if (delta == 0) {
    return;
  }
  g_last_tick_ms = now;

  uint32_t total = g_phase_ms + delta;
  uint32_t new_cycles = total / SYNC_CYCLE_MS;
  g_cycle += new_cycles;
  g_phase_ms = (uint16_t)(total % SYNC_CYCLE_MS);

  /* 检测周期边界：如果跨越了周期起点，立即点亮 LED 并记录时间戳 */
  if (new_cycles > 0) {
    /* v2.0.0: 使用 PWM 驱动 LED 升压电路 */
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    g_led_on_tick = now;  /* 记录点亮时间戳 */
    if (g_led_state == 0) {
      DebugPrint("LED ON @0\r\n");
    }
    g_led_state = 1;
  }
}

/* 根据本地相位控制同步闪灯：使用时间戳确保 LED 至少亮 100ms */
static void SyncLamp_Update(void)
{
  uint32_t now = HAL_GetTick();
  
  if (g_led_state == 1) {
    /* LED 当前是亮的，检查是否已经亮够 100ms */
    uint32_t on_duration = now - g_led_on_tick;
    if (on_duration >= SYNC_LED_ON_MS) {
      /* 已经亮够 100ms，可以熄灭 */
      /* v2.0.0: 停止 PWM -> LED 灭 */
      HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
      DebugPrint("LED OFF@");
      DebugPrintDec((uint16_t)on_duration);
      DebugPrint("\r\n");
      g_led_state = 0;
    }
  } else {
    /* LED 当前是灭的，根据相位判断是否需要亮 */
    if (g_phase_ms < SYNC_LED_ON_MS) {
      /* v2.0.0: 启动 PWM -> LED 亮 */
      HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
      g_led_on_tick = now;
      DebugPrint("LED ON @");
      DebugPrintDec(g_phase_ms);
      DebugPrint("\r\n");
      g_led_state = 1;
    }
  }
}

/* 构造 4 字节精简同步包：AA 55 + 2字节相位（1ms 分辨率） */
static void BuildSyncPacket(uint8_t *pkt)
{
  /* 补偿传输延迟：发送时预估接收方收到时的相位 */
  uint16_t compensated_phase = g_phase_ms + SYNC_TX_DELAY_MS;
  if (compensated_phase >= SYNC_CYCLE_MS) {
    compensated_phase -= SYNC_CYCLE_MS;
  }

  pkt[0] = 0xAA;
  pkt[1] = 0x55;
  pkt[2] = (uint8_t)(compensated_phase >> 8);   /* 相位高字节，1ms 分辨率 */
  pkt[3] = (uint8_t)(compensated_phase & 0xFF); /* 相位低字节 */
}

/* 解析 4 字节精简同步包，输出毫秒相位；异常包时输出 0xFFFF */
static uint16_t ParseSyncPacket(const uint8_t *pkt)
{
  if (pkt[0] != 0xAA || pkt[1] != 0x55) {
    return 0xFFFF; /* 无效包 */
  }
  return ((uint16_t)pkt[2] << 8) | pkt[3]; /* 直接是 ms，无需乘法 */
}

/* 根据收到的同步包对本地时间做小步调整 */
static void Sync_AdjustFromPacket(uint16_t rx_phase_ms)
{
  /* 非法包直接忽略 */
  if (rx_phase_ms >= 900U) {
    return;
  }

  /* 仅比较“本周期内相位差”，忽略 cycle 绝对值 */
  int16_t delta = (int16_t)rx_phase_ms - (int16_t)g_phase_ms;  /* >0 我们落后 */

  /* 将相位差规范到 [-450, 450) 区间（半个周期），选择最短路径 */
  if (delta >= 450) {
    delta -= 900;
  } else if (delta < -450) {
    delta += 900;
  }

  /* 调试输出当前本地相位与差值，便于观察收敛情况 */
  DebugPrint(" ADJ: local_phase=");
  DebugPrintHex((uint8_t *)&g_phase_ms, 2);
  DebugPrint(" diff=");
  DebugPrintHex((uint8_t *)&delta, 2);
  DebugPrint("\r\n");

  const int16_t DEAD_BAND   = 5;      /* 小于 5ms 不调整 */
  const int16_t MAX_ADJUST  = 20;     /* 单次最大调整 20ms */

  if (delta > DEAD_BAND) {
    /* 本地相位落后，对本周期相位做前移 */
    int16_t adj = delta;
    if (adj > MAX_ADJUST) adj = MAX_ADJUST;
    g_phase_ms = (uint16_t)((int16_t)g_phase_ms + adj);
    if (g_phase_ms >= 900U) {
      g_phase_ms -= 900U;
      g_cycle++;
    }
  } else if (delta < -DEAD_BAND) {
    /* 本地相位超前，对相位做后移 */
    int16_t adj = -delta;
    if (adj > MAX_ADJUST) adj = MAX_ADJUST;
    if (g_phase_ms > (uint16_t)adj) {
      g_phase_ms = (uint16_t)(g_phase_ms - (uint16_t)adj);
    } else {
      g_phase_ms = (uint16_t)(900U - ((uint16_t)adj - g_phase_ms));
      if (g_cycle > 0) {
        g_cycle--;
      }
    }
  } else {
    /* 差异很小，忽略，避免抖动 */
  }
}

/* 日/夜检测：读 PA0 太阳能板电压，滞回 + 持续 3 秒确认后翻转 (规格书 §5) */
static void DayNight_Update(void)
{
  uint32_t now = HAL_GetTick();
  if ((now - g_last_daynight_tick) < DAYNIGHT_SAMPLE_INTERVAL_MS) {
    return;
  }
  g_last_daynight_tick = now;

  if (HAL_ADC_Start(&hadc1) != HAL_OK) {
    return;
  }
  if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
    HAL_ADC_Stop(&hadc1);
    return;
  }
  uint32_t raw = (uint32_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  uint8_t prev = g_is_night;

  if (raw < SOLAR_ADC_NIGHT_THRESHOLD) {
    /* 条件满足「夜间」 */
    if (g_is_night) {
      g_daynight_hold_until_tick = 0;
    } else {
      if (g_daynight_hold_until_tick == 0) {
        g_daynight_hold_until_tick = now + DAYNIGHT_HOLD_MS;
      } else if (now >= g_daynight_hold_until_tick) {
        g_is_night = 1;
        g_daynight_hold_until_tick = 0;
      }
    }
  } else if (raw > SOLAR_ADC_DAY_THRESHOLD) {
    /* 条件满足「白天」 */
    if (!g_is_night) {
      g_daynight_hold_until_tick = 0;
    } else {
      if (g_daynight_hold_until_tick == 0) {
        g_daynight_hold_until_tick = now + DAYNIGHT_HOLD_MS;
      } else if (now >= g_daynight_hold_until_tick) {
        g_is_night = 0;
        g_daynight_hold_until_tick = 0;
      }
    }
  } else {
    /* 滞回中间区：取消本次翻转确认 */
    g_daynight_hold_until_tick = 0;
  }

  if (prev != g_is_night) {
    DebugPrint(g_is_night ? "->Night\r\n" : "->Day\r\n");
  }
}

/* v1.7.2: 单固件多节点主循环状态机 */
static void Sync_MainLoop(void)
{
  /* 更新本地时间并驱动同步闪灯 LED */
  SyncTime_Update();
  SyncLamp_Update();

  /* 所有节点使用相同的固定 TX 时间，同步后 TX 时间也同步 */
  uint16_t tx_time = SYNC_TX_TIME_MS;

  /* 判断是否到了本周期的发送时间（且本周期尚未发送） */
  if (g_cycle != g_last_tx_cycle && g_phase_ms >= tx_time && g_phase_ms < (tx_time + 50U)) {
    /* 切换到 TX 模式发送同步包 */
    if (g_rf_mode != 1) {
      RF_Link_ConfigTx(76);
      g_rf_mode = 1;
    }

    BuildSyncPacket(RF_TX_Buf);
    if (RF_Link_Send(RF_TX_Buf, SYNC_PKT_SIZE) == 0) {
      DebugPrint("TX@");
      DebugPrintDec(g_phase_ms);
      DebugPrint("\r\n");
    }
    g_last_tx_cycle = g_cycle;
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

    /* 发送完立刻切回 RX 模式 (XL2400T: RX=75) */
    RF_Link_ConfigRx(75);
    g_rf_mode = 0;
  }

  /* RX 模式下轮询接收 */
  if (g_rf_mode == 0) {
    uint8_t rx_len = 0;
    if (RF_Link_PollReceive(RF_RX_Buf, &rx_len) == 1) {
      uint16_t rx_phase_ms = ParseSyncPacket(RF_RX_Buf);

      if (rx_phase_ms < SYNC_CYCLE_MS) {
        DebugPrint("RX: ph=");
        DebugPrintDec(rx_phase_ms);
        Sync_AdjustFromPacket(rx_phase_ms);
        SyncLamp_Update();  /* 调整相位后立即更新 LED，避免跳过边沿 */
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      }
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
