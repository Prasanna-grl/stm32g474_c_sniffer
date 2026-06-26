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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sniffer.h"

#include "lvgl.h"
#include "st7789.h"
#include "ui.h"
#include "ui/vars.h"
#include <fonts_.h>
#include <lvgl_port.h>
#include <stdint.h>

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
COMP_HandleTypeDef hcomp1;
COMP_HandleTypeDef hcomp2;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi2_tx;

HRTIM_HandleTypeDef hhrtim1;
DMA_HandleTypeDef hdma_hrtim1_tima;
DMA_HandleTypeDef hdma_hrtim1_timb;

/* USER CODE BEGIN PV */
uint32_t test_toggle_count = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_HRTIM1_Init(void);
static void MX_COMP1_Init(void);
static void MX_COMP2_Init(void);
static void MX_SPI2_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
#define LVGL_DISPLAY
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

uint8_t dbg_index = 0;
uint8_t debug_arr[1024] = {0};
/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_HRTIM1_Init();
  MX_COMP1_Init();
  MX_COMP2_Init();
  MX_SPI2_Init();
  MX_I2C1_Init();

  MX_USB_Device_Init();

  MX_USB_ENUM_Complete();
  /* USER CODE BEGIN 2 */

  /* Initialize sniffer module */
  sniffer_init();
  analog_data_init();

  /* Start DMA capture */
  sniffer_start_capture();
  //  INA237_Config();
#ifndef LVGL_DISPLAY
  ST7789_Init();
  ST7789_SetRotation(1); // 0 = Portrait (135x240)

  ST7789_Test();
#else
#if 0
  lv_init();
  lvgl_port_init();
  ui_init();
#endif
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
// if (test_toggle_count++ & 1) {
//   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
// } else {
//   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
// }
/* Process sniffer buffers */
#if 0
    HAL_Delay(1);
    volatile uint32_t comp1_out = HAL_COMP_GetOutputLevel(&hcomp1);
    volatile uint32_t comp2_out = HAL_COMP_GetOutputLevel(&hcomp2);
    volatile uint32_t sys_freq = HAL_RCC_GetPCLK2Freq();
#endif
    // HAL_Delay(1);
    sniffer_task();
    analog_task();
    if (!sniffer_get_dma_status()) {
      // __DSB();
      // __WFI(); /* CPU sleeps until next IRQ (DMA, USB, SysTick) */
      /* Wakes IMMEDIATELY when DMA fires → zero latency */
      /* USB IRQ fires normally during sleep → enumeration OK */
    }
    /* Small delay to prevent busy waiting */
#ifdef LVGL_DISPLAY
//	  lvgl_test();
#else
    ST7789_Test();
#endif
    //	  vbus_adc_read();
    //	  HAL_Delay(1);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  RCC_CRSInitTypeDef crs = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /* HSI 16MHz + HSI48 for USB */
  RCC_OscInitStruct.OscillatorType =
      RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON; /* USB clock source */
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;

  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 48;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV6; /* unused */
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    Error_Handler();

  /* USB: use HSI48 — completely independent of PLL */
  /* HSI48 + CRS (auto-calibrated via USB SOF) = better than PLL for USB */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    Error_Handler();

  __HAL_RCC_CRS_CLK_ENABLE();

  crs.Prescaler = RCC_CRS_SYNC_DIV1;
  crs.Source = RCC_CRS_SYNC_SOURCE_USB; /* sync to USB SOF */
  crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
  crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
  crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
  HAL_RCCEx_CRSConfig(&crs);
  // if (HAL_RCCEx_CRSConfig(&crs) != HAL_OK)
  //   Error_Handler();
}

/**
 * @brief COMP1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_COMP1_Init(void) {

  /* USER CODE BEGIN COMP1_Init 0 */

  /* USER CODE END COMP1_Init 0 */

  /* USER CODE BEGIN COMP1_Init 1 */

  /* USER CODE END COMP1_Init 1 */
  hcomp1.Instance = COMP1;
  hcomp1.Init.InputPlus = COMP_INPUT_PLUS_IO1;
  hcomp1.Init.InputMinus = COMP_INPUT_MINUS_1_4VREFINT;
  hcomp1.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp1.Init.Hysteresis = COMP_HYSTERESIS_50MV;
  hcomp1.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp1.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
  if (HAL_COMP_Init(&hcomp1) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP1_Init 2 */

  /* USER CODE END COMP1_Init 2 */
}

