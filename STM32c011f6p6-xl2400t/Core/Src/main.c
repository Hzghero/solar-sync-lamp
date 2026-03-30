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
#include <string.h>
#include <stdio.h>

/* 中文注释：
   - XL2400T.h 与 rf_xl2400.h 提供 2.4GHz RF 模块初始化、发送、接收接口
   - 采用 900ms 周期同步协议：0-450ms 发送同步包，450-900ms 接收同步包
   - LED 指示：PB6 (LED_Pin) 同步事件指示 + PA2 (LED_DRV_Pin) PWM 驱动
*/
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SYNC_CYCLE_MS       900U    /* 同步周期 900ms */
#define SYNC_LED_ON_MS      100U    /* LED 亮灯时间 100ms */
#define SYNC_TX_TIME_MS     450U    /* TX 发送时间：所有节点固定 450ms */
#define SYNC_TX_DELAY_MS    6U      /* 传输延迟补偿 */
#define SYNC_PKT_SIZE       4U      /* AA 55 + 2字节相位 */
#define RF_TX_CHANNEL       76U     /* XL2400T TX 频道 76 (2476 MHz) */
#define RF_RX_CHANNEL       75U     /* XL2400T RX 频道 75 (2475 MHz) - 相邻频道避免自干扰 */

#define SOLAR_ADC_NIGHT_THRESHOLD   360U
#define SOLAR_ADC_DAY_THRESHOLD    496U
#define DAYNIGHT_SAMPLE_INTERVAL_MS 1000U
#define DAYNIGHT_HOLD_MS            3000U

#define BATT_ADC_OVERCHARGE_RAW     1924U
#define BATT_ADC_REENABLE_RAW      1676U
#define CHARGE_SAMPLE_INTERVAL_MS  3000U

/* 电池 ~0.9V 提前停载（与 ME2188 约 0.7V 硬截止、回升约 0.9V 再配合）；ADC 折算同 Charge_Update 3.3V 参考 */
#define BATT_ADC_UNDERVOLT_RAW          1116U  /* ≈0.90V */
#define BATT_ADC_UNDERVOLT_RECOVER_RAW  1215U  /* ≈0.98V 滞回，避免抖动 */
#define BATT_UV_SAMPLE_MS             500U   /* 两次确认间隔 */

#define UV_STOP_RTC_ALARM_STEP_SEC   5U    /* 欠压 STOP 期间 RTC Alarm A 周期间隔（秒） */

#define DEBUG_ADC_VERBOSE  1   /* 调试：1=打印 ADC 采样值，完成后可改为 0 精简 */
#define DEBUG_SYNC_VERBOSE 1   /* 调试：1=打印详细同步信息，0=只打印关键事件 */
#define DEBUG_LED_VERBOSE  1   /* 调试：1=打印LED状态，0=不打印 */
#define DEBUG_PERIODIC     0   /* 调试：1=启用定期打印（影响功耗），0=禁用 */

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
static uint8_t RF_TX_Buf[RF_PACKET_SIZE]   = {0};
static uint8_t RF_RX_Buf[RF_PACKET_SIZE]   = {0};

static uint32_t g_cycle = 0;
static uint16_t g_phase_ms = 0;
static uint32_t g_last_tick_ms = 0;
static uint32_t g_last_tx_cycle = (uint32_t)-1;

static uint8_t  g_rf_mode = 0;
static uint8_t  g_led_state = 0;
static uint32_t g_led_on_tick = 0;

static uint8_t  g_is_night = 1;
static uint32_t g_last_daynight_tick = 0;
static uint32_t g_daynight_hold_until_tick = 0;

static uint32_t g_last_charge_tick = 0;

static uint8_t  g_rf_sleeping = 0;
static uint8_t  g_rf_sleeping_night = 0;
static uint32_t g_adc_display_tick = 0;  /* ADC显示计时器 */

static uint8_t  g_batt_undervolt = 0;     /* 1=已触发软件欠压，停载并在 STOP 中等待唤醒 */
static uint32_t g_last_uv_tick = 0;
static uint8_t  g_uv_low_pending = 0;   /* 第一次采样低于阈值，待间隔后二次确认 */

