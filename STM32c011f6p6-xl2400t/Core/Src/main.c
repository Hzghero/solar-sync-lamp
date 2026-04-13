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
#define SYNC_LED_ON_MS      60U    /* LED 亮灯时间 100ms */
#define SYNC_TX_TIME_MS     450U    /* TX 发送时间：所有节点固定 450ms */

/* 实验2：仅用普通IO LED指示同步，不启动PWM升压驱动 */
#define EXP2_GPIO_LED_ONLY  0U
#define SYNC_TX_DELAY_MS    6U      /* 传输延迟补偿 */
#define SYNC_PKT_SIZE       4U      /* AA 55 + 2字节相位 */
#define RF_TX_CHANNEL       76U     /* XL2400T TX 频道 76 (2476 MHz) */
#define RF_RX_CHANNEL       75U     /* XL2400T RX 频道 75 (2475 MHz) - 相邻频道避免自干扰 */

/* ======================= 联调宏集中区（优先改这里） =======================
 * 说明：以下为最常改参数，集中到前部方便调试。
 * 其余业务参数保持原位置以降低改动风险。
 * RF底层发送等待宏在：Core/Inc/XL2400T.h
 */
/* 常用组合：
 *   TX_ONLY=0, RX_ONLY=0 -> 正常双向
 *   TX_ONLY=1, RX_ONLY=0 -> 发射端测试
 *   TX_ONLY=0, RX_ONLY=1 -> 接收端测试
 */
#define SYNC_TEST_FORCE_TX_ONLY      0U
#define SYNC_TEST_FORCE_RX_ONLY      0U

#if (SYNC_TEST_FORCE_TX_ONLY && SYNC_TEST_FORCE_RX_ONLY)
#error "SYNC_TEST_FORCE_TX_ONLY and SYNC_TEST_FORCE_RX_ONLY cannot both be 1"
#endif

/* 观测：原始收包计数（raw）与有效包计数（valid）
 * 0 = 关闭 [RXCNT] 打印
 * 1 = 开启 [RXCNT] 打印（联调用）
 */
#define LOG_SYNC_RX_COUNTER_ENABLE   1U

/* 实验：是否禁用省电策略（常开RX）
 * 0 = 正常省电策略（ACQ/LOCK/FSCAN 生效）
 * 1 = 禁用省电策略（强制常开RX，便于定位）
 */
#define SYNC_TEST_DISABLE_POWER_SAVE           0U

/* 实验：本周期收包后是否禁止本周期再发
 * 0 = 关闭（按原逻辑，到时就发）
 * 1 = 开启（若本周期已收有效包，则本周期不再发）
 */
#define SYNC_TEST_SKIP_TX_IF_VALID_THIS_CYCLE  0U

/* Step1 最小日志（降低串口阻塞）
 * 0 = 关闭（使用各 LOG_* 宏当前设置）
 * 1 = 开启（强制关闭大部分高频日志，仅保留必要日志）
 */
#define SYNC_TEST_MIN_LOGS                     0U

/* Step4 固定 TX 偏移（破对称），单位 ms，可正可负
 * 0  = 无偏移（默认）
 * >0 = TX 时刻后移；<0 = TX 时刻前移
 * 示例：节点A=0，节点B=13
 */
#define SYNC_TEST_TX_OFFSET_MS                 0

/* 电池欠压阈值档位（集中调参）
 * 0 = 0.75V
 * 1 = 0.80V
 * 2 = 0.90V（默认）
 */
#define BATT_UV_LEVEL_SELECT                   2U
/* ======================================================================= */

/* 同步省电策略参数：锁定后跨周期打开接收，失锁后回退连续接收 */
#define SYNC_RX_EVERY_N_CYCLES         4U   /* 锁定后每4个周期开1次接收 */
#define SYNC_LOCK_ERR_TH_MS           20U   /* 锁定判据：相位误差阈值 */
#define SYNC_LOCK_NEED_GOOD_COUNT      5U   /* 常规模式：连续良好次数达到后进入锁定态 */
#define SYNC_UNLOCK_ERR_TH_MS         60U   /* 失锁判据：相位误差阈值 */
#define SYNC_UNLOCK_NEED_BAD_COUNT     2U   /* 连续坏包次数达到后回退捕获态 */
#define SYNC_UNLOCK_MISS_COUNT         2U   /* 计划接收周期连续丢包次数达到后回退捕获态 */
#define SYNC_LOCK_ASSUME_NO_RX_CYCLES  6U   /* 锁定后允许短期无包（对撞场景） */

/* 对撞保持策略：锁定后若长期收不到包，仍维持“稀疏接收”而不是退回 ACQUIRE
 * 目的：在双方高度同步且易对撞时，继续获得省电收益（先跑效果，再逐步收敛）
 */
#define SYNC_COLLISION_HOLD_ENABLE      1U   /* 1=启用对撞保持，0=按原逻辑 miss 超时后退回 ACQUIRE */
#define SYNC_COLLISION_HOLD_MIN_NO_RX   5U   /* 连续无包达到该值后，miss 超时时继续保持 LOCKED_SPARSE */

/* 快速入锁策略（用于“先跑起来看省电效果”）：
 * 1=启用：ACQ态只要收到 1 次有效好包就进入 LOCKED_SPARSE
 * 0=关闭：使用常规 SYNC_LOCK_NEED_GOOD_COUNT
 */
#define SYNC_FAST_LOCK_ENABLE          1U
#define SYNC_FAST_LOCK_GOOD_COUNT      1U

/* 强制省电巡检策略（上电后可在“无包”场景下自动进入关RX巡检循环）
 * 流程：
 * 1) 上电先连续开 RX SYNC_BOOT_ACQ_CYCLES 个周期，尝试快速捕获
 * 2) 若始终无有效包，则进入“关RX-SLEEP + 开RX-PROBE”循环
 *    - 关RX: SYNC_FORCED_SLEEP_CYCLES 个周期
 *    - 开RX探测: SYNC_FORCED_PROBE_RX_CYCLES 个周期
 * 3) 探测窗口任一周期收到有效包，立即退出强制巡检，回到 ACQUIRE 收敛
 */
#define SYNC_FORCED_SCAN_ENABLE            1U
#define SYNC_BOOT_ACQ_CYCLES               5U
#define SYNC_FORCED_SLEEP_CYCLES          15U   /* 温和版：关RX窗口缩短，减少“长时间错开”体感 */
#define SYNC_FORCED_PROBE_RX_CYCLES        5U   /* 探测窗口连续开RX周期数（增强命中概率） */
#define SYNC_FORCED_PROBE_MISS_ROUNDS_TO_SLEEP 2U /* 连续探测失败多少轮后才回到关RX窗口 */

/* TX 偏移探测开关（用于打破长期对撞）
 * - 0: 关闭偏移，TX 固定在 SYNC_TX_TIME_MS
 * - 1: 开启偏移；偏移量由 SYNC_TX_DITHER_OFFSET_MS 指定
 * 若只想在“强制巡检的 PROBE 窗口”使用偏移，可把 SYNC_TX_DITHER_PROBE_ONLY 设为 1。
 */
#define SYNC_TX_DITHER_ENABLE              1U
#define SYNC_TX_DITHER_OFFSET_MS           6U   /* 兼容旧逻辑的基础偏移 */
#define SYNC_TX_DITHER_PROBE_ONLY          1U

/* 探测窗口递进偏移：用于打破“固定相位互撞”的死角
 * 偏移序列：20/25/30/35/40ms，命中有效包后回到 20ms 起点。
 */
#define SYNC_TX_DITHER_STEP_MS             4U
#define SYNC_TX_DITHER_MAX_MS              22U

/* 分层恢复参数（先协议自救，后底层恢复）
 * 1) LOCK 长时间无包 -> 强制回 ACQUIRE 连续开 RX 一段时间
 * 2) 若多轮自救仍失败且存在底层异常证据 -> 触发 RF_Link_Init()
 */
#define SYNC_LOCK_NO_RX_FORCE_ACQ_TH      24U   /* LOCK 连续无包阈值（按“计划接收周期”计） */
#define SYNC_FORCE_ACQ_RX_CYCLES           8U   /* 强制 ACQUIRE 连续开 RX 周期数 */
#define SYNC_RECOVER_FAIL_ROUNDS_TH        2U   /* 至少完成多少轮协议自救仍失败 */
#define SYNC_RECOVER_NO_VALID_MS        60000U  /* 全局无有效包时长阈值（ms） */
#define SYNC_RECOVER_COOLDOWN_MS       120000U  /* RF 恢复冷却时间（ms） */
#define SYNC_RECOVER_MAX_PER_HOUR         3U    /* 每小时最多触发 RF 恢复次数 */
#define SYNC_RECOVER_HOUR_MS         3600000UL

/* 底层异常证据阈值（任一命中即可作为“异常证据”） */
#define RF_ERR_STREAK_TH                   3U

/* ADC 参考电压一键切换：
 * 0 = 3.3V（默认）
 * 1 = 3.0V
 */