/**
 * @brief COMP2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_COMP2_Init(void) {

  /* USER CODE BEGIN COMP2_Init 0 */

  /* USER CODE END COMP2_Init 0 */

  /* USER CODE BEGIN COMP2_Init 1 */

  /* USER CODE END COMP2_Init 1 */
  hcomp2.Instance = COMP2;
  hcomp2.Init.InputPlus = COMP_INPUT_PLUS_IO1;
  hcomp2.Init.InputMinus = COMP_INPUT_MINUS_1_4VREFINT;
  hcomp2.Init.OutputPol = COMP_OUTPUTPOL_NONINVERTED;
  hcomp2.Init.Hysteresis = COMP_HYSTERESIS_50MV;
  hcomp2.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
  hcomp2.Init.TriggerMode = COMP_TRIGGERMODE_NONE;
  if (HAL_COMP_Init(&hcomp2) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN COMP2_Init 2 */

  /* USER CODE END COMP2_Init 2 */
}

/**
 * @brief I2C1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C1_Init(void) {

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10A30E20;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    Error_Handler();
  }

  /** Configure Analogue filter
   */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
    Error_Handler();
  }

  /** Configure Digital filter
   */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
    Error_Handler();
  }

  /** I2C Fast mode Plus enable
   */
  HAL_I2CEx_EnableFastModePlus(I2C_FASTMODEPLUS_I2C1);
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */
}

/**
 * @brief SPI2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SPI2_Init(void) {

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */
}

static void MX_HRTIM1_Init(void) {
  HRTIM_TimeBaseCfgTypeDef timBase = {0};
  HRTIM_EventCfgTypeDef evtCfg = {0};
  HRTIM_CaptureCfgTypeDef capCfg = {0};
  HRTIM_TimerCfgTypeDef timCfg = {0};

  hhrtim1.Instance = HRTIM1;
  hhrtim1.Init.HRTIMInterruptRequests = HRTIM_IT_NONE;
  hhrtim1.Init.SyncOptions = HRTIM_SYNCOPTION_NONE;
  hhrtim1.Init.SyncInputSource = HRTIM_SYNCINPUTSOURCE_NONE;
  hhrtim1.Init.SyncOutputSource = HRTIM_SYNCOUTPUTSOURCE_MASTER_START;
  hhrtim1.Init.SyncOutputPolarity = HRTIM_SYNCOUTPUTPOLARITY_NONE;
  if (HAL_HRTIM_Init(&hhrtim1) != HAL_OK)
    Error_Handler();

  /* ── STEP 1: WaveformTimerConfig FIRST (full TIMxCR write) ─── */
  timCfg.InterruptRequests = HRTIM_TIM_IT_NONE;
  timCfg.DMARequests = HRTIM_TIM_DMA_NONE;
  timCfg.DMASrcAddress = 0;
  timCfg.DMADstAddress = 0;
  timCfg.DMASize = 0;
  timCfg.HalfModeEnable = HRTIM_HALFMODE_DISABLED;
  timCfg.StartOnSync = HRTIM_SYNCSTART_DISABLED;
  timCfg.ResetOnSync = HRTIM_SYNCRESET_DISABLED;
  timCfg.DACSynchro = HRTIM_DACSYNC_NONE;
  timCfg.PreloadEnable = HRTIM_PRELOAD_DISABLED;
  timCfg.UpdateGating = HRTIM_UPDATEGATING_INDEPENDENT;
  timCfg.BurstMode = HRTIM_TIMERBURSTMODE_MAINTAINCLOCK;
  timCfg.RepetitionUpdate = HRTIM_UPDATEONREPETITION_DISABLED;
  timCfg.PushPull = HRTIM_TIMPUSHPULLMODE_DISABLED;
  timCfg.FaultEnable = HRTIM_TIMFAULTENABLE_NONE;
  timCfg.FaultLock = HRTIM_TIMFAULTLOCK_READWRITE;
  timCfg.DeadTimeInsertion = HRTIM_TIMDEADTIMEINSERTION_DISABLED;
  timCfg.DelayedProtectionMode = HRTIM_TIMER_A_B_C_DELAYEDPROTECTION_DISABLED;
  timCfg.UpdateTrigger = HRTIM_TIMUPDATETRIGGER_NONE;
  timCfg.ResetTrigger = HRTIM_TIMRESETTRIGGER_NONE;
  timCfg.ResetUpdate = HRTIM_TIMUPDATEONRESET_DISABLED;
  timCfg.ReSyncUpdate = HRTIM_TIMERESYNC_UPDATE_UNCONDITIONAL;

  if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A,
                                    &timCfg) != HAL_OK)
    Error_Handler();
  if (HAL_HRTIM_WaveformTimerConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B,
                                    &timCfg) != HAL_OK)
    Error_Handler();

  /* ── STEP 2: TimeBaseConfig AFTER (read-modify-write, sets CKPSCx) */
  timBase.Period = 0xFFFF;
  timBase.RepetitionCounter =
      0x00; // ← fires REP event every 10 periods to match roll over speed
  timBase.PrescalerRatio = HRTIM_PRESCALERRATIO_DIV4; /* 96/4 = 24MHz */
  timBase.Mode = HRTIM_MODE_CONTINUOUS;

  if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A, &timBase) !=
      HAL_OK)
    Error_Handler();
  if (HAL_HRTIM_TimeBaseConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B, &timBase) !=
      HAL_OK)
    Error_Handler();