/* 夜间初期双通道采样调试 */
static uint32_t g_night_start_tick = 0;     /* 夜间开始时间 */
static uint8_t  g_night_debug_window = 0;   /* 是否在调试窗口内（夜间开始后6秒） */
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
static void DebugPrint(const char *s);
static void DebugPrintHex(const uint8_t *buf, uint8_t len);
static void DebugPrintDec(uint16_t val);
static void SyncTime_Update(void);
static void SyncLamp_Update(void);
static void BuildSyncPacket(uint8_t *pkt);
static uint16_t ParseSyncPacket(const uint8_t *pkt);
static void Sync_AdjustFromPacket(uint16_t rx_phase_ms);
static void Sync_MainLoop(void);
static void DayNight_Update(void);
static void Charge_Update(void);
static void BattUndervolt_Update(void);
static void Undervolt_PrepareExtiPa0(void);
static void Undervolt_RestoreAdcFromExti(void);
static void Undervolt_EnterLowPowerOutputs(void);
static void Undervolt_SetUartPinsAnalog(void);
static HAL_StatusTypeDef Undervolt_RTC_SetNextAlarm(RTC_HandleTypeDef *hrtc);
static uint32_t Read_ADC1_Channel(uint32_t channel);  /* 指定通道读 ADC1 一次，PA0=ch0 太阳能，PA1=ch1 电池 */
static void Test_ADC_Channels(void);                   /* ADC通道切换测试函数 */
static uint8_t Read_ADC1_DualChannel(uint32_t* solar_raw, uint32_t* batt_raw);  /* 扫描模式读取双通道 */
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
  DebugPrint("[FW] " FW_VERSION "\r\n");

  /* 启用升压电路供电 */
  HAL_Delay(100);
  HAL_GPIO_WritePin(BOOST_EN_GPIO_Port, BOOST_EN_Pin, GPIO_PIN_SET);



    /* RF 模块初始化 */
  DebugPrint("RF Init...\r\n");
  RF_Link_Init();
  DebugPrint("RF Init OK\r\n");

    /* 默认进入 RX 模式，监听频道 75 */
  DebugPrint("RF Config RX...\r\n");
  RF_Link_ConfigRx(RF_RX_CHANNEL);
  g_rf_mode = 0;
  DebugPrint("RF Ready\r\n");
  
  /* 调试：确认TX固定时间 */
  DebugPrint("[SYNC] TX fixed at ");
  DebugPrintDec(SYNC_TX_TIME_MS);
  DebugPrint("ms, Window: ");
  DebugPrintDec(SYNC_TX_TIME_MS);
  DebugPrint("- ");
  DebugPrintDec(SYNC_TX_TIME_MS + 50U);
  DebugPrint("ms\r\n");





  /* 初始化同步数据 */
  g_cycle = 0;
  g_phase_ms = 0;
  g_last_tick_ms = HAL_GetTick();
  g_last_tx_cycle = (uint32_t)-1;

  g_led_state = 0;
  g_led_on_tick = 0;

  g_is_night = 1;
  g_last_daynight_tick = HAL_GetTick();
  g_daynight_hold_until_tick = 0;  /* 0=未在确认中，非0=满足条件后需持续到该 tick 才翻转 */

  g_last_charge_tick = HAL_GetTick();

  g_rf_sleeping = 0;
  g_rf_sleeping_night = 0;

#if DEBUG_ADC_VERBOSE
  /* 积木1 调试：上电读取双通道 ADC 验证 */
  uint32_t s0 = Read_ADC1_Channel(ADC_CHANNEL_0);
  uint32_t s1 = Read_ADC1_Channel(ADC_CHANNEL_1);
  DebugPrint("[ADC] ch0=");
  DebugPrintDec((uint16_t)s0);
  DebugPrint(" ch1=");
  DebugPrintDec((uint16_t)s1);
  DebugPrint("\r\n");
  
  /* 运行ADC通道切换测试 */
  Test_ADC_Channels();