#define ADC_REF_SELECT_3V0               0U
#if ADC_REF_SELECT_3V0
#define ADC_REF_MV                    3000U
#else
#define ADC_REF_MV                    3300U
#endif
#define ADC_MAX_COUNT                 4095U
#define ADC_RAW_FROM_MV(mv) ((uint16_t)((((uint32_t)(mv)) * ADC_MAX_COUNT + (ADC_REF_MV / 2U)) / ADC_REF_MV))
#define ADC_MV_FROM_RAW(raw) ((uint32_t)((((uint32_t)(raw)) * ADC_REF_MV) / ADC_MAX_COUNT))

/* 阈值按“实际电压(mV)”定义，RAW 随 ADC_REF_MV 自动换算 */
#define SOLAR_NIGHT_THRESHOLD_MV      290U
#define SOLAR_DAY_THRESHOLD_MV        400U
#define SOLAR_ADC_NIGHT_THRESHOLD     ADC_RAW_FROM_MV(SOLAR_NIGHT_THRESHOLD_MV)
#define SOLAR_ADC_DAY_THRESHOLD       ADC_RAW_FROM_MV(SOLAR_DAY_THRESHOLD_MV)
#define DAYNIGHT_SAMPLE_INTERVAL_MS  1000U
#define DAYNIGHT_HOLD_MS              0U

#define BATT_OVERCHARGE_MV           1550U
#define BATT_REENABLE_MV             1350U
#define BATT_ADC_OVERCHARGE_RAW      ADC_RAW_FROM_MV(BATT_OVERCHARGE_MV)
#define BATT_ADC_REENABLE_RAW        ADC_RAW_FROM_MV(BATT_REENABLE_MV)
#define CHARGE_SAMPLE_INTERVAL_MS    3000U

/* 电池 ~0.9V 提前停载（与 ME2188 约 0.7V 硬截止、回升约 0.9V 再配合） */
#if (BATT_UV_LEVEL_SELECT == 0U)
#define BATT_UNDERVOLT_MV             750U
#define BATT_UNDERVOLT_RECOVER_MV     830U
#elif (BATT_UV_LEVEL_SELECT == 1U)
#define BATT_UNDERVOLT_MV             800U
#define BATT_UNDERVOLT_RECOVER_MV     880U
#elif (BATT_UV_LEVEL_SELECT == 2U)
#define BATT_UNDERVOLT_MV             900U
#define BATT_UNDERVOLT_RECOVER_MV     980U
#else
#error "Invalid BATT_UV_LEVEL_SELECT, use 0/1/2"
#endif
#define BATT_ADC_UNDERVOLT_RAW       ADC_RAW_FROM_MV(BATT_UNDERVOLT_MV)
#define BATT_ADC_UNDERVOLT_RECOVER_RAW ADC_RAW_FROM_MV(BATT_UNDERVOLT_RECOVER_MV)
#define BATT_UV_SAMPLE_MS             500U   /* 两次确认间隔 */

#define UV_STOP_RTC_ALARM_STEP_SEC   5U    /* 欠压 STOP 期间 RTC Alarm A 周期间隔（秒） */

/* Debug: 在进入 Standby 前留出时间观察 RF 睡眠后电流回落情况 */
#define UV_RF_SLEEP_MEASURE_BEFORE_STANDBY_MS 3000U

/* 欠压低功耗“证明实验”开关：
 * 0=关闭（走正常逻辑）
 * 1=用 HAL_Delay 忙等 UV_RF_SLEEP_MEASURE_BEFORE_STANDBY_MS（CPU 运行态）
 * 2=用 WFI(SLEEP) 等待 UV_RF_SLEEP_MEASURE_BEFORE_STANDBY_MS（浅睡眠态）
 *
 * 说明：用于解释“为什么等待窗口电流较高，但进入 Standby 瞬间掉到 µA”。
 * 测完把它改回 0 即可复原。
 */
#define UV_RF_PROOF_MODE 0U

#define DEBUG_UART_ENABLE 1   /* 1=初始化USART1；0=不初始化USART1(并将PA9/PA10设为模拟输入降漏电) */
#define DEBUG_UART_PRINT  1   /* 1=允许串口打印；0=所有 DebugPrint*() 直接return，不发送任何字节 */

/* 串口日志总开关与分组开关（便于按需开启/关闭）
 * 说明：
 * - 以下宏仅控制“打印内容”，不影响功能逻辑。
 * - 想恢复某类日志时，把对应宏改为 1 即可。
 */
#define LOG_FW_VERSION_ENABLE       1   /* 固件版本号打印（建议始终保持 1） */
#define LOG_BOOT_INFO_ENABLE        0   /* 启动/电源流程日志（PWR/UV/RF Init/TX固定窗口） */
#define LOG_SYNC_SCHEDULE_ENABLE    1   /* 每周期调度日志：[SCH]，用于观察 RX ON/OFF */
#define LOG_SYNC_LOCK_DIAG_ENABLE   1   /* 锁定诊断日志：[DIAG] good/bad/miss/no_rx/state */
#define LOG_SYNC_COLLISION_HOLD_ENABLE 0 /* 对撞保持事件日志：[SYNC] COLLISION-HOLD...（测流建议关） */
#define LOG_SYNC_RXTX_VERBOSE       1   /* 同步细节日志：[TX]/RX/[ADJ]/[CYCLE]（较多，测流时建议关） */
#define LOG_SYNC_OBSERVE_ENABLE     1   /* 观测增强总开关：1=打印观测日志，0=关闭新增观测日志（不影响功能） */
#define LOG_SYNC_OBS_DITHER_ENABLE  1   /* 递进偏移观测日志：[OBS][DITHER]（建议联调开、量产可关） */
#define LOG_SYNC_OBS_RESCUE_ENABLE  1   /* 协议自救观测日志：[OBS][RESCUE] 触发判据与计数 */
#define LOG_SYNC_OBS_RECOVER_ENABLE 1   /* RF恢复观测日志：[OBS][RECOVER] 触发判据与每小时次数 */
#define LOG_LED_VERBOSE_ENABLE      0   /* LED 详细日志 */
#define LOG_ADC_VERBOSE_ENABLE      0   /* ADC/充电/欠压详细日志（较多，测流时建议关） */

/* 白天低功耗策略：STOP + RTC 5s 周期唤醒 + PA0 EXTI 立刻唤醒 */
#define DAY_STOP_RTC_ALARM_STEP_SEC 5U

/* 白天 ADC 日夜判断打印频率：
 * dn_cnt >= DAY_ADC_PRINT_EVERY_N 时才打印一次 [ADC] solar=... night=...
 * dn_cnt 计数发生在 DayNight_Update 的“非调试窗口”分支里。
 */
#define DAY_ADC_PRINT_EVERY_N 1U

/* 白天 [ADC] 打印是否附带电池电压（PA1/CH1）：
 * 0=只打印 solar（默认，最低开销）
 * 1=每次打印 solar 时额外采样 batt 并在同一行打印 batt=...
 */
#define DAY_ADC_PRINT_WITH_BATT 1U

/* 日夜翻转防误触发确认：在“准备翻转”的瞬间，额外开一个短确认窗口做多次采样。
 * - DAYNIGHT_FALSE_TRIG_CONFIRM_WINDOW_MS：确认窗口时间（默认 1000ms）
 * - DAYNIGHT_FALSE_TRIG_CONFIRM_MIN_HITS：在窗口内至少命中阈值的次数
 * - DAYNIGHT_FALSE_TRIG_CONFIRM_MAX_READS：窗口内最多采样次数（上限，避免阻塞过久）
 *
 * 注意：Read_ADC1_Channel 内部会有少量延时，因此这些参数会直接影响一次翻转判定的耗时。
 */
#define DAYNIGHT_FALSE_TRIG_SUPPRESS_ENABLE       1U
#define DAYNIGHT_FALSE_TRIG_CONFIRM_WINDOW_MS  1000U
#define DAYNIGHT_FALSE_TRIG_CONFIRM_MIN_HITS      2U
#define DAYNIGHT_FALSE_TRIG_CONFIRM_MAX_READS     4U
#define DEBUG_ADC_VERBOSE  LOG_ADC_VERBOSE_ENABLE   /* 兼容旧宏：ADC 详细日志 */
#define DEBUG_SYNC_VERBOSE LOG_SYNC_RXTX_VERBOSE    /* 兼容旧宏：同步细节日志 */
#define DEBUG_LED_VERBOSE  LOG_LED_VERBOSE_ENABLE   /* 兼容旧宏：LED 详细日志 */
#define DEBUG_PERIODIC     0   /* 调试：1=启用定期打印（影响功耗），0=禁用 */

/* 同步调度可观测性：每 N 个新周期打印一次“本周期是否开 RX” */
#define SYNC_SCHEDULE_PRINT_EVERY_N 1U

