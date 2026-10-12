/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dma.c
  * @brief   This file provides code for the configuration
  *          of all the requested memory to memory DMA transfers.
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
#include "dma.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure DMA                                                              */
/*----------------------------------------------------------------------------*/

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Enable DMA controller clock
  */
void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);

}

/* USER CODE BEGIN 2 */

/* DMA1_Channel6_IRQn interrupt configuration
 * Priority 0 is reserved for interrupts that must NOT call any FreeRTOS API.
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY is 5, so any ISR that needs
 * "...FromISR" APIs must sit at priority >= 5.
 *
 * Headroom, not a requirement: RX uses a DMA circular buffer read by
 * polling CNDTR, and no ISR in this project calls any RTOS API (there is
 * not a single ...FromISR call anywhere).  Sitting at 5 only matters if
 * one is ever added.
 *
 * The .ioc already pins this to 5 (NVIC.DMA1_Channel6_IRQn), so
 * "Generate Code" will not revert it to 0.
 *
 * ↑ 上面这段原先写在 MX_DMA_Init() 里 HAL_NVIC_SetPriority 的正上方, 那个位置
 *   在 USER CODE 区块之外, 2026-10-10 一次 Generate Code 把它删掉了.
 *   现挪进这个 USER CODE 区块, 以后再生成不会再丢. */

/* USER CODE END 2 */

