/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file         stm32g4xx_hal_msp.c
 * @brief        This file provides code for the MSP Initialization
 *               and de-Initialization codes.
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
#include "stm32g4xx_hal_dma.h"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern DMA_HandleTypeDef hdma_spi2_tx;
extern DMA_HandleTypeDef hdma_hrtim1_tima;
extern DMA_HandleTypeDef hdma_hrtim1_timb;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
void HAL_HRTIM_MspPostInit(HRTIM_HandleTypeDef *hhrtim);
/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
/**
 * Initializes the Global MSP.
 */
void HAL_MspInit(void) {

  /* USER CODE BEGIN MspInit 0 */

  /* USER CODE END MspInit 0 */

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();

  /* System interrupt init*/

  /** Disable the internal Pull-Up in Dead Battery pins of UCPD peripheral
   */
  HAL_PWREx_DisableUCPDDeadBattery();

  /* USER CODE BEGIN MspInit 1 */

  /* USER CODE END MspInit 1 */
}

/**
 * @brief COMP MSP Initialization
 * This function configures the hardware resources used in this example
 * @param hcomp: COMP handle pointer
 * @retval None
 */
void HAL_COMP_MspInit(COMP_HandleTypeDef *hcomp) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (hcomp->Instance == COMP1) {
    /* USER CODE BEGIN COMP1_MspInit 0 */

    /* USER CODE END COMP1_MspInit 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**COMP1 GPIO Configuration
    PA1     ------> COMP1_INP
    */
    GPIO_InitStruct.Pin = COMP1_INP_CC1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(COMP1_INP_CC1_GPIO_Port, &GPIO_InitStruct);

    /* USER CODE BEGIN COMP1_MspInit 1 */

    /* USER CODE END COMP1_MspInit 1 */
  } else if (hcomp->Instance == COMP2) {
    /* USER CODE BEGIN COMP2_MspInit 0 */

    /* USER CODE END COMP2_MspInit 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**COMP2 GPIO Configuration
    PA7     ------> COMP2_INP
    */
    GPIO_InitStruct.Pin = COMP2_INP_CC2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(COMP2_INP_CC2_GPIO_Port, &GPIO_InitStruct);

    /* USER CODE BEGIN COMP2_MspInit 1 */

    /* USER CODE END COMP2_MspInit 1 */
  }
}

/**
 * @brief COMP MSP De-Initialization
 * This function freeze the hardware resources used in this example
 * @param hcomp: COMP handle pointer
 * @retval None
 */
void HAL_COMP_MspDeInit(COMP_HandleTypeDef *hcomp) {
  if (hcomp->Instance == COMP1) {
    /* USER CODE BEGIN COMP1_MspDeInit 0 */

    /* USER CODE END COMP1_MspDeInit 0 */

    /**COMP1 GPIO Configuration
    PA1     ------> COMP1_INP
    */
    HAL_GPIO_DeInit(COMP1_INP_CC1_GPIO_Port, COMP1_INP_CC1_Pin);

    /* USER CODE BEGIN COMP1_MspDeInit 1 */

    /* USER CODE END COMP1_MspDeInit 1 */
  } else if (hcomp->Instance == COMP2) {
    /* USER CODE BEGIN COMP2_MspDeInit 0 */

    /* USER CODE END COMP2_MspDeInit 0 */

    /**COMP2 GPIO Configuration
    PA7     ------> COMP2_INP
    */
    HAL_GPIO_DeInit(COMP2_INP_CC2_GPIO_Port, COMP2_INP_CC2_Pin);

    /* USER CODE BEGIN COMP2_MspDeInit 1 */

    /* USER CODE END COMP2_MspDeInit 1 */
  }
}

/**
 * @brief I2C MSP Initialization
 * This function configures the hardware resources used in this example
 * @param hi2c: I2C handle pointer
 * @retval None
 */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if (hi2c->Instance == I2C1) {
    /* USER CODE BEGIN I2C1_MspInit 0 */

    /* USER CODE END I2C1_MspInit 0 */

    /** Initializes the peripherals clocks
     */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
      Error_Handler();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2C1 GPIO Configuration
    PB7     ------> I2C1_SDA
    PB8-BOOT0     ------> I2C1_SCL
    */
    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Peripheral clock enable */
    __HAL_RCC_I2C1_CLK_ENABLE();
    /* USER CODE BEGIN I2C1_MspInit 1 */

    /* USER CODE END I2C1_MspInit 1 */
  }
}