#if SYNC_TEST_MIN_LOGS
#undef LOG_BOOT_INFO_ENABLE
#undef LOG_SYNC_SCHEDULE_ENABLE
#undef LOG_SYNC_LOCK_DIAG_ENABLE
#undef LOG_SYNC_COLLISION_HOLD_ENABLE
#undef LOG_SYNC_RXTX_VERBOSE
#undef LOG_SYNC_OBSERVE_ENABLE
#undef LOG_SYNC_OBS_DITHER_ENABLE
#undef LOG_SYNC_OBS_RESCUE_ENABLE
#undef LOG_SYNC_OBS_RECOVER_ENABLE
#undef LOG_LED_VERBOSE_ENABLE
#undef LOG_ADC_VERBOSE_ENABLE
#define LOG_BOOT_INFO_ENABLE        0
#define LOG_SYNC_SCHEDULE_ENABLE    0
#define LOG_SYNC_LOCK_DIAG_ENABLE   0
#define LOG_SYNC_COLLISION_HOLD_ENABLE 0
#define LOG_SYNC_RXTX_VERBOSE       0
#define LOG_SYNC_OBSERVE_ENABLE     0
#define LOG_SYNC_OBS_DITHER_ENABLE  0
#define LOG_SYNC_OBS_RESCUE_ENABLE  0
#define LOG_SYNC_OBS_RECOVER_ENABLE 0
#define LOG_LED_VERBOSE_ENABLE      0
#define LOG_ADC_VERBOSE_ENABLE      0
#endif

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

typedef enum {
  SYNC_STATE_ACQUIRE = 0,      /* 捕获态：连续接收 */
  SYNC_STATE_LOCKED_SPARSE = 1 /* 锁定态：跨周期接收 */
} SyncState_t;

static uint32_t g_cycle = 0;
static uint16_t g_phase_ms = 0;
static uint32_t g_last_tick_ms = 0;
static uint32_t g_last_tx_cycle = (uint32_t)-1;

static uint8_t  g_rf_mode = 0;
static uint8_t  g_led_state = 0;
static uint32_t g_led_on_tick = 0;

static SyncState_t g_sync_state = SYNC_STATE_ACQUIRE;
static uint8_t g_sync_good_count = 0;
static uint8_t g_sync_bad_count = 0;
static uint8_t g_sync_miss_count = 0;
static uint8_t g_sync_sparse_counter = 0;
static uint8_t g_sync_no_rx_keep_count = 0;
static uint32_t g_sync_last_sched_cycle = (uint32_t)-1;
static uint8_t g_sync_rx_open_this_cycle = 1;
static uint32_t g_sync_last_collision_hold_cycle = (uint32_t)-1; /* 防止同一周期重复打印 COLLISION-HOLD */

/* 强制省电巡检状态变量 */
static uint8_t g_sync_forced_scan_mode = 0;   /* 0=正常状态机；1=强制巡检模式 */
static uint8_t g_sync_probe_window_mode = 0;  /* 强制巡检子态：0=关RX窗口，1=开RX探测窗口 */
static uint8_t g_sync_boot_acq_counter = 0;   /* 上电后 ACQUIRE 连续开RX计数 */
static uint8_t g_sync_forced_sleep_counter = 0; /* 关RX窗口计数 */
static uint8_t g_sync_forced_probe_counter = 0; /* 开RX探测计数 */
static uint8_t g_sync_forced_probe_miss_rounds = 0; /* 探测失败轮次计数（轮=连续 SYNC_FORCED_PROBE_RX_CYCLES 周期） */
static uint8_t g_sync_forced_probe_hit_in_round = 0; /* 本轮探测窗口内是否至少收到过1次有效包 */
static uint8_t g_sync_dither_idx = 0;                /* 递进偏移索引：0..4 => 20/25/30/35/40ms */
static uint8_t g_sync_force_acq_cycles_left = 0;     /* 协议自救：强制 ACQUIRE 连续 RX 剩余周期 */
static uint8_t g_sync_recover_fail_rounds = 0;       /* 协议自救失败轮次计数 */
static uint32_t g_sync_rescue_count_total = 0;       /* 观测计数：协议自救总触发次数（仅统计） */

/* RF 底层健康统计：仅用于“是否需要底层恢复”的判据，不直接影响正常收发流程 */
static uint32_t g_last_valid_rx_tick = 0;            /* 最近一次收到有效同步包的时间戳 */
static uint8_t g_rf_cfg_rx_fail_streak = 0;
static uint8_t g_rf_cfg_tx_fail_streak = 0;
static uint8_t g_rf_send_fail_streak = 0;
static uint8_t g_rf_poll_err_streak = 0;             /* 预留：当前接口无显式错误码，先保持0 */
static uint32_t g_rf_recover_count_total = 0;
static uint32_t g_rf_recover_hour_window_start = 0;
static uint8_t g_rf_recover_count_this_hour = 0;
static uint32_t g_rf_recover_cooldown_until = 0;

static uint32_t g_sync_rx_raw_count = 0;
static uint32_t g_sync_rx_valid_count = 0;
static uint32_t g_sync_rx_counter_last_cycle = (uint32_t)-1;
static uint32_t g_sync_last_valid_rx_cycle = (uint32_t)-1;

static uint8_t  g_is_night = 1;
static uint32_t g_last_daynight_tick = 0;
static uint32_t g_daynight_hold_until_tick = 0;

static uint32_t g_last_charge_tick = 0;

static uint8_t  g_rf_sleeping = 0;
static uint8_t  g_rf_sleeping_night = 0;
static uint8_t  g_rf_initialized = 0;
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
static void Undervolt_EnterStandby(void);
static void Undervolt_PrepareStandbyIO(void);
static void Undervolt_StandbyHoldRfPins(void);
static void Undervolt_Wait_WFI_Ms(uint32_t ms);
static HAL_StatusTypeDef Undervolt_RTC_SetNextAlarm(RTC_HandleTypeDef *hrtc);
static HAL_StatusTypeDef Day_RTC_SetNextAlarm(RTC_HandleTypeDef *hrtc_p);
static void Day_PrepareStopWakeSources(void);
static void Day_RestoreFromStopWake(void);
static uint32_t Read_ADC1_Channel(uint32_t channel);  /* 指定通道读 ADC1 一次，PA0=ch0 太阳能，PA1=ch1 电池 */
static void Test_ADC_Channels(void);                   /* ADC通道切换测试函数 */
static uint8_t Read_ADC1_DualChannel(uint32_t* solar_raw, uint32_t* batt_raw);  /* 扫描模式读取双通道 */
static uint8_t DayNight_ConfirmTransition(uint8_t toNight);
static void RF_Recovery_Check(void);
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
#if DEBUG_UART_ENABLE
  MX_USART1_UART_Init();
#else
  /* 不启用 USART：把 PA9/PA10 设为模拟输入，避免浮空/复用导致漏电 */
  Undervolt_SetUartPinsAnalog();
#endif
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
#if LOG_FW_VERSION_ENABLE
  DebugPrint("[FW] " FW_VERSION "\r\n");
#endif

  /* 欠压 Standby 唤醒后（或直接上电欠压）快速重入判定：
   *  - 在不启用升压/无线的前提下，尽快决定是否进入 Standby
   *  - 避免“刚唤醒就先把 BOOST/RF 打开导致功耗抖动/不再进入低功耗”
   */
  {
    uint32_t sb_flag = __HAL_PWR_GET_FLAG(PWR_FLAG_SB);
    uint32_t wuf1_flag = __HAL_PWR_GET_FLAG(PWR_FLAG_WUF1);
#if LOG_BOOT_INFO_ENABLE
    if(sb_flag != 0U) {
      DebugPrint("[PWR] Standby flag set\r\n");
    }
    if(wuf1_flag != 0U) {
      DebugPrint("[PWR] Wake from WKUP1\r\n");
    }
#endif
    /* 清掉唤醒标志，避免后续再次进入 Standby 时误判/立即唤醒 */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF1);
    if(sb_flag != 0U) {
      __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    }

    uint32_t br_boot = Read_ADC1_Channel(ADC_CHANNEL_1);
    if(br_boot != 0xFFFFU && ((uint16_t)br_boot) <= BATT_ADC_UNDERVOLT_RAW) {
#if LOG_BOOT_INFO_ENABLE
      DebugPrint("[UV] Boot undervolt -> enter STANDBY\r\n");
#endif
      Undervolt_EnterStandby();
    }
  }

  /* 启用升压电路供电 */
  HAL_Delay(100);
  HAL_GPIO_WritePin(BOOST_EN_GPIO_Port, BOOST_EN_Pin, GPIO_PIN_SET);



    /* RF 模块初始化 */
#if LOG_BOOT_INFO_ENABLE
  DebugPrint("RF Init...\r\n");
#endif
  RF_Link_Init();
#if LOG_BOOT_INFO_ENABLE
  DebugPrint("RF Init OK\r\n");
#endif
  g_rf_initialized = 1;

    /* 默认进入 RX 模式，监听频道 75 */
#if LOG_BOOT_INFO_ENABLE
  DebugPrint("RF Config RX...\r\n");
#endif
  RF_Link_ConfigRx(RF_RX_CHANNEL);
  g_rf_mode = 0;
#if LOG_BOOT_INFO_ENABLE
  DebugPrint("RF Ready\r\n");
  
  /* 调试：确认TX固定时间 */
  DebugPrint("[SYNC] TX fixed at ");
  DebugPrintDec(SYNC_TX_TIME_MS);
  DebugPrint("ms, Window: ");
  DebugPrintDec(SYNC_TX_TIME_MS);
  DebugPrint("- ");
  DebugPrintDec(SYNC_TX_TIME_MS + 50U);
  DebugPrint("ms\r\n");