#endif

      /* 看门狗：暂时禁用，后续再解决 */
  // MX_IWDG_Init();
  // 
  // /* 等待看门狗计数器超过窗口值（3072），确保第一次喂狗在窗口内 */
  // HAL_Delay(100);  /* 100ms 足够让计数器超过 3072（32.7s * 3072/4095 ≈ 24.5s 的 0.4%） */
  // 
  // /* 第一次喂狗，确保在窗口内 */
  // HAL_IWDG_Refresh(&hiwdg);
  // DebugPrint("[IWDG] First refresh OK\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
  {
    static uint8_t last_is_night = 1;  /* 积木6/7：日夜切换时处理 RF 与低功耗 */
    /* 积木8：喂狗 - 暂时禁用 */
    // HAL_IWDG_Refresh(&hiwdg);

    #if DEBUG_PERIODIC
    /* 定期显示双通道ADC值（每10秒一次）- 影响功耗，调试完成后禁用 */
    uint32_t now = HAL_GetTick();
    if((now - g_adc_display_tick) >= 10000U) {
      g_adc_display_tick = now;
      
            uint32_t solar_raw, batt_raw;
      if(Read_ADC1_DualChannel(&solar_raw, &batt_raw) == 0) {
        DebugPrint("[ADC-DUAL] solar=");
        DebugPrintDec((uint16_t)solar_raw);
        DebugPrint("(");
        
        /* 计算太阳能电压 */
        uint32_t solar_mv = (solar_raw * 3300) / 4095;
        DebugPrintDec((uint16_t)(solar_mv / 1000));
        DebugPrint(".");
        DebugPrintDec((uint16_t)((solar_mv % 1000) / 100));
        DebugPrintDec((uint16_t)((solar_mv % 100) / 10));
        DebugPrint("V) batt=");
        
        DebugPrintDec((uint16_t)batt_raw);
        DebugPrint("(");
        
        /* 计算电池电压 */
        uint32_t batt_mv = (batt_raw * 3300) / 4095;
        DebugPrintDec((uint16_t)(batt_mv / 1000));
        DebugPrint(".");
        DebugPrintDec((uint16_t)((batt_mv % 1000) / 100));
        DebugPrintDec((uint16_t)((batt_mv % 100) / 10));
        DebugPrint("V)\r\n");
      }
    }
#endif

    DayNight_Update();
    Charge_Update();
    BattUndervolt_Update();

    /* 欠压：停载、CHG_MOS 拉低；STOP 期间由 RTC Alarm + PA0 上升沿唤醒再测电量 */
    if(g_batt_undervolt) {
      Undervolt_EnterLowPowerOutputs();
      while(g_batt_undervolt) {
        DebugPrint("[UV] enter STOP (RTC ~");
        DebugPrintDec((uint16_t)UV_STOP_RTC_ALARM_STEP_SEC);
        DebugPrint("s + EXTI0)\r\n");
        if(Undervolt_RTC_SetNextAlarm(&hrtc) != HAL_OK) {
          DebugPrint("[UV] RTC arm failed\r\n");
        }
        /* STOP 前关串口并拉 PA9/PA10 为模拟输入，避免 UART 复用/浮空脚漏电（白天分支 Sleep 亦 DeInit UART） */
        HAL_UART_DeInit(&huart1);
        Undervolt_SetUartPinsAnalog();
        Undervolt_PrepareExtiPa0();
        HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
        SystemClock_Config();
        PeriphCommonClock_Config();
        Undervolt_RestoreAdcFromExti();
        MX_USART1_UART_Init();
        __HAL_RTC_CLEAR_FLAG(&hrtc, RTC_CLEAR_ALRAF);
        DebugPrint("[UV] wake from STOP\r\n");
        uint32_t br = Read_ADC1_Channel(ADC_CHANNEL_1);
        if(br != 0xFFFFU && (uint16_t)br >= BATT_ADC_UNDERVOLT_RECOVER_RAW) {
          g_batt_undervolt = 0;
#if DEBUG_ADC_VERBOSE
          DebugPrint("[UV] recovered batt OK\r\n");
#endif
          break;
        }
      }
      HAL_GPIO_WritePin(BOOST_EN_GPIO_Port, BOOST_EN_Pin, GPIO_PIN_SET);
      HAL_Delay(50);
      if(g_is_night) {
        RF_Link_ConfigRx(RF_RX_CHANNEL);
        g_rf_mode = 0;
        g_rf_sleeping = 0;
      } else {
        RF_Link_Sleep();
        g_rf_sleeping = 1;
      }
      g_last_tick_ms = HAL_GetTick();
      continue;
    }

    /* 日夜切换：进入夜间唤醒 RF，进入白天 RF 睡眠 */
    {
      static uint8_t last_is_night = 1;
      if(last_is_night != g_is_night) {
        if(g_is_night) {
          RF_Link_ConfigRx(RF_RX_CHANNEL);
          g_rf_mode = 0;
          g_rf_sleeping = 0;
        } else {
          RF_Link_Sleep();
          g_rf_sleeping = 1;
        }
        last_is_night = g_is_night;
      }
    }

    if(g_is_night) {
      /* 夜间：同步+闪灯 */
      Sync_MainLoop();
      SyncLamp_Update();
      HAL_Delay(1);
    } else {
      /* 白天：关灯，不跑同步 */
      HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

      if(!g_rf_sleeping) {
        RF_Link_Sleep();
        g_rf_sleeping = 1;
      }

            /* 积木7：白天 CPU 进入 Sleep(WFI)，靠 SysTick 唤醒 */
      /* 进入睡眠前禁用串口以降低功耗 */
      HAL_UART_DeInit(&huart1);
      HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
      /* 唤醒后重新初始化串口 */
      MX_USART1_UART_Init();
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
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
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
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;
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
  sTime.Seconds = 0x3;
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
static void DebugPrint(const char *s)
{
  HAL_UART_Transmit(&huart1, (uint8_t*)s, strlen(s), 100);
}

static void DebugPrintHex(const uint8_t *buf, uint8_t len)
{
  char hex[3];
  uint8_t i;
  for(i = 0; i < len; i++) {
    sprintf(hex, "%02X", buf[i]);
    HAL_UART_Transmit(&huart1, (uint8_t*)hex, 2, 100);
  }
}

static void DebugPrintDec(uint16_t val)
{
  char buf[6];
  sprintf(buf, "%u", val);
  HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
}

/* ADC通道切换测试函数 */
static void Test_ADC_Channels(void)
{
  DebugPrint("[ADC-TEST] Starting channel switching test...\r\n");
  
    /* 测试1：连续读取同一通道多次 */
  DebugPrint("[ADC-TEST] Reading CH0 3 times:\r\n");
  {
    int i;
    for(i = 0; i < 3; i++) {
      uint32_t val = Read_ADC1_Channel(ADC_CHANNEL_0);
      if(val != 0xFFFFU) {
        DebugPrint("  CH0[");
        DebugPrintDec(i);
        DebugPrint("]=");
        DebugPrintDec((uint16_t)val);
        DebugPrint("\r\n");
      }
      HAL_Delay(100);
    }
  }
  
    /* 测试2：使用扫描模式读取双通道 */
  DebugPrint("[ADC-TEST] Reading dual channels (scan mode):\r\n");
  uint32_t ch0_val, ch1_val;
  if(Read_ADC1_DualChannel(&ch0_val, &ch1_val) == 0) {
    DebugPrint("  CH0=");
    DebugPrintDec((uint16_t)ch0_val);
    DebugPrint(" CH1=");
    DebugPrintDec((uint16_t)ch1_val);
    DebugPrint(" Diff=");
    if(ch0_val > ch1_val) {
      DebugPrintDec((uint16_t)(ch0_val - ch1_val));
    } else {
      DebugPrintDec((uint16_t)(ch1_val - ch0_val));
    }
    DebugPrint("\r\n");
  } else {
    DebugPrint("  Dual channel read failed\r\n");
  }
  
    /* 测试3：交替读取 */
  DebugPrint("[ADC-TEST] Alternating CH0/CH1 3 times:\r\n");
  {
    int i;
    for(i = 0; i < 3; i++) {
      uint32_t val0 = Read_ADC1_Channel(ADC_CHANNEL_0);
      HAL_Delay(50);
      uint32_t val1 = Read_ADC1_Channel(ADC_CHANNEL_1);
      HAL_Delay(50);
      
      if(val0 != 0xFFFFU && val1 != 0xFFFFU) {
        DebugPrint("  Iter[");
        DebugPrintDec(i);
        DebugPrint("]: CH0=");
        DebugPrintDec((uint16_t)val0);
        DebugPrint(" CH1=");
        DebugPrintDec((uint16_t)val1);
        DebugPrint("\r\n");
      }
    }
  }
  
  DebugPrint("[ADC-TEST] Test completed\r\n");
}

/**
 * @brief  更新本地时钟相位基准，积木5：周期边界时启动 LED PWM
 *         使用 HAL_GetTick 的时间差，滚动更新 g_phase_ms
 */
static void SyncTime_Update(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t delta = now - g_last_tick_ms;
  if(delta == 0) return;
  g_last_tick_ms = now;

  uint32_t total = g_phase_ms + delta;
  uint32_t new_cycles = total / SYNC_CYCLE_MS;
  g_cycle += new_cycles;
  g_phase_ms = (uint16_t)(total % SYNC_CYCLE_MS);

        /* 周期边界：跨越 0ms 时立即点亮 LED（PWM 124kHz 60%），积木4 */
  if(new_cycles > 0) {
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    g_led_on_tick = now;
    g_led_state = 1;
    #if DEBUG_SYNC_VERBOSE
    DebugPrint("[CYCLE] new_cycle=");
    DebugPrintDec(g_cycle);
    DebugPrint(", phase_reset=0ms\r\n");
#endif
#if DEBUG_LED_VERBOSE
    DebugPrint("[LED] CYCLE @0\r\n");
#endif
  }
}

/**
 * @brief  同步闪灯：时间戳保证至少亮 100ms 后熄灭，积木4/5
 *         LED_DRV(PA2) 用 TIM1_CH3 PWM，PB6 状态指示
 */
static void SyncLamp_Update(void)
{
  uint32_t now = HAL_GetTick();

  if(g_led_state == 1) {
    if((now - g_led_on_tick) >= SYNC_LED_ON_MS) {
      HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
      g_led_state = 0;
      #if DEBUG_LED_VERBOSE
      DebugPrint("[LED] OFF\r\n");
#endif
    }
  }
  /* 注释：移除补亮逻辑，LED只在周期边界点亮 */
  /* 这样确保每个周期只点亮一次，持续100ms */
}

/* 构造同步包，积木8：传输延迟补偿 6ms */
static void BuildSyncPacket(uint8_t *pkt)
{
  uint16_t compensated = g_phase_ms + SYNC_TX_DELAY_MS;
  if(compensated >= SYNC_CYCLE_MS) compensated -= SYNC_CYCLE_MS;

  pkt[0] = 0xAA;
  pkt[1] = 0x55;
  pkt[2] = (uint8_t)(compensated >> 8);
  pkt[3] = (uint8_t)(compensated & 0xFF);
}

static uint16_t ParseSyncPacket(const uint8_t *pkt)
{
  if(pkt[0] != 0xAA || pkt[1] != 0x55) return 0xFFFF;
  return (uint16_t)((pkt[2] << 8) | pkt[3]);
}

static void Sync_AdjustFromPacket(uint16_t rx_phase_ms)
{
  int16_t delta = (int16_t)rx_phase_ms - (int16_t)g_phase_ms;
  if(delta > (int16_t)(SYNC_CYCLE_MS / 2)) delta -= SYNC_CYCLE_MS;
  if(delta < -(int16_t)(SYNC_CYCLE_MS / 2)) delta += SYNC_CYCLE_MS;
  
  #if DEBUG_SYNC_VERBOSE
  /* 调试：显示调整前的详细信息 */
  DebugPrint("[ADJ] rx=");
  DebugPrintDec(rx_phase_ms);
  DebugPrint(" local=");
  DebugPrintDec(g_phase_ms);
  DebugPrint(" delta=");
  if(delta >= 0) {
    DebugPrint("+");
    DebugPrintDec((uint16_t)delta);
  } else {
    DebugPrint("-");
    DebugPrintDec((uint16_t)(-delta));
  }
  DebugPrint(" adjust=");
  DebugPrintDec((uint16_t)(delta / 4));
  DebugPrint("ms\r\n");
#endif

  g_phase_ms = (uint16_t)((int16_t)g_phase_ms + delta / 4);
  if(g_phase_ms >= SYNC_CYCLE_MS) g_phase_ms -= SYNC_CYCLE_MS;
}

/**
 * @brief  同步主状态机：TX@450ms固定时间发送，450-900ms全程RX侦听
 *         相邻频道策略（TX@76, RX@75）避免自干扰和碰撞
 */
static void Sync_MainLoop(void)
{
  /* 更新本地时间基准（增量式） */
  SyncTime_Update();
  /* 驱动 LED 指示灯（100ms 保持亮，然后熄灭） */
  SyncLamp_Update();

  /* 固定 TX 时间段：所有节点同一时刻发送，确保相位对齐 */
  if (g_cycle != g_last_tx_cycle && g_phase_ms >= SYNC_TX_TIME_MS && g_phase_ms < (SYNC_TX_TIME_MS + 50U)) {
    /* 切换到 TX 模式，频道 76 */
    if (g_rf_mode != 1) {
      RF_Link_ConfigTx(RF_TX_CHANNEL);
      g_rf_mode = 1;
    }

        /* 构造并发送同步包 */
    BuildSyncPacket(RF_TX_Buf);
        if (RF_Link_Send(RF_TX_Buf, SYNC_PKT_SIZE) == 0) {
#if DEBUG_SYNC_VERBOSE
      DebugPrint("[TX] phase=");
      DebugPrintDec(g_phase_ms);
      DebugPrint("ms, target=");
      DebugPrintDec(SYNC_TX_TIME_MS);
      DebugPrint("ms, diff=");
      if(g_phase_ms >= SYNC_TX_TIME_MS) {
        DebugPrint("+");
        DebugPrintDec(g_phase_ms - SYNC_TX_TIME_MS);
      } else {
        DebugPrint("-");
        DebugPrintDec(SYNC_TX_TIME_MS - g_phase_ms);
      }
      DebugPrint("ms\r\n");
#else
      DebugPrint("[TX]\r\n");
#endif
    }
    g_last_tx_cycle = g_cycle;

            /* 注释：TX事件不再点亮LED，只做调试输出 */
    /* HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3); */
    /* HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET); */
    /* g_led_on_tick = HAL_GetTick(); */
    /* g_led_state = 1; */
    /* LED调试：TX事件（仅调试，不影响LED） */
    DebugPrint("[TX]\r\n");

    /* 发送完立刻切回 RX 模式，频道 75，全程侦听 */
    RF_Link_ConfigRx(RF_RX_CHANNEL);
    g_rf_mode = 0;
  }

  /* RX 模式下轮询接收 - 全周期执行（不睡眠，连续侦听） */
  if (g_rf_mode == 0) {
    uint8_t rx_len = 0;
    if (RF_Link_PollReceive(RF_RX_Buf, &rx_len) == 1) {
      uint16_t rx_phase_ms = ParseSyncPacket(RF_RX_Buf);

                  if (rx_phase_ms < SYNC_CYCLE_MS) {
#if DEBUG_SYNC_VERBOSE
        DebugPrint("RX: ph=");
        DebugPrintDec(rx_phase_ms);
        DebugPrint(" local=");
        DebugPrintDec(g_phase_ms);
        
        /* 计算相位差异 */
        int16_t phase_diff = (int16_t)rx_phase_ms - (int16_t)g_phase_ms;
        if(phase_diff > (int16_t)(SYNC_CYCLE_MS / 2)) phase_diff -= SYNC_CYCLE_MS;
        if(phase_diff < -(int16_t)(SYNC_CYCLE_MS / 2)) phase_diff += SYNC_CYCLE_MS;
        
        DebugPrint(" diff=");
        if(phase_diff >= 0) {
          DebugPrint("+");
          DebugPrintDec((uint16_t)phase_diff);
        } else {
          DebugPrint("-");
          DebugPrintDec((uint16_t)(-phase_diff));
        }
        
        DebugPrint("[RX]");
        
        DebugPrint(" adj=");
        DebugPrintDec(g_phase_ms);
        DebugPrint("\r\n");
#else
        DebugPrint("RX\r\n");
#endif
        
        /* 相位调整 */
        Sync_AdjustFromPacket(rx_phase_ms);
      }
    }
  }
}

/**
 * @brief  指定通道读 ADC1 一次，返回 raw（失败返回 0xFFFF）
 *         PA0=ch0 太阳能板电压，PA1=ch1 电池电压
 *         MX_ADC1 为 ADC_SCAN_SEQ_FIXED + 双通道时，EOC_SINGLE 先结束的是序列中靠前通道；
 *         只读 CH1 时须用 ADC_RANK_NONE 从序列移除 CH0，否则 GetValue 常为 PA0。
 */
static uint32_t Read_ADC1_Channel(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  
  HAL_ADC_Stop(&hadc1);
  HAL_ADC_DeInit(&hadc1);
  MX_ADC1_Init();

  sConfig.SamplingTime = ADC_SAMPLETIME_7CYCLES_5;
  if(channel == ADC_CHANNEL_1) {
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_RANK_NONE;
    if(HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
      return 0xFFFFU;
    }
  } else if(channel == ADC_CHANNEL_0) {
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_RANK_NONE;
    if(HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
      return 0xFFFFU;
    }
  } else {
    sConfig.Channel = channel;
    sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
    if(HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
      return 0xFFFFU;
    }
  }

  HAL_Delay(2);
  
  /* 清除所有ADC标志 */
  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR | ADC_FLAG_EOS | ADC_FLAG_EOSMP);
  
  /* 启动ADC */
  if (HAL_ADC_Start(&hadc1) != HAL_OK) {
    return 0xFFFFU;
  }
  
  /* 等待转换完成 */
  if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
    HAL_ADC_Stop(&hadc1);
    return 0xFFFFU;
  }
  
  uint32_t raw = (uint32_t)HAL_ADC_GetValue(&hadc1);
  
  /* 停止ADC */
  HAL_ADC_Stop(&hadc1);
  
  return raw;
}

