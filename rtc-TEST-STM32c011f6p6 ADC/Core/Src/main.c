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

/* USER CODE END Includes */












/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 固件调试串口打印：所有文本均通过 USART1 输出 */
static void UART_Print(const char *s)
{
  uint32_t len = 0U;
  const char *p = s;
  while (p != 0 && *p != '\0')
  {
    len++;
    p++;
  }

  if (len > 0U)
  {
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)len, 100U);
  }
}

void DebugPrint(const char *s)
{
#if (ENABLE_UART_DEBUG == 1)
  UART_Print(s);
#else
  (void)s;
#endif
}

/* RTC 闹钟触发计数器：由 RTC 中断回调递增，主循环在唤醒/处理时逐个消费
 * 说明：使用计数器避免“采样/串口输出期间闹钟又触发，但被主循环清零丢失”的问题。
 */
static volatile uint32_t g_rtc_wakeup_cnt = 0U;

/**
  * @brief RTC Alarm A 回调函数
  * @note 只做唤醒标志置位，避免在中断上下文里做复杂 HAL 操作
  */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
  UNUSED(hrtc);
  g_rtc_wakeup_cnt++;
}

/* 前置声明：RTC 设置函数里会先调用 DebugPrintU32，避免隐式声明导致 static 冲突 */
static void DebugPrintU32(uint32_t v);

/**
  * @brief  重新设置 RTC Alarm A：下一次触发秒 = 当前秒 + 3
  * @note   该函数在主循环上下文执行（非中断），避免 HAL_GetTick 相关超时风险
  */
static HAL_StatusTypeDef RTC_SetNextAlarmA_3s(RTC_HandleTypeDef *hrtc)
{
  RTC_TimeTypeDef rtcTime = {0};
  RTC_DateTypeDef rtcDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};
  uint8_t currSec = 0U;
  uint8_t nextSec = 0U;
  uint8_t newSec = 0U;

  /* 从低功耗/唤醒环境读取 RTC 时必须同步，且读取时间后要读取日期解锁影子寄存器 */
  if (HAL_RTC_WaitForSynchro(hrtc) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_RTC_GetTime(hrtc, &rtcTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    return HAL_ERROR;
  }
  (void)HAL_RTC_GetDate(hrtc, &rtcDate, RTC_FORMAT_BIN);

  currSec = rtcTime.Seconds;

  /* 等到 seconds 刷新（避免刚好错过目标秒窗口） */
  newSec = currSec;
  {
    uint32_t tickStart = HAL_GetTick();
    while ((newSec == currSec) && ((HAL_GetTick() - tickStart) < 200U))
    {
      if (HAL_RTC_GetTime(hrtc, &rtcTime, RTC_FORMAT_BIN) != HAL_OK)
      {
        break;
      }
      (void)HAL_RTC_GetDate(hrtc, &rtcDate, RTC_FORMAT_BIN);
      newSec = rtcTime.Seconds;
    }
  }

  sAlarm.Alarm = RTC_ALARM_A;
  sAlarm.AlarmTime.Hours = 0U;
  sAlarm.AlarmTime.Minutes = 0U;
  nextSec = (uint8_t)((newSec + 3U) % 60U);
  sAlarm.AlarmTime.Seconds = nextSec;
  sAlarm.AlarmTime.SubSeconds = 0U;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;

  /* 只匹配 Seconds：屏蔽 Date/WeekDay、Hours、Minutes */
  sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY | RTC_ALARMMASK_HOURS | RTC_ALARMMASK_MINUTES;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;

  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1U;

  /* 调试输出：便于确认 seconds 对齐是否正确 */
#if (ENABLE_UART_DEBUG == 1)
  DebugPrint("\r\n[RTC] currSec=");
  DebugPrintU32((uint32_t)currSec);
  DebugPrint(" nextSec=");
  DebugPrintU32((uint32_t)nextSec);
  DebugPrint("\r\n");
#endif

  return HAL_RTC_SetAlarm_IT(hrtc, &sAlarm, RTC_FORMAT_BIN);
}