#endif





  /* 初始化同步数据 */
  g_cycle = 0;
  g_phase_ms = 0;
  g_last_tick_ms = HAL_GetTick();
  g_last_tx_cycle = (uint32_t)-1;

  g_led_state = 0;
  g_led_on_tick = 0;

  /* 同步状态机初始化：默认捕获态（连续接收） */
  g_sync_state = SYNC_STATE_ACQUIRE;
  g_sync_good_count = 0;
  g_sync_bad_count = 0;
  g_sync_miss_count = 0;
  g_sync_sparse_counter = 0;
  g_sync_no_rx_keep_count = 0;
  g_sync_last_sched_cycle = (uint32_t)-1;
  g_sync_rx_open_this_cycle = 1;
  g_sync_dither_idx = 0;
  g_sync_force_acq_cycles_left = 0;
  g_sync_recover_fail_rounds = 0;
  g_sync_rescue_count_total = 0;

  g_last_valid_rx_tick = HAL_GetTick();
  g_rf_cfg_rx_fail_streak = 0;
  g_rf_cfg_tx_fail_streak = 0;
  g_rf_send_fail_streak = 0;
  g_rf_poll_err_streak = 0;
  g_rf_recover_count_total = 0;
  g_rf_recover_hour_window_start = HAL_GetTick();
  g_rf_recover_count_this_hour = 0;
  g_rf_recover_cooldown_until = 0;

  g_sync_rx_raw_count = 0;
  g_sync_rx_valid_count = 0;
  g_sync_rx_counter_last_cycle = (uint32_t)-1;
  g_sync_last_valid_rx_cycle = (uint32_t)-1;

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
        uint32_t solar_mv = ADC_MV_FROM_RAW(solar_raw);
        DebugPrintDec((uint16_t)(solar_mv / 1000));
        DebugPrint(".");
        DebugPrintDec((uint16_t)((solar_mv % 1000) / 100));
        DebugPrintDec((uint16_t)((solar_mv % 100) / 10));
        DebugPrint("V) batt=");
        
        DebugPrintDec((uint16_t)batt_raw);
        DebugPrint("(");
        
        /* 计算电池电压 */
        uint32_t batt_mv = ADC_MV_FROM_RAW(batt_raw);
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

    /* 欠压：停载、CHG_MOS 拉低；进入 STANDBY，由 WKUP1(PA0) 唤醒退出并重新判断欠压 */
    if(g_batt_undervolt) {
      DebugPrint("[UV] undervolt -> enter STANDBY (WKUP1=PA0)\r\n");
      Undervolt_EnterStandby();
      /* Standby exit 等价于复位流程，理想情况下不在此处继续运行 */
      g_batt_undervolt = 0;
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

          /* 夜间重新开始时，重置同步省电状态机，先连续接收确保快速重同步 */
          g_sync_state = SYNC_STATE_ACQUIRE;
          g_sync_good_count = 0;
          g_sync_bad_count = 0;
          g_sync_miss_count = 0;
          g_sync_sparse_counter = 0;
          g_sync_no_rx_keep_count = 0;
          g_sync_last_sched_cycle = (uint32_t)-1;
          g_sync_rx_open_this_cycle = 1;
          g_sync_dither_idx = 0;
          g_sync_force_acq_cycles_left = 0;
          g_sync_recover_fail_rounds = 0;
          g_last_valid_rx_tick = HAL_GetTick();
          DebugPrint("[SYNC] Night enter -> ACQUIRE\r\n");
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
      RF_Recovery_Check();
      SyncLamp_Update();
      HAL_Delay(1);
    } else {
      /* 白天：关灯，不跑同步 */
#if !EXP2_GPIO_LED_ONLY
      HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
#endif
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

      if(!g_rf_sleeping) {
        RF_Link_Sleep();
        g_rf_sleeping = 1;
      }

      /* 白天改为 STOP 模式：
       * - RTC 每 5 秒唤醒一次做日夜/充电/欠压判断
       * - PA0 EXTI 上升沿可立刻唤醒（太阳能快速变化时更灵敏）
       * - 进入 STOP 前暂停 SysTick，避免 1ms 频繁唤醒导致白天电流偏大
       */
      Day_PrepareStopWakeSources();
      if(Day_RTC_SetNextAlarm(&hrtc) != HAL_OK) {
        DebugPrint("[DAY] RTC arm failed\r\n");
      }

      HAL_PWREx_EnableFlashPowerDown(PWR_FLASHPD_STOP);
      HAL_SuspendTick();
      HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
      SystemClock_Config();
      PeriphCommonClock_Config();

      /* STOP 后时钟链路被重配，SysTick reload 可能需要重新初始化，
       * 否则 HAL_GetTick()/DayNight_Update 的 1s 节流逻辑会被错误节流。
       */
      if(HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK) {
        Error_Handler();
      }
      HAL_ResumeTick();

      /* 对齐日夜采样节流计时基准 */
      uint32_t _dayWakeTick = HAL_GetTick();
      /* 让 DayNight_Update 这次 wake 后立刻可采样，避免 HAL_GetTick 节拍/重配误差导致“额外等待一轮” */
      g_last_daynight_tick = (_dayWakeTick > DAYNIGHT_SAMPLE_INTERVAL_MS) ? (_dayWakeTick - DAYNIGHT_SAMPLE_INTERVAL_MS) : 0U;
      Day_RestoreFromStopWake();
      HAL_PWREx_DisableFlashPowerDown(PWR_FLASHPD_STOP);
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
  if(!DEBUG_UART_PRINT || !DEBUG_UART_ENABLE) {
    return;
  }
  HAL_UART_Transmit(&huart1, (uint8_t*)s, strlen(s), 100);
}

static void DebugPrintHex(const uint8_t *buf, uint8_t len)
{
  if(!DEBUG_UART_PRINT || !DEBUG_UART_ENABLE) {
    return;
  }
  char hex[3];
  uint8_t i;
  for(i = 0; i < len; i++) {
    sprintf(hex, "%02X", buf[i]);
    HAL_UART_Transmit(&huart1, (uint8_t*)hex, 2, 100);
  }
}