/**
 * @brief  扫描模式读取双通道ADC值（太阳能和电池）
 * @param  solar_raw: 太阳能电压ADC原始值（PA0/CH0）
 * @param  batt_raw: 电池电压ADC原始值（PA1/CH1）
 * @retval 0=成功，1=失败
 * 说明：使用ADC扫描模式，一次转换读取两个通道
 */
static uint8_t Read_ADC1_DualChannel(uint32_t* solar_raw, uint32_t* batt_raw)
{
  uint32_t adc_values[2] = {0};
  
  /* 确保ADC已停止 */
  HAL_ADC_Stop(&hadc1);
  
  /* 清除所有ADC标志 */
  __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR | ADC_FLAG_EOS | ADC_FLAG_EOSMP);
  
  /* 启动ADC转换 */
  if (HAL_ADC_Start(&hadc1) != HAL_OK) {
    return 1;
  }
  
    /* 等待序列转换完成 */
  if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
    HAL_ADC_Stop(&hadc1);
    return 1;
  }
  
  /* 读取两个通道的值 */
  /* 注意：在扫描模式下，HAL_ADC_GetValue()会返回当前转换的值 */
  /* 我们需要读取两次来获取两个通道的值 */
  adc_values[0] = HAL_ADC_GetValue(&hadc1);  /* 第一个转换：CH0 */
  
  /* 对于STM32C0，在扫描模式下，需要再次调用GetValue获取第二个通道 */
  /* 或者使用DMA/中断方式，这里我们简单处理 */
  adc_values[1] = HAL_ADC_GetValue(&hadc1);  /* 第二个转换：CH1 */
  
  /* 停止ADC */
  HAL_ADC_Stop(&hadc1);
  
  /* 返回结果 */
  *solar_raw = adc_values[0];
  *batt_raw = adc_values[1];
  
  return 0;
}