/**
 * @brief I2C MSP De-Initialization
 * This function freeze the hardware resources used in this example
 * @param hi2c: I2C handle pointer
 * @retval None
 */
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c) {
  if (hi2c->Instance == I2C1) {
    /* USER CODE BEGIN I2C1_MspDeInit 0 */

    /* USER CODE END I2C1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PB7     ------> I2C1_SDA
    PB8-BOOT0     ------> I2C1_SCL
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8);

    /* USER CODE BEGIN I2C1_MspDeInit 1 */

    /* USER CODE END I2C1_MspDeInit 1 */
  }
}

/**
 * @brief SPI MSP Initialization
 * This function configures the hardware resources used in this example
 * @param hspi: SPI handle pointer
 * @retval None
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (hspi->Instance == SPI2) {
    /* USER CODE BEGIN SPI2_MspInit 0 */

    /* USER CODE END SPI2_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_SPI2_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SPI2 GPIO Configuration
    PB13     ------> SPI2_SCK
    PB15     ------> SPI2_MOSI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SPI2 DMA Init */
    /* SPI2_TX Init */
    hdma_spi2_tx.Instance = DMA1_Channel5;
    hdma_spi2_tx.Init.Request = DMA_REQUEST_SPI2_TX;
    hdma_spi2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi2_tx.Init.Mode = DMA_NORMAL;
    hdma_spi2_tx.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_spi2_tx) != HAL_OK) {
      Error_Handler();
    }

    __HAL_LINKDMA(hspi, hdmatx, hdma_spi2_tx);

    /* USER CODE BEGIN SPI2_MspInit 1 */

    /* USER CODE END SPI2_MspInit 1 */
  }
}

/**
 * @brief SPI MSP De-Initialization
 * This function freeze the hardware resources used in this example
 * @param hspi: SPI handle pointer
 * @retval None
 */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi) {
  if (hspi->Instance == SPI2) {
    /* USER CODE BEGIN SPI2_MspDeInit 0 */

    /* USER CODE END SPI2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI2_CLK_DISABLE();

    /**SPI2 GPIO Configuration
    PB13     ------> SPI2_SCK
    PB15     ------> SPI2_MOSI
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13 | GPIO_PIN_15);

    /* SPI2 DMA DeInit */
    HAL_DMA_DeInit(hspi->hdmatx);
    /* USER CODE BEGIN SPI2_MspDeInit 1 */

    /* USER CODE END SPI2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

void HAL_HRTIM_MspInit(HRTIM_HandleTypeDef *hhrtim) {
  if (hhrtim->Instance == HRTIM1) {
    __HAL_RCC_HRTIM1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();

    /* DMA1_Ch1: HRTIM1_TIMA → samples[0][] */
    hdma_hrtim1_tima.Instance = DMA1_Channel1;
    hdma_hrtim1_tima.Init.Request = DMA_REQUEST_HRTIM1_A;
    hdma_hrtim1_tima.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_hrtim1_tima.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_hrtim1_tima.Init.MemInc = DMA_MINC_ENABLE;
    hdma_hrtim1_tima.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_hrtim1_tima.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_hrtim1_tima.Init.Mode = DMA_CIRCULAR;
    hdma_hrtim1_tima.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_hrtim1_tima) != HAL_OK)
      Error_Handler();
    __HAL_LINKDMA(hhrtim, hdmaTimerA, hdma_hrtim1_tima);

    /* DMA1_Ch3: HRTIM1_TIMB → samples[1][] */
    hdma_hrtim1_timb.Instance = DMA1_Channel3;
    hdma_hrtim1_timb.Init.Request = DMA_REQUEST_HRTIM1_B;
    hdma_hrtim1_timb.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_hrtim1_timb.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_hrtim1_timb.Init.MemInc = DMA_MINC_ENABLE;
    hdma_hrtim1_timb.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_hrtim1_timb.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_hrtim1_timb.Init.Mode = DMA_CIRCULAR;
    hdma_hrtim1_timb.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_hrtim1_timb) != HAL_OK)
      Error_Handler();
    __HAL_LINKDMA(hhrtim, hdmaTimerB, hdma_hrtim1_timb);

    /* DMA IRQs — priority lower than nothing (pure HW) */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
  }
}

void HAL_HRTIM_MspDeInit(HRTIM_HandleTypeDef *hhrtim) {
  if (hhrtim->Instance == HRTIM1) {
    __HAL_RCC_HRTIM1_CLK_DISABLE();
    HAL_DMA_DeInit(&hdma_hrtim1_tima);
    HAL_DMA_DeInit(&hdma_hrtim1_timb);
    HAL_NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel3_IRQn);
  }
}
/* USER CODE END 1 */