static void DebugPrintDec(uint16_t val)
{
  if(!DEBUG_UART_PRINT || !DEBUG_UART_ENABLE) {
    return;
  }
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

        /* 周期边界：跨越 0ms 时立即点亮 LED（实验2可仅用普通IO，不启PWM） */
  if(new_cycles > 0) {
#if !EXP2_GPIO_LED_ONLY
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
#endif
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
#if !EXP2_GPIO_LED_ONLY
      HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
#endif
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
 * @brief  同步主状态机：固定TX + 锁定后跨周期接收（省电）
 *         - ACQUIRE：连续接收，快速重同步
 *         - LOCKED_SPARSE：每 N 周期开一次接收，失锁后回退 ACQUIRE
 */
static void Sync_MainLoop(void)
{
  uint8_t rx_got_valid = 0;
  int16_t phase_diff = 0;
  uint16_t tx_time_ms = SYNC_TX_TIME_MS;
  uint8_t fscan_schedule_handled = 0U;

  /* 更新本地时间基准（增量式） */
  SyncTime_Update();
  /* 驱动 LED 指示灯（100ms 保持亮，然后熄灭） */
  SyncLamp_Update();

  /* 每个新周期只调度一次“本周期是否开启 RX” */
  if(g_sync_last_sched_cycle != g_cycle) {
    g_sync_last_sched_cycle = g_cycle;

#if SYNC_FORCED_SCAN_ENABLE
    /* 上电初期：先连续开 RX 若干周期，尽快捕获同步包 */
    if((g_sync_forced_scan_mode == 0U) && (g_sync_state == SYNC_STATE_ACQUIRE) && (g_sync_boot_acq_counter < SYNC_BOOT_ACQ_CYCLES)) {
      g_sync_boot_acq_counter++;
      g_sync_rx_open_this_cycle = 1;
      g_sync_sparse_counter = 0;
    }
    /* 若上电捕获窗口结束且仍未收到有效包，进入强制巡检 */
    else if((g_sync_forced_scan_mode == 0U) && (g_sync_state == SYNC_STATE_ACQUIRE) && (g_sync_boot_acq_counter >= SYNC_BOOT_ACQ_CYCLES) && (g_sync_good_count == 0U)) {
      g_sync_forced_scan_mode = 1U;
      g_sync_probe_window_mode = 0U;
      g_sync_forced_sleep_counter = 0U;
      g_sync_forced_probe_counter = 0U;
      g_sync_forced_probe_miss_rounds = 0U;
      g_sync_forced_probe_hit_in_round = 0U;
      g_sync_rx_open_this_cycle = 0;
      g_sync_sparse_counter = 0;
      g_sync_state = SYNC_STATE_LOCKED_SPARSE; /* 复用稀疏省电行为：不回 ACQ 常开RX */
      g_sync_good_count = 0;
      g_sync_bad_count = 0;
      g_sync_no_rx_keep_count = 0;
      g_sync_miss_count = 0;
      DebugPrint("[SYNC] FORCED-SCAN enter (boot no-rx)\r\n");
    }
    /* 强制巡检主循环：SLEEP窗口与PROBE窗口交替 */
    else if(g_sync_forced_scan_mode == 1U) {
      fscan_schedule_handled = 1U;
      if(g_sync_probe_window_mode == 0U) {
        /* 关RX窗口 */
        g_sync_rx_open_this_cycle = 0;
        g_sync_sparse_counter = 0;
        if(g_sync_forced_sleep_counter < 255U) g_sync_forced_sleep_counter++;
        if(g_sync_forced_sleep_counter >= SYNC_FORCED_SLEEP_CYCLES) {
          g_sync_probe_window_mode = 1U;
          g_sync_forced_probe_counter = 0U;
          g_sync_forced_probe_hit_in_round = 0U;
          DebugPrint("[SYNC] FORCED-SCAN -> PROBE\r\n");
        }
      } else {
        /* 开RX探测窗口：pw=1 时本周期强制开 RX，禁止被后续稀疏调度覆盖 */
        g_sync_rx_open_this_cycle = 1;
        g_sync_sparse_counter = 0;
        if(g_sync_forced_probe_counter < 255U) g_sync_forced_probe_counter++;
        if(g_sync_forced_probe_counter >= SYNC_FORCED_PROBE_RX_CYCLES) {
          g_sync_forced_probe_counter = 0U;
          if(g_sync_forced_probe_hit_in_round == 0U) {
            if(g_sync_forced_probe_miss_rounds < 255U) g_sync_forced_probe_miss_rounds++;
            if(g_sync_dither_idx < 255U) g_sync_dither_idx++;
            if((uint16_t)(SYNC_TX_DITHER_OFFSET_MS + ((uint16_t)g_sync_dither_idx * SYNC_TX_DITHER_STEP_MS)) > SYNC_TX_DITHER_MAX_MS) {
              g_sync_dither_idx = 0U;
            }
#if LOG_SYNC_OBSERVE_ENABLE && LOG_SYNC_OBS_DITHER_ENABLE
            {
              uint16_t obs_dither_ms = (uint16_t)(SYNC_TX_DITHER_OFFSET_MS + ((uint16_t)g_sync_dither_idx * SYNC_TX_DITHER_STEP_MS));
              if(obs_dither_ms > SYNC_TX_DITHER_MAX_MS) {
                obs_dither_ms = SYNC_TX_DITHER_MAX_MS;
              }
              DebugPrint("[OBS][DITHER] idx=");
              DebugPrintDec(g_sync_dither_idx);
              DebugPrint(" ms=");
              DebugPrintDec(obs_dither_ms);
              DebugPrint(" probe_miss_rounds=");
              DebugPrintDec(g_sync_forced_probe_miss_rounds);
              DebugPrint("\r\n");
            }
#endif
            if(g_sync_forced_probe_miss_rounds >= SYNC_FORCED_PROBE_MISS_ROUNDS_TO_SLEEP) {
              g_sync_probe_window_mode = 0U;
              g_sync_forced_sleep_counter = 0U;
              g_sync_forced_probe_miss_rounds = 0U;
              DebugPrint("[SYNC] FORCED-SCAN probe miss x2 -> SLEEP\r\n");
            } else {
              DebugPrint("[SYNC] FORCED-SCAN probe miss round=1, keep PROBE\r\n");
            }
          } else {
            g_sync_forced_probe_miss_rounds = 0U;
          }
          g_sync_forced_probe_hit_in_round = 0U;
        }
      }
    }
    else
#endif
    if((g_sync_force_acq_cycles_left == 0U) &&
       (g_sync_forced_scan_mode == 0U) &&
       (g_sync_state == SYNC_STATE_LOCKED_SPARSE) &&
       (g_sync_no_rx_keep_count >= SYNC_LOCK_NO_RX_FORCE_ACQ_TH)) {
      g_sync_state = SYNC_STATE_ACQUIRE;
      g_sync_force_acq_cycles_left = SYNC_FORCE_ACQ_RX_CYCLES;
      g_sync_good_count = 0U;
      g_sync_bad_count = 0U;
      g_sync_miss_count = 0U;
      g_sync_sparse_counter = 0U;
      g_sync_rx_open_this_cycle = 1U;
      if(g_sync_rescue_count_total < 0xFFFFFFFFUL) g_sync_rescue_count_total++;
      DebugPrint("[RESCUE] LOCK no-rx -> FORCE ACQ\r\n");
#if LOG_SYNC_OBSERVE_ENABLE && LOG_SYNC_OBS_RESCUE_ENABLE
      DebugPrint("[OBS][RESCUE] no_rx=");
      DebugPrintDec(g_sync_no_rx_keep_count);
      DebugPrint(" th=");
      DebugPrintDec(SYNC_LOCK_NO_RX_FORCE_ACQ_TH);
      DebugPrint(" force_rx=");
      DebugPrintDec(SYNC_FORCE_ACQ_RX_CYCLES);
      DebugPrint(" rounds=");
      DebugPrintDec(g_sync_recover_fail_rounds);
      DebugPrint(" cnt=");
      DebugPrintDec((uint16_t)(g_sync_rescue_count_total & 0xFFFFU));
      DebugPrint("\r\n");
#endif
    }

    if(!fscan_schedule_handled) {
      if(g_sync_force_acq_cycles_left > 0U) {
        g_sync_rx_open_this_cycle = 1;
        g_sync_sparse_counter = 0;
        g_sync_force_acq_cycles_left--;
        if(g_sync_force_acq_cycles_left == 0U) {
          if(g_sync_recover_fail_rounds < 255U) {
            g_sync_recover_fail_rounds++;
          }
        }
      } else if(g_sync_state == SYNC_STATE_ACQUIRE) {
        g_sync_rx_open_this_cycle = 1;
        g_sync_sparse_counter = 0;
      } else {
        g_sync_sparse_counter++;
        if(g_sync_sparse_counter >= SYNC_RX_EVERY_N_CYCLES) {
          g_sync_sparse_counter = 0;
          g_sync_rx_open_this_cycle = 1;
        } else {
          g_sync_rx_open_this_cycle = 0;
        }
      }
    }

#if SYNC_TEST_DISABLE_POWER_SAVE
    g_sync_forced_scan_mode = 0U;
    g_sync_probe_window_mode = 0U;
    g_sync_state = SYNC_STATE_ACQUIRE;
    g_sync_rx_open_this_cycle = 1U;
    g_sync_sparse_counter = 0U;
#endif

#if LOG_SYNC_SCHEDULE_ENABLE
    if((SYNC_SCHEDULE_PRINT_EVERY_N > 0U) && ((g_cycle % SYNC_SCHEDULE_PRINT_EVERY_N) == 0U)) {
      DebugPrint("[SCH] cyc=");
      DebugPrintDec((uint16_t)(g_cycle & 0xFFFFU));
      DebugPrint(" st=");
      if(g_sync_forced_scan_mode == 1U) {
        DebugPrint("FSCAN");
      } else if(g_sync_state == SYNC_STATE_ACQUIRE) {
        DebugPrint("ACQ");
      } else {
        DebugPrint("LOCK");
      }
      DebugPrint(" N=");
      DebugPrintDec(SYNC_RX_EVERY_N_CYCLES);
      DebugPrint(" sc=");
      DebugPrintDec(g_sync_sparse_counter);
      DebugPrint(" rx=");
      if(g_sync_rx_open_this_cycle) {
        DebugPrint("ON");
      } else {
        DebugPrint("OFF");
      }
      DebugPrint("\r\n");
    }
#endif

#if LOG_SYNC_LOCK_DIAG_ENABLE
    if((SYNC_SCHEDULE_PRINT_EVERY_N > 0U) && ((g_cycle % SYNC_SCHEDULE_PRINT_EVERY_N) == 0U)) {
      uint8_t lock_need_diag = SYNC_LOCK_NEED_GOOD_COUNT;
#if SYNC_FAST_LOCK_ENABLE
      lock_need_diag = SYNC_FAST_LOCK_GOOD_COUNT;
#endif
      DebugPrint("[DIAG] good=");
      DebugPrintDec(g_sync_good_count);
      DebugPrint("/");
      DebugPrintDec(lock_need_diag);
      DebugPrint(" bad=");
      DebugPrintDec(g_sync_bad_count);
      DebugPrint(" miss=");
      DebugPrintDec(g_sync_miss_count);
      DebugPrint(" no_rx=");
      DebugPrintDec(g_sync_no_rx_keep_count);
#if SYNC_FORCED_SCAN_ENABLE
      DebugPrint(" fs=");
      DebugPrintDec(g_sync_forced_scan_mode);
      DebugPrint(" pw=");
      DebugPrintDec(g_sync_probe_window_mode);
      DebugPrint(" sl=");
      DebugPrintDec(g_sync_forced_sleep_counter);
      DebugPrint(" pb=");
      DebugPrintDec(g_sync_forced_probe_counter);
      DebugPrint(" pm=");
      DebugPrintDec(g_sync_forced_probe_miss_rounds);
#endif
      DebugPrint(" st=");
      if(g_sync_forced_scan_mode == 1U) {
        DebugPrint("FSCAN");
      } else if(g_sync_state == SYNC_STATE_ACQUIRE) {
        DebugPrint("ACQ");
      } else {
        DebugPrint("LOCK");
      }
      DebugPrint("\r\n");
    }
#endif

#if LOG_SYNC_RX_COUNTER_ENABLE
    if(g_sync_rx_counter_last_cycle != g_cycle) {
      g_sync_rx_counter_last_cycle = g_cycle;
      DebugPrint("[RXCNT] raw=");
      DebugPrintDec((uint16_t)(g_sync_rx_raw_count & 0xFFFFU));
      DebugPrint(" valid=");
      DebugPrintDec((uint16_t)(g_sync_rx_valid_count & 0xFFFFU));
      DebugPrint("\r\n");
    }
#endif
  }

  /* TX 时间段：默认固定发送；可按宏开启偏移探测（用于打破长期对撞） */
#if SYNC_TX_DITHER_ENABLE
  {
    uint8_t use_dither = 1U;
    uint16_t dither_ms = SYNC_TX_DITHER_OFFSET_MS;
#if SYNC_TX_DITHER_PROBE_ONLY
    use_dither = (g_sync_forced_scan_mode == 1U && g_sync_probe_window_mode == 1U) ? 1U : 0U;
#endif
    if(use_dither) {
      dither_ms = (uint16_t)(SYNC_TX_DITHER_OFFSET_MS + ((uint16_t)g_sync_dither_idx * SYNC_TX_DITHER_STEP_MS));
      if(dither_ms > SYNC_TX_DITHER_MAX_MS) {
        dither_ms = SYNC_TX_DITHER_MAX_MS;
      }
      tx_time_ms = (uint16_t)(SYNC_TX_TIME_MS + dither_ms);
      if(tx_time_ms >= SYNC_CYCLE_MS) tx_time_ms -= SYNC_CYCLE_MS;
    }
  }
#endif

#if (SYNC_TEST_TX_OFFSET_MS != 0)
  {
    int16_t _tx_tmp = (int16_t)tx_time_ms + (int16_t)SYNC_TEST_TX_OFFSET_MS;
    while(_tx_tmp < 0) _tx_tmp += (int16_t)SYNC_CYCLE_MS;
    while(_tx_tmp >= (int16_t)SYNC_CYCLE_MS) _tx_tmp -= (int16_t)SYNC_CYCLE_MS;
    tx_time_ms = (uint16_t)_tx_tmp;
  }
#endif

#if SYNC_TEST_FORCE_TX_ONLY
  g_sync_rx_open_this_cycle = 0U;
#endif
#if SYNC_TEST_FORCE_RX_ONLY
  g_sync_rx_open_this_cycle = 1U;
#endif

  if (!SYNC_TEST_FORCE_RX_ONLY
#if SYNC_TEST_SKIP_TX_IF_VALID_THIS_CYCLE
      && (g_sync_last_valid_rx_cycle != g_cycle)
#endif
      && g_cycle != g_last_tx_cycle && g_phase_ms >= tx_time_ms && g_phase_ms < (tx_time_ms + 50U)) {
    /* 切换到 TX 模式，频道 76 */
    if (g_rf_mode != 1) {
      RF_Link_ConfigTx(RF_TX_CHANNEL);
      g_rf_mode = 1;
      g_rf_cfg_tx_fail_streak = 0U;
    }

    /* 构造并发送同步包 */
    BuildSyncPacket(RF_TX_Buf);
    {
      int tx_ret = RF_Link_Send(RF_TX_Buf, SYNC_PKT_SIZE);
      uint8_t tx_st = RF_Link_GetLastTxStatus();
      if (tx_ret == 0) {
        g_rf_send_fail_streak = 0U;
#if DEBUG_SYNC_VERBOSE
        DebugPrint("[TX] phase=");
        DebugPrintDec(g_phase_ms);
        DebugPrint("ms, target=");
        DebugPrintDec(tx_time_ms);
        DebugPrint("ms, diff=");
        if(g_phase_ms >= tx_time_ms) {
          DebugPrint("+");
          DebugPrintDec(g_phase_ms - tx_time_ms);
        } else {
          DebugPrint("-");
          DebugPrintDec(tx_time_ms - g_phase_ms);
        }
        DebugPrint("ms st=");
        DebugPrintHex(&tx_st, 1);
        DebugPrint("\r\n");
#else
        DebugPrint("[TX] st=");
        DebugPrintHex(&tx_st, 1);
        DebugPrint("\r\n");
#endif
      } else {
        if(g_rf_send_fail_streak < 255U) g_rf_send_fail_streak++;
        DebugPrint("[TX-FAIL] st=");
        DebugPrintHex(&tx_st, 1);
        DebugPrint("\r\n");
      }
    }
    g_last_tx_cycle = g_cycle;

    /* 发送后按策略切回 RX 或进入 RF 睡眠（锁定省电态跨周期接收） */
    if(g_sync_rx_open_this_cycle) {
      RF_Link_ConfigRx(RF_RX_CHANNEL);
      g_rf_mode = 0;
      g_rf_cfg_rx_fail_streak = 0U;
    } else {
      RF_Link_Sleep();
      g_rf_mode = 2;
    }
  }

  /* 若本周期计划接收但当前未在 RX，则补切到 RX（兼容异常恢复场景） */
  if(g_sync_rx_open_this_cycle && g_rf_mode != 0) {
    RF_Link_ConfigRx(RF_RX_CHANNEL);
    g_rf_mode = 0;
    g_rf_cfg_rx_fail_streak = 0U;
  }

  /* RX 模式下轮询接收 */
  if (g_sync_rx_open_this_cycle && g_rf_mode == 0) {
    uint8_t rx_len = 0;
    if (RF_Link_PollReceive(RF_RX_Buf, &rx_len) == 1) {
      if(g_sync_rx_raw_count < 0xFFFFFFFFUL) g_sync_rx_raw_count++;
      uint16_t rx_phase_ms = ParseSyncPacket(RF_RX_Buf);
      if (rx_phase_ms < SYNC_CYCLE_MS) {
        if(g_sync_rx_valid_count < 0xFFFFFFFFUL) g_sync_rx_valid_count++;
        g_sync_last_valid_rx_cycle = g_cycle;
        /* 计算相位差异（规范化到 [-T/2, +T/2]） */
        phase_diff = (int16_t)rx_phase_ms - (int16_t)g_phase_ms;
        if(phase_diff > (int16_t)(SYNC_CYCLE_MS / 2)) phase_diff -= SYNC_CYCLE_MS;
        if(phase_diff < -(int16_t)(SYNC_CYCLE_MS / 2)) phase_diff += SYNC_CYCLE_MS;

#if DEBUG_SYNC_VERBOSE
        DebugPrint("RX: ph=");
        DebugPrintDec(rx_phase_ms);
        DebugPrint(" local=");
        DebugPrintDec(g_phase_ms);
        DebugPrint(" diff=");
        if(phase_diff >= 0) {
          DebugPrint("+");
          DebugPrintDec((uint16_t)phase_diff);
        } else {
          DebugPrint("-");
          DebugPrintDec((uint16_t)(-phase_diff));
        }
        DebugPrint("\r\n");
#else
        DebugPrint("RX\r\n");
#endif

        /* 相位调整 */
        Sync_AdjustFromPacket(rx_phase_ms);
        rx_got_valid = 1;
#if SYNC_FORCED_SCAN_ENABLE
        if(g_sync_forced_scan_mode == 1U && g_sync_probe_window_mode == 1U) {
          g_sync_forced_probe_hit_in_round = 1U;
        }
#endif
      }
    }
  }

  /* 锁定质量评估与状态迁移 */
  if(g_sync_rx_open_this_cycle) {
    if(rx_got_valid) {
      uint16_t abs_err = (phase_diff >= 0) ? (uint16_t)phase_diff : (uint16_t)(-phase_diff);
      g_sync_miss_count = 0;
      g_sync_no_rx_keep_count = 0;
      g_last_valid_rx_tick = HAL_GetTick();
      g_sync_recover_fail_rounds = 0U;
      g_sync_dither_idx = 0U;
      g_rf_poll_err_streak = 0U;

      if(abs_err <= SYNC_LOCK_ERR_TH_MS) {
        if(g_sync_good_count < 255U) g_sync_good_count++;
        g_sync_bad_count = 0;
      } else if(abs_err >= SYNC_UNLOCK_ERR_TH_MS) {
        if(g_sync_bad_count < 255U) g_sync_bad_count++;
        g_sync_good_count = 0;
      } else {
        /* 中间带：不计好也不计坏，避免抖动 */
        g_sync_good_count = 0;
        g_sync_bad_count = 0;
      }

      {
        uint8_t lock_need = SYNC_LOCK_NEED_GOOD_COUNT;
#if SYNC_FAST_LOCK_ENABLE
        lock_need = SYNC_FAST_LOCK_GOOD_COUNT;
#endif
        if((g_sync_state == SYNC_STATE_ACQUIRE || g_sync_forced_scan_mode == 1U) && g_sync_good_count >= lock_need) {
          g_sync_forced_scan_mode = 0U;
          g_sync_probe_window_mode = 0U;
          g_sync_forced_sleep_counter = 0U;
          g_sync_forced_probe_counter = 0U;
          g_sync_forced_probe_miss_rounds = 0U;
          g_sync_forced_probe_hit_in_round = 0U;
          g_sync_state = SYNC_STATE_LOCKED_SPARSE;
          g_sync_good_count = 0;
          g_sync_bad_count = 0;
          g_sync_sparse_counter = 0;
          DebugPrint("[SYNC] LOCKED -> SPARSE RX\r\n");
        }
      }

#if SYNC_FORCED_SCAN_ENABLE
      /* 日志/状态优化：FSCAN 期间不执行 bad-phase 退锁，避免探测阶段状态抖动 */
      if((g_sync_forced_scan_mode == 0U) && g_sync_state == SYNC_STATE_LOCKED_SPARSE && g_sync_bad_count >= SYNC_UNLOCK_NEED_BAD_COUNT) {
#else
      if(g_sync_state == SYNC_STATE_LOCKED_SPARSE && g_sync_bad_count >= SYNC_UNLOCK_NEED_BAD_COUNT) {
#endif
        g_sync_state = SYNC_STATE_ACQUIRE;
        g_sync_good_count = 0;
        g_sync_bad_count = 0;
        g_sync_miss_count = 0;
        g_sync_no_rx_keep_count = 0;
        g_sync_rx_open_this_cycle = 1;
        RF_Link_ConfigRx(RF_RX_CHANNEL);
        g_rf_mode = 0;
        DebugPrint("[SYNC] UNLOCK (bad phase) -> ACQUIRE\r\n");
      }
    } else {
      if(g_sync_state == SYNC_STATE_LOCKED_SPARSE) {
        if(g_sync_no_rx_keep_count < 255U) g_sync_no_rx_keep_count++;

        if(g_sync_no_rx_keep_count <= SYNC_LOCK_ASSUME_NO_RX_CYCLES) {
          /* 对撞无包容忍窗口：保持锁定，不立即回退 */
          g_sync_miss_count = 0;
        } else {
          if(g_sync_miss_count < 255U) g_sync_miss_count++;
          if(g_sync_miss_count >= SYNC_UNLOCK_MISS_COUNT) {
#if SYNC_COLLISION_HOLD_ENABLE
            if(g_sync_no_rx_keep_count >= SYNC_COLLISION_HOLD_MIN_NO_RX) {
              /* 对撞保持：判定为“高同步但长期对撞无包”，继续留在稀疏接收态 */
              g_sync_miss_count = 0;
#if LOG_SYNC_COLLISION_HOLD_ENABLE
              if(g_sync_last_collision_hold_cycle != g_cycle) {
                g_sync_last_collision_hold_cycle = g_cycle;
                DebugPrint("[SYNC] COLLISION-HOLD keep SPARSE RX\r\n");
              }
#endif
            } else {
              g_sync_state = SYNC_STATE_ACQUIRE;
              g_sync_good_count = 0;
              g_sync_bad_count = 0;
              g_sync_miss_count = 0;
              g_sync_no_rx_keep_count = 0;
              g_sync_rx_open_this_cycle = 1;
              RF_Link_ConfigRx(RF_RX_CHANNEL);
              g_rf_mode = 0;
              DebugPrint("[SYNC] UNLOCK (miss timeout) -> ACQUIRE\r\n");
            }
#else
            g_sync_state = SYNC_STATE_ACQUIRE;
            g_sync_good_count = 0;
            g_sync_bad_count = 0;
            g_sync_miss_count = 0;
            g_sync_no_rx_keep_count = 0;
            g_sync_rx_open_this_cycle = 1;
            RF_Link_ConfigRx(RF_RX_CHANNEL);
            g_rf_mode = 0;
            DebugPrint("[SYNC] UNLOCK (miss timeout) -> ACQUIRE\r\n");
#endif
          }
        }
      }
    }
  }
}

/* 分层恢复：先协议自救，再底层恢复
 * 条件：长期无有效包 + 协议自救多轮失败 + 有底层异常证据 + 冷却窗允许
 */
static void RF_Recovery_Check(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t obs_no_valid_ms = now - g_last_valid_rx_tick; /* 观测：触发前的“无有效包时长” */
  uint8_t obs_recover_rounds = g_sync_recover_fail_rounds; /* 观测：触发前协议自救失败轮次 */

  if((now - g_rf_recover_hour_window_start) >= SYNC_RECOVER_HOUR_MS) {
    g_rf_recover_hour_window_start = now;
    g_rf_recover_count_this_hour = 0U;
  }

  if((now - g_last_valid_rx_tick) < SYNC_RECOVER_NO_VALID_MS) {
    return;
  }
  if(g_sync_recover_fail_rounds < SYNC_RECOVER_FAIL_ROUNDS_TH) {
    return;
  }
  if(now < g_rf_recover_cooldown_until) {
    return;
  }
  if(g_rf_recover_count_this_hour >= SYNC_RECOVER_MAX_PER_HOUR) {
    return;
  }

  if((g_rf_cfg_rx_fail_streak < RF_ERR_STREAK_TH) &&
     (g_rf_cfg_tx_fail_streak < RF_ERR_STREAK_TH) &&
     (g_rf_send_fail_streak < RF_ERR_STREAK_TH) &&
     (g_rf_poll_err_streak < RF_ERR_STREAK_TH)) {
    return;
  }

  RF_Link_Init();
  g_rf_initialized = 1U;
  RF_Link_ConfigRx(RF_RX_CHANNEL);
  g_rf_mode = 0;
  g_rf_cfg_rx_fail_streak = 0U;

  g_sync_state = SYNC_STATE_ACQUIRE;
  g_sync_forced_scan_mode = 0U;
  g_sync_probe_window_mode = 0U;
  g_sync_forced_sleep_counter = 0U;
  g_sync_forced_probe_counter = 0U;
  g_sync_forced_probe_miss_rounds = 0U;
  g_sync_forced_probe_hit_in_round = 0U;
  g_sync_force_acq_cycles_left = SYNC_FORCE_ACQ_RX_CYCLES;
  g_sync_recover_fail_rounds = 0U;
  g_sync_dither_idx = 0U;

  g_last_valid_rx_tick = now;
  g_rf_recover_cooldown_until = now + SYNC_RECOVER_COOLDOWN_MS;
  g_rf_recover_count_total++;
  g_rf_recover_count_this_hour++;

  DebugPrint("[RF-RECOVER] reinit + force ACQ\r\n");
#if LOG_SYNC_OBSERVE_ENABLE && LOG_SYNC_OBS_RECOVER_ENABLE
  DebugPrint("[OBS][RECOVER] no_valid_ms=");
  DebugPrintDec((uint16_t)(obs_no_valid_ms > 65535U ? 65535U : obs_no_valid_ms));
  DebugPrint(" rounds=");
  DebugPrintDec(obs_recover_rounds);
  DebugPrint(" st_rx=");
  DebugPrintDec(g_rf_cfg_rx_fail_streak);
  DebugPrint(" st_tx=");
  DebugPrintDec(g_rf_cfg_tx_fail_streak);
  DebugPrint(" st_send=");
  DebugPrintDec(g_rf_send_fail_streak);
  DebugPrint(" st_poll=");
  DebugPrintDec(g_rf_poll_err_streak);
  DebugPrint(" hour=");
  DebugPrintDec(g_rf_recover_count_this_hour);
  DebugPrint("/max");
  DebugPrintDec(SYNC_RECOVER_MAX_PER_HOUR);
  DebugPrint(" total=");
  DebugPrintDec((uint16_t)(g_rf_recover_count_total & 0xFFFFU));
  DebugPrint("\r\n");
#endif
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

/* 日夜翻转前的短时防误触发确认：在一个确认窗口内多次采样 PA0。
 * toNight=1 表示准备从白天翻到夜间；toNight=0 表示从夜间翻到白天。
 */
static uint8_t DayNight_ConfirmTransition(uint8_t toNight)
{
#if DAYNIGHT_FALSE_TRIG_SUPPRESS_ENABLE
  uint32_t start = HAL_GetTick();
  uint32_t hits = 0U;
  uint32_t reads = 0U;

  while(((HAL_GetTick() - start) < DAYNIGHT_FALSE_TRIG_CONFIRM_WINDOW_MS) &&
        (reads < DAYNIGHT_FALSE_TRIG_CONFIRM_MAX_READS))
  {
    uint32_t raw = Read_ADC1_Channel(ADC_CHANNEL_0);
    if(raw == 0xFFFFU) {
      break;
    }

    uint16_t v = (uint16_t)raw;
    if(toNight != 0U) {
      if(v < SOLAR_ADC_NIGHT_THRESHOLD) {
        hits++;
      }
    } else {
      if(v > SOLAR_ADC_DAY_THRESHOLD) {
        hits++;
      }
    }

    reads++;
    if(hits >= DAYNIGHT_FALSE_TRIG_CONFIRM_MIN_HITS) {
      break;
    }
  }

  return (hits >= DAYNIGHT_FALSE_TRIG_CONFIRM_MIN_HITS) ? 1U : 0U;
#else
  (void)toNight;
  return 1U;
#endif
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
  
  /* 扫描模式下需要“每个转换都 Poll 一次”再 GetValue，不能只 Poll 一次就连读两次寄存器。
   * 否则两个通道很容易读到同一个值（你日志里 solar==batt 的现象）。
   */
  {
    uint32_t i;
    for(i = 0; i < 2U; i++) {
      if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 1;
      }
      adc_values[i] = (uint32_t)HAL_ADC_GetValue(&hadc1);
    }
  }
  
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
  /* STOP 欠压停载：让 RF 进入睡眠（不要依赖 g_rf_sleeping 的软件状态，避免 IO 状态不一致） */
  if(g_rf_initialized) {
    RF_Link_Sleep();
  }
  /* 软件 SPI 三线钉死到安全电平，避免进入低功耗后电平漂移导致 XL2400T 异常唤醒/通信态错乱 */
  HAL_GPIO_WritePin(GPIOA, RF_CSN_Pin, GPIO_PIN_SET);    /* CSN 高：片选无效 */
  HAL_GPIO_WritePin(GPIOA, RF_SCK_Pin, GPIO_PIN_RESET);  /* SCK 低 */
  HAL_GPIO_WritePin(GPIOA, RF_DATA_Pin, GPIO_PIN_RESET); /* DATA 低 */
  g_rf_sleeping = 1;
}

static void Undervolt_EnterStandby(void)
{
  /* 清掉唤醒标志，避免“进入 Standby 时 WKUP 电平/标志已存在”导致立即再次退出 */
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF1);

  /* 只启用 WKUP1，用 PA0(太阳能电压) 上升沿触发退出 Standby */
  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN2);
  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN3);
  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN4);
  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN6);
  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1_HIGH);

  /* 先把业务输出/无线拉到低功耗基准态（降低进入 Standby 前的毛刺电流） */
  Undervolt_EnterLowPowerOutputs();

  /* Standby 期间用 PWR 内部 PU/PD 硬化 RF 引脚电平 */
  Undervolt_StandbyHoldRfPins();

  /* 关闭串口并把 UART 引脚改为模拟输入，避免外部未定义电平漏电 */
  #if DEBUG_UART_ENABLE
  HAL_UART_DeInit(&huart1);
  #endif
  Undervolt_SetUartPinsAnalog();

  /* 进入 Standby 的 GPIO 配置：
   *  - PA0 必须是数字输入（WKUP1 用），且用下拉锁定低电平
   *  - PA1 保持模拟输入，降低电流
   */
  Undervolt_PrepareStandbyIO();

  HAL_PWR_EnterSTANDBYMode();

  /* 不应返回；若返回则说明 Standby 未成功进入 */
  while(1) {;}
}