/**
 * @brief 两次采样确认后锁存欠压；与 Charge_Update 独立，夜间/白天均可能触发
 */
static void BattUndervolt_Update(void)
{
  if(g_batt_undervolt) {
    return;
  }
  uint32_t now = HAL_GetTick();
  if((now - g_last_uv_tick) < BATT_UV_SAMPLE_MS) {
    return;
  }
  g_last_uv_tick = now;

  uint32_t raw = Read_ADC1_Channel(ADC_CHANNEL_1);
  if(raw == 0xFFFFU) {
    return;
  }
  if((uint16_t)raw <= BATT_ADC_UNDERVOLT_RAW) {
    if(!g_uv_low_pending) {
      g_uv_low_pending = 1;
      return;
    }
    g_batt_undervolt = 1;
    g_uv_low_pending = 0;
#if DEBUG_ADC_VERBOSE
    DebugPrint("[UV] latched <=0.9V\r\n");
#endif
  } else {
    g_uv_low_pending = 0;
  }
}

/**
 * @brief USART1 已 DeInit 后，将 PA9/PA10 配成模拟输入以降低 STOP 漏电（参考 AN4899）
 */
static void Undervolt_SetUartPinsAnalog(void)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);
}

/**
 * @brief 欠压停载：BOOST 关断放电；CHG_MOS 强制低（仅过充时软件才置高关充电）
 */