/* 打印无符号十进制数到 UART（不依赖 printf，便于控制代码体积） */
static void UART_PrintU32(uint32_t v)
{
  char buf[11];
  uint32_t len = 0U;

  if (v == 0U)
  {
    buf[len++] = '0';
  }
  else
  {
    while (v > 0U && len < (sizeof(buf) - 1U))
    {
      buf[len++] = (char)('0' + (v % 10U));
      v /= 10U;
    }
  }

  /* 反向输出数字 */
  while (len > 0U)
  {
    uint8_t ch = (uint8_t)buf[len - 1U];
    (void)HAL_UART_Transmit(&huart1, &ch, 1U, 100U);
    len--;
  }
}

static void DebugPrintU32(uint32_t v)
{
#if (ENABLE_UART_DEBUG == 1)
  UART_PrintU32(v);
#else
  (void)v;
#endif
}

/**
  * @brief 采样 PA0/PA1：每次触发扫描序列，读取通道0/通道1各1次
  *        重复16次后对每个通道求平均。
  */
static void ADC_ReadAvgPa0Pa1_16x(uint16_t *avgPa0, uint16_t *avgPa1)
{
  uint32_t sum0 = 0U;
  uint32_t sum1 = 0U;
  uint32_t i = 0U;

  for (i = 0U; i < 16U; i++)
  {
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
      Error_Handler();
    }

    /* 第一次 EOC：PA0(ADC1_IN0) */
    if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
    {
      Error_Handler();
    }
    uint16_t v0 = (uint16_t)HAL_ADC_GetValue(&hadc1);

    /* 第二次 EOC：PA1(ADC1_IN1) */
    if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
    {
      Error_Handler();
    }
    uint16_t v1 = (uint16_t)HAL_ADC_GetValue(&hadc1);

    (void)HAL_ADC_Stop(&hadc1);

    sum0 += (uint32_t)v0;
    sum1 += (uint32_t)v1;
  }

  *avgPa0 = (uint16_t)(sum0 / 16U);
  *avgPa1 = (uint16_t)(sum1 / 16U);
}

/**
  * @brief 进入 STOP 前的功耗处理
  * @note 低风险策略：本测试中保留 CubeMX 的 RF/控制脚默认状态；仅对 ADC/USART 做去初始化与模拟模式配置。
  */