static void Undervolt_StandbyHoldRfPins(void)
{
  /* Standby 下 GPIO 可能不再保持推挽输出，使用 PWR 的 PU/PD 在 Standby 内维持线状态 */
  (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_A, RF_CSN_Pin);
  (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_A, RF_SCK_Pin | RF_DATA_Pin);
  (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, RF_CSN_Pin);                 /* CSN 高 */
  (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, RF_SCK_Pin | RF_DATA_Pin); /* SCK/DATA 低 */
  HAL_PWREx_EnablePullUpPullDownConfig();
}

static void Undervolt_Wait_WFI_Ms(uint32_t ms)
{
  uint32_t start = HAL_GetTick();
  while((HAL_GetTick() - start) < ms) {
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
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

/**
 * @brief 欠压 Standby：只做 IO 最小化（PA0/PA1 模拟输入），唤醒仅依赖 RTC alarm
 */
static void Undervolt_PrepareStandbyIO(void)
{
  GPIO_InitTypeDef g = {0};
  HAL_NVIC_DisableIRQ(EXTI0_1_IRQn);
  HAL_ADC_DeInit(&hadc1);
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA0：WKUP1 输入必须是数字状态，使用下拉锁定“低电平”基准 */
  g.Pin = GPIO_PIN_0;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &g);

  /* PA1：电池 ADC 输入用模拟，降低漏电 */
  g.Pin = GPIO_PIN_1;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);
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

static HAL_StatusTypeDef Day_RTC_SetNextAlarm(RTC_HandleTypeDef *hrtc_p)
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

  nextSec = (uint8_t)((newSec + DAY_STOP_RTC_ALARM_STEP_SEC) % 60U);

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

  /* 日间 STOP 调试：确认 RTC Alarm A 是否按预期步进 */
  DebugPrint("[DAY] RTC arm currSec=");
  DebugPrintDec((uint16_t)newSec);
  DebugPrint(" nextSec=");
  DebugPrintDec((uint16_t)nextSec);
  DebugPrint(" step=");
  DebugPrintDec((uint16_t)DAY_STOP_RTC_ALARM_STEP_SEC);
  DebugPrint("\r\n");

  return HAL_RTC_SetAlarm_IT(hrtc_p, &sAlarm, RTC_FORMAT_BIN);
}