static void Undervolt_EnterLowPowerOutputs(void)
{
  HAL_GPIO_WritePin(CHG_MOS_GPIO_Port, CHG_MOS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BOOST_EN_GPIO_Port, BOOST_EN_Pin, GPIO_PIN_RESET);
  HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  if(!g_rf_sleeping) {
      RF_Link_Sleep();
      g_rf_sleeping = 1;
  }
}

/**
 * @brief 释放 ADC 引脚并将 PA0 配成 EXTI 上升沿，供 STOP 期间太阳能电压上升唤醒
 */
static void Undervolt_PrepareExtiPa0(void)
{
  GPIO_InitTypeDef g = {0};
  HAL_NVIC_DisableIRQ(EXTI0_1_IRQn);
  HAL_ADC_DeInit(&hadc1);
  __HAL_RCC_GPIOA_CLK_ENABLE();
  /* ADC 释放后 PA1 易呈浮空输入，STOP 下漏电偏大；模拟输入漏电最低 */
  g.Pin = GPIO_PIN_1;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_0;
  g.Mode = GPIO_MODE_IT_RISING;
  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &g);
  HAL_NVIC_SetPriority(EXTI0_1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
}

static void Undervolt_RestoreAdcFromExti(void)
{
  HAL_NVIC_DisableIRQ(EXTI0_1_IRQn);
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);
  MX_ADC1_Init();
}

/**
 * @brief  重装 Alarm A：下一次触发在「当前 RTC 秒 + UV_STOP_RTC_ALARM_STEP_SEC」（仅匹配秒域）
 * @note   与 rtc-TEST-STM32c011f6p6 ADC 一致；主循环/唤醒后调用，不在 ISR 里调用
 */