static void Power_PrepareForStop(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 关闭 ADC 外设以降低功耗（PA0/PA1 重新置为 Analog 防止悬空漏电） */
  (void)HAL_ADC_Stop(&hadc1);
  (void)HAL_ADC_DeInit(&hadc1);
  GPIO_InitStruct.Pin = (GPIO_PIN_0 | GPIO_PIN_1);
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

#if (ENABLE_UART_DEBUG == 0)
  /* 调试关闭时：关闭 USART1 并把 TX/RX 引脚置为 Analog 进一步降低漏电 */
  (void)HAL_UART_DeInit(&huart1);
  __HAL_RCC_USART1_CLK_DISABLE();
  GPIO_InitStruct.Pin = (GPIO_PIN_9 | GPIO_PIN_10);
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
#endif
}

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

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  /* 上电打印固件版本号：用于区分当前烧录的测试固件 */
  UART_Print("\r\n[FW] " FW_VERSION "\r\n");

  /* 使能 STOP 模式下的 Debug：避免出现你说的 SWD/RJTAG 锁死、无法下载 */
  HAL_DBGMCU_EnableDBGStopMode();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 如果没有待处理的 RTC 唤醒事件，才进入 STOP */
    if (g_rtc_wakeup_cnt == 0U)
    {
      DebugPrint("\r\n[LP-IN]\r\n");

      /* 进入 STOP 之前进行功耗准备（关闭 ADC/可选关闭 UART） */
      Power_PrepareForStop();

      /* 进入 STOP 前暂停 SysTick，避免 Tick 在低功耗状态下异常 */
      HAL_SuspendTick();

      /* 等待中断：由 RTC Alarm A 唤醒 */
      #if (LOWPOWER_USE_STOP == 1)
      HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
      /* 唤醒后恢复时钟树与 SysTick（STOP 需要） */
      SystemClock_Config();
      PeriphCommonClock_Config();
      DebugPrint("\r\n[LP-RET-STOP]\r\n");
      #else
      HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
      DebugPrint("\r\n[LP-RET-SLEEP]\r\n");
      #endif
      HAL_ResumeTick();

      /* 从低功耗返回后打印（用于确认唤醒是否发生、计数是否递增） */
      DebugPrint("\r\n[LP-RET] cnt=");
      DebugPrintU32(g_rtc_wakeup_cnt);
      DebugPrint("\r\n");
    }

    /* 消费可能累积的唤醒事件（避免丢事件导致周期变成约 60s） */
    while (g_rtc_wakeup_cnt > 0U)
    {
      g_rtc_wakeup_cnt--;

      /* 重设下一次闹钟：当前秒 + 3s */
      if (RTC_SetNextAlarmA_3s(&hrtc) == HAL_OK)
      {
        /* 最小验证输出：确认唤醒链路通，并维持下一次 3s 周期 */
        DebugPrint("\r\n[WAKE+RTC]\r\n");
      }

      /* 唤醒后执行 ADC 采样与电压换算 */
      uint16_t adc0_avg = 0U;
      uint16_t adc1_avg = 0U;

      /* 每次事件处理都重新初始化 ADC，确保在 STOP 前去初始化后恢复正常 */
      MX_ADC1_Init();
      ADC_ReadAvgPa0Pa1_16x(&adc0_avg, &adc1_avg);

      /* V(mV) = adc_avg * 3300 / 4095（Vref=3.3V，12bit） */
      uint32_t v0_mV = (uint32_t)(((uint64_t)adc0_avg * 3300ULL) / 4095ULL);
      uint32_t v1_mV = (uint32_t)(((uint64_t)adc1_avg * 3300ULL) / 4095ULL);

      DebugPrint("\r\n[ADC]");
      DebugPrint(" PA0=");
      DebugPrintU32(v0_mV);
      DebugPrint("mV");
      DebugPrint(" PA1=");
      DebugPrintU32(v1_mV);
      DebugPrint("mV\r\n");
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV4;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the common peripherals clocks
  */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_HSIKER;
  PeriphClkInit.HSIKerClockDivider = RCC_HSIKER_DIV4;

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

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  /* 让第一次 RTC Alarm A 尽快触发：当前 seconds=0，Alarm A seconds=3 */
  sTime.Seconds = 0x0;
  sTime.SubSeconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm A
  */
  sAlarm.AlarmTime.Hours = 0x0;
  sAlarm.AlarmTime.Minutes = 0x0;
  sAlarm.AlarmTime.Seconds = 0x3;
  sAlarm.AlarmTime.SubSeconds = 0x0;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
  sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY|RTC_ALARMMASK_HOURS
                              |RTC_ALARMMASK_MINUTES;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1;
  sAlarm.Alarm = RTC_ALARM_A;
  if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 192;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 115;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_ENABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

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
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, BOOST_EN_Pin|RF_CSN_Pin|RF_SCK_Pin|DIV_MOS_Pin
                          |RF_DATA_Pin|CHG_MOS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : KEY_IN_Pin */
  GPIO_InitStruct.Pin = KEY_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(KEY_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BOOST_EN_Pin RF_CSN_Pin RF_SCK_Pin DIV_MOS_Pin
                           RF_DATA_Pin CHG_MOS_Pin */
  GPIO_InitStruct.Pin = BOOST_EN_Pin|RF_CSN_Pin|RF_SCK_Pin|DIV_MOS_Pin
                          |RF_DATA_Pin|CHG_MOS_Pin;
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