static void Day_PrepareStopWakeSources(void)
{
  #if DEBUG_UART_ENABLE
  HAL_UART_DeInit(&huart1);
  #endif
  Undervolt_SetUartPinsAnalog();

  /* 复用已验证的 STOP 唤醒 IO 处理：PA0=EXTI上升沿，PA1=模拟输入 */
  Undervolt_PrepareExtiPa0();
}

static void Day_RestoreFromStopWake(void)
{
  DebugPrint("[DAY] STOP wake\r\n");
  Undervolt_RestoreAdcFromExti();
  __HAL_RTC_CLEAR_FLAG(&hrtc, RTC_CLEAR_ALRAF);

  #if DEBUG_UART_ENABLE
  MX_USART1_UART_Init();
  #endif
}

/* 日/夜检测：读 PA0 太阳能板电压，滞回 + 持续确认（由 DAYNIGHT_HOLD_MS 控制）后翻转 (规格书 §5)，积木2 */
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
        if(DAYNIGHT_HOLD_MS == 0U) {
          /* 允许“立即翻转”：第一次采样达阈值就直接切换（增加短时防误触发确认） */
          if(DayNight_ConfirmTransition(1U)) {
            g_is_night = 1;
            g_daynight_hold_until_tick = 0;
            g_night_start_tick = now;
            g_night_debug_window = 1;
            DebugPrint("[NIGHT] Debug window started (6s)\r\n");
          }
        } else {
          g_daynight_hold_until_tick = now + DAYNIGHT_HOLD_MS;
        }
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
        if(DAYNIGHT_HOLD_MS == 0U) {
          /* 允许“立即翻转”：第一次采样达阈值就直接切换（增加短时防误触发确认） */
          if(DayNight_ConfirmTransition(0U)) {
            g_is_night = 0;
            g_daynight_hold_until_tick = 0;
            g_night_debug_window = 0;
          }
        } else {
          g_daynight_hold_until_tick = now + DAYNIGHT_HOLD_MS;
        }
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
    if(g_is_night == 0U) {
      DebugPrint("[DAY] ADC printEveryN=");
      DebugPrintDec(DAY_ADC_PRINT_EVERY_N);
      DebugPrint("\r\n");
    }
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
        
        uint32_t solar_mv = ADC_MV_FROM_RAW(solar_raw);
        DebugPrintDec((uint16_t)(solar_mv / 1000));
        DebugPrint(".");
        DebugPrintDec((uint16_t)((solar_mv % 1000) / 100));
        DebugPrintDec((uint16_t)((solar_mv % 100) / 10));
        
        DebugPrint("V) batt=");
        DebugPrintDec((uint16_t)batt_raw);
        DebugPrint("(");
        
        uint32_t batt_mv = ADC_MV_FROM_RAW(batt_raw);
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
    if(++dn_cnt >= DAY_ADC_PRINT_EVERY_N) {
      dn_cnt = 0;
      DebugPrint("[ADC] solar=");
      DebugPrintDec(adc_val);
      DebugPrint("(");
      
      uint32_t voltage_mv = ADC_MV_FROM_RAW(adc_val);
      DebugPrintDec((uint16_t)(voltage_mv / 1000));
      DebugPrint(".");
      DebugPrintDec((uint16_t)((voltage_mv % 1000) / 100));
      DebugPrintDec((uint16_t)((voltage_mv % 100) / 10));
      
      DebugPrint("V)");

#if DAY_ADC_PRINT_WITH_BATT
      {
        uint32_t batt_raw = Read_ADC1_Channel(ADC_CHANNEL_1);
        if(batt_raw != 0xFFFFU) {
          DebugPrint(" batt=");
          DebugPrintDec((uint16_t)batt_raw);
          DebugPrint("(");
          uint32_t batt_mv = ADC_MV_FROM_RAW(batt_raw);
          DebugPrintDec((uint16_t)(batt_mv / 1000));
          DebugPrint(".");
          DebugPrintDec((uint16_t)((batt_mv % 1000) / 100));
          DebugPrintDec((uint16_t)((batt_mv % 100) / 10));
          DebugPrint("V)");
        }
      }
#endif

      DebugPrint(" night=");
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
      uint32_t batt_voltage_mv = ADC_MV_FROM_RAW(adc_val);
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
    uint32_t batt_voltage_mv = ADC_MV_FROM_RAW(adc_val);
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
    uint32_t batt_voltage_mv = ADC_MV_FROM_RAW(adc_val);
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