static HAL_StatusTypeDef Undervolt_RTC_SetNextAlarm(RTC_HandleTypeDef *hrtc_p)
{
  RTC_TimeTypeDef rtcTime = {0};
  RTC_DateTypeDef rtcDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};
  uint8_t currSec;
  uint8_t newSec;
  uint8_t nextSec;

  if(HAL_RTC_WaitForSynchro(hrtc_p) != HAL_OK) {
    return HAL_ERROR;
  }
  if(HAL_RTC_GetTime(hrtc_p, &rtcTime, RTC_FORMAT_BIN) != HAL_OK) {
    return HAL_ERROR;
  }
  (void)HAL_RTC_GetDate(hrtc_p, &rtcDate, RTC_FORMAT_BIN);

  currSec = rtcTime.Seconds;
  newSec = currSec;
  {
    uint32_t tickStart = HAL_GetTick();
    while((newSec == currSec) && ((HAL_GetTick() - tickStart) < 200U)) {
      if(HAL_RTC_GetTime(hrtc_p, &rtcTime, RTC_FORMAT_BIN) != HAL_OK) {
        break;
      }
      (void)HAL_RTC_GetDate(hrtc_p, &rtcDate, RTC_FORMAT_BIN);
      newSec = rtcTime.Seconds;
    }
  }

  nextSec = (uint8_t)((newSec + UV_STOP_RTC_ALARM_STEP_SEC) % 60U);

  sAlarm.Alarm = RTC_ALARM_A;
  sAlarm.AlarmTime.Hours = 0U;
  sAlarm.AlarmTime.Minutes = 0U;
  sAlarm.AlarmTime.Seconds = nextSec;
  sAlarm.AlarmTime.SubSeconds = 0U;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
  sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY | RTC_ALARMMASK_HOURS | RTC_ALARMMASK_MINUTES;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1U;

#if DEBUG_ADC_VERBOSE
  DebugPrint("[UV] RTC arm currSec=");
  DebugPrintDec((uint16_t)newSec);
  DebugPrint(" nextSec=");
  DebugPrintDec((uint16_t)nextSec);
  DebugPrint("\r\n");
#endif

  return HAL_RTC_SetAlarm_IT(hrtc_p, &sAlarm, RTC_FORMAT_BIN);
}

/* 日/夜检测：读 PA0 太阳能板电压，滞回 + 持续 3s 确认后翻转 (规格书 §5)，积木2 */
static void DayNight_Update(void)
{
  uint32_t now = HAL_GetTick();
  if((now - g_last_daynight_tick) < DAYNIGHT_SAMPLE_INTERVAL_MS) {
    return;
  }
  g_last_daynight_tick = now;

  uint32_t raw = Read_ADC1_Channel(ADC_CHANNEL_0);
  if(raw == 0xFFFFU) {
    return;
  }
  uint16_t adc_val = (uint16_t)raw;
  uint8_t prev = g_is_night;

    if(adc_val < SOLAR_ADC_NIGHT_THRESHOLD) {
    /* 条件满足「夜间」：需持续 3s 才从白天切到夜间 */
    if(g_is_night) {
      g_daynight_hold_until_tick = 0;
    } else {
      if(g_daynight_hold_until_tick == 0) {
        g_daynight_hold_until_tick = now + DAYNIGHT_HOLD_MS;
      } else if(now >= g_daynight_hold_until_tick) {
        /* 检测到从白天切换到夜间，启动调试窗口 */
        uint8_t prev_night = g_is_night;
        g_is_night = 1;
        g_daynight_hold_until_tick = 0;
        
        /* 记录夜间开始时间，启动6秒调试窗口 */
        if(prev_night == 0) {  /* 从白天切换到夜间 */
          g_night_start_tick = now;
          g_night_debug_window = 1;
          DebugPrint("[NIGHT] Debug window started (6s)\r\n");
        }
      }
    }
  } else if(adc_val > SOLAR_ADC_DAY_THRESHOLD) {
    /* 条件满足「白天」：需持续 3s 才从夜间切到白天 */
    if(!g_is_night) {
      g_daynight_hold_until_tick = 0;
    } else {
      if(g_daynight_hold_until_tick == 0) {
        g_daynight_hold_until_tick = now + DAYNIGHT_HOLD_MS;
      } else if(now >= g_daynight_hold_until_tick) {
        g_is_night = 0;
        g_daynight_hold_until_tick = 0;
        
        /* 切换到白天，关闭调试窗口 */
        g_night_debug_window = 0;
      }
    }
  } else {
    /* 滞回中间区：取消翻转确认 */
    g_daynight_hold_until_tick = 0;
  }

#if DEBUG_ADC_VERBOSE
  if(prev != g_is_night) {
    DebugPrint(g_is_night ? "->Night\r\n" : "->Day\r\n");
  }
  
  /* 检查是否在夜间调试窗口内（夜间开始后6秒） */
  if(g_is_night && g_night_debug_window) {
    uint32_t now = HAL_GetTick();
        if(now - g_night_start_tick <= 6000U) {  /* 6秒窗口 */
      /* 在调试窗口内：使用扫描模式同时采样两个通道 */
      uint32_t solar_raw, batt_raw;
      if(Read_ADC1_DualChannel(&solar_raw, &batt_raw) == 0) {
        DebugPrint("[NIGHT-DBG] solar=");
        DebugPrintDec((uint16_t)solar_raw);
        DebugPrint("(");
        
        uint32_t solar_mv = (solar_raw * 3300) / 4095;
        DebugPrintDec((uint16_t)(solar_mv / 1000));
        DebugPrint(".");
        DebugPrintDec((uint16_t)((solar_mv % 1000) / 100));
        DebugPrintDec((uint16_t)((solar_mv % 100) / 10));
        
        DebugPrint("V) batt=");
        DebugPrintDec((uint16_t)batt_raw);
        DebugPrint("(");
        
        uint32_t batt_mv = (batt_raw * 3300) / 4095;
        DebugPrintDec((uint16_t)(batt_mv / 1000));
        DebugPrint(".");
        DebugPrintDec((uint16_t)((batt_mv % 1000) / 100));
        DebugPrintDec((uint16_t)((batt_mv % 100) / 10));
        
        DebugPrint("V)\r\n");
      }
    } else {
      /* 6秒窗口结束 */
      g_night_debug_window = 0;
      DebugPrint("[NIGHT] Debug window ended\r\n");
    }
  } else {
    /* 不在调试窗口：正常单通道采样显示 */
    static uint8_t dn_cnt = 0;
    if(++dn_cnt >= 5) {
      dn_cnt = 0;
      DebugPrint("[ADC] solar=");
      DebugPrintDec(adc_val);
      DebugPrint("(");
      
      uint32_t voltage_mv = (adc_val * 3300) / 4095;
      DebugPrintDec((uint16_t)(voltage_mv / 1000));
      DebugPrint(".");
      DebugPrintDec((uint16_t)((voltage_mv % 1000) / 100));
      DebugPrintDec((uint16_t)((voltage_mv % 100) / 10));
      
      DebugPrint("V) night=");
      DebugPrint(g_is_night ? "1" : "0");
      if(g_daynight_hold_until_tick != 0) DebugPrint(" hold");
      DebugPrint("\r\n");
    }
  }
#else
  if(prev != g_is_night) {
    DebugPrint(g_is_night ? "->Night\r\n" : "->Day\r\n");
  }
#endif
}