#if 0
  /* ── STEP 3: Safety net — force CKPSCx bits [9:7] = 0b111 ─── */
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].TIMxCR |= (0x7u << 7);
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxCR |= (0x7u << 7);

  /* ── STEP 4: Clear rogue HALF, PSHPLL, RETRIG bits ──────────────── */
  /* Target = 0x381: only CKPSCx=7 + CONT=1 */
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_A].TIMxCR &=
      ~(0xEu); /* clear bits[3:1] */
  HRTIM1->sTimerxRegs[HRTIM_TIMERINDEX_TIMER_B].TIMxCR &=
      ~(0xEu); /* clear bits[3:1] */
#endif
  /* ── External Events ──────────────────────────────────────────── */
  evtCfg.Polarity = HRTIM_EVENTPOLARITY_HIGH;
  evtCfg.Sensitivity = HRTIM_EVENTSENSITIVITY_BOTHEDGES;
  evtCfg.FastMode = HRTIM_EVENTFASTMODE_DISABLE;
  evtCfg.Filter = HRTIM_EVENTFILTER_NONE; // HRTIM_EVENTFILTER_NONE;

  evtCfg.Source = HRTIM_EEV4SRC_COMP1_OUT; /* COMP1 → EEV4 */
  if (HAL_HRTIM_EventConfig(&hhrtim1, HRTIM_EVENT_4, &evtCfg) != HAL_OK)
    Error_Handler();

  evtCfg.Source = HRTIM_EEV1SRC_COMP2_OUT; /* COMP2 → EEV1 */
  if (HAL_HRTIM_EventConfig(&hhrtim1, HRTIM_EVENT_1, &evtCfg) != HAL_OK)
    Error_Handler();

  /* ── Capture units ────────────────────────────────────────────── */
  capCfg.Trigger = HRTIM_CAPTURETRIGGER_EEV_4;
  if (HAL_HRTIM_WaveformCaptureConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_A,
                                      HRTIM_CAPTUREUNIT_1, &capCfg) != HAL_OK)
    Error_Handler();

  capCfg.Trigger = HRTIM_CAPTURETRIGGER_EEV_1;
  if (HAL_HRTIM_WaveformCaptureConfig(&hhrtim1, HRTIM_TIMERINDEX_TIMER_B,
                                      HRTIM_CAPTUREUNIT_1, &capCfg) != HAL_OK)
    Error_Handler();
}
/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */

  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BL_GPIO_Port, BL_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, DC_Pin | RST_Pin | CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : COMP1_INP_CC1_Pin COMP2_INP_CC2_Pin */
  GPIO_InitStruct.Pin = COMP1_INP_CC1_Pin | COMP2_INP_CC2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : BL_Pin */
  GPIO_InitStruct.Pin = BL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : DC_Pin RST_Pin CS_Pin */
  GPIO_InitStruct.Pin = DC_Pin | RST_Pin | CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
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
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