/* 充电控制：读 PA1 电池电压，过充 1.55V 关断、1.35V 滞回重开，积木3：太阳能预判+过充二次确认 */
static void Charge_Update(void)
{
  uint32_t now = HAL_GetTick();
  if((now - g_last_charge_tick) < CHARGE_SAMPLE_INTERVAL_MS) {
    return;
  }
  g_last_charge_tick = now;

  /* 仅在“有太阳能输入”时检查过充，夜间放电时跳过以减少 ADC 次数 */
  uint32_t solar_raw = Read_ADC1_Channel(ADC_CHANNEL_0);
  if(solar_raw == 0xFFFFU) return;
  if(solar_raw <= SOLAR_ADC_DAY_THRESHOLD) {
    return;  /* 太阳能电压不高，认为未在充电，跳过过充判断 */
  }

  uint32_t raw = Read_ADC1_Channel(ADC_CHANNEL_1);
  if(raw == 0xFFFFU) return;
  uint16_t adc_val = (uint16_t)raw;

  /* 过充二次确认：连续两次采样都 >= 阈值才关断，避免误触发 */
  static uint8_t overcharge_pending = 0;

  if(adc_val >= BATT_ADC_OVERCHARGE_RAW) {
    if(!overcharge_pending) {
      overcharge_pending = 1;
#if DEBUG_ADC_VERBOSE
      DebugPrint("[CHG] batt=");
      DebugPrintDec(adc_val);
      DebugPrint("(");
      
      /* 计算电池电压值（假设3.3V参考电压，12位ADC） */
      uint32_t batt_voltage_mv = (adc_val * 3300) / 4095;
      DebugPrintDec((uint16_t)(batt_voltage_mv / 1000));  /* 整数部分 */
      DebugPrint(".");
      DebugPrintDec((uint16_t)((batt_voltage_mv % 1000) / 100));  /* 小数第一位 */
      DebugPrintDec((uint16_t)((batt_voltage_mv % 100) / 10));    /* 小数第二位 */
      
      DebugPrint("V) pending\r\n");
#endif
      return;
    }
    overcharge_pending = 0;
    HAL_GPIO_WritePin(CHG_MOS_GPIO_Port, CHG_MOS_Pin, GPIO_PIN_SET);  /* 过充：高电平关断 */
#if DEBUG_ADC_VERBOSE
    DebugPrint("[CHG] overcharge OFF batt=");
    DebugPrintDec(adc_val);
    DebugPrint("(");
    
    /* 计算电池电压值 */
    uint32_t batt_voltage_mv = (adc_val * 3300) / 4095;
    DebugPrintDec((uint16_t)(batt_voltage_mv / 1000));
    DebugPrint(".");
    DebugPrintDec((uint16_t)((batt_voltage_mv % 1000) / 100));
    DebugPrintDec((uint16_t)((batt_voltage_mv % 100) / 10));
    
    DebugPrint("V)\r\n");
#endif
  } else if(adc_val <= BATT_ADC_REENABLE_RAW) {
    overcharge_pending = 0;
    HAL_GPIO_WritePin(CHG_MOS_GPIO_Port, CHG_MOS_Pin, GPIO_PIN_RESET);  /* 滞回：重新开启充电 */
#if DEBUG_ADC_VERBOSE
    DebugPrint("[CHG] reenable ON batt=");
    DebugPrintDec(adc_val);
    DebugPrint("(");
    
    /* 计算电池电压值 */
    uint32_t batt_voltage_mv = (adc_val * 3300) / 4095;
    DebugPrintDec((uint16_t)(batt_voltage_mv / 1000));
    DebugPrint(".");
    DebugPrintDec((uint16_t)((batt_voltage_mv % 1000) / 100));
    DebugPrintDec((uint16_t)((batt_voltage_mv % 100) / 10));
    
    DebugPrint("V)\r\n");
#endif
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
