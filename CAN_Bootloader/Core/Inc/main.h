/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_adc.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_ADC1_Init(void);
void MX_CAN1_Init(void);
void MX_CAN2_Init(void);
void MX_TIM1_Init(void);
void MX_TIM3_Init(void);
void MX_USART2_UART_Init(void);
void MX_I2C2_Init(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin LL_GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define BSPD_Pin LL_GPIO_PIN_1
#define BSPD_GPIO_Port GPIOC
#define HV_Present_Pin LL_GPIO_PIN_2
#define HV_Present_GPIO_Port GPIOC
#define DI_ENABLE_1_Pin LL_GPIO_PIN_3
#define DI_ENABLE_1_GPIO_Port GPIOC
#define APPS1_Pin LL_GPIO_PIN_0
#define APPS1_GPIO_Port GPIOA
#define APPS2_Pin LL_GPIO_PIN_1
#define APPS2_GPIO_Port GPIOA
#define USART_TX_Pin LL_GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin LL_GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define BSE1_Pin LL_GPIO_PIN_4
#define BSE1_GPIO_Port GPIOA
#define LD2_Pin LL_GPIO_PIN_5
#define LD2_GPIO_Port GPIOA
#define MCU_PUMPS_Pin LL_GPIO_PIN_6
#define MCU_PUMPS_GPIO_Port GPIOA
#define MCU_FANS_Pin LL_GPIO_PIN_7
#define MCU_FANS_GPIO_Port GPIOA
#define DI_ENABLE_2_Pin LL_GPIO_PIN_4
#define DI_ENABLE_2_GPIO_Port GPIOC
#define DI_ENABLE_3_Pin LL_GPIO_PIN_5
#define DI_ENABLE_3_GPIO_Port GPIOC
#define SPS_Pin LL_GPIO_PIN_0
#define SPS_GPIO_Port GPIOB
#define BSE2_Pin LL_GPIO_PIN_1
#define BSE2_GPIO_Port GPIOB
#define Brake_Light_Pin LL_GPIO_PIN_14
#define Brake_Light_GPIO_Port GPIOB
#define DI_ENABLE_4_Pin LL_GPIO_PIN_6
#define DI_ENABLE_4_GPIO_Port GPIOC
#define RTD_Speaker_Pin LL_GPIO_PIN_8
#define RTD_Speaker_GPIO_Port GPIOA
#define PWM_Debug_Pin LL_GPIO_PIN_9
#define PWM_Debug_GPIO_Port GPIOA
#define TMS_Pin LL_GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin LL_GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define IMD_OK_Pin LL_GPIO_PIN_10
#define IMD_OK_GPIO_Port GPIOC
#define TS_Disabled_Pin LL_GPIO_PIN_11
#define TS_Disabled_GPIO_Port GPIOC
#define MCU_INTLCK_Pin LL_GPIO_PIN_12
#define MCU_INTLCK_GPIO_Port GPIOC
#define AMS_OK_Pin LL_GPIO_PIN_2
#define AMS_OK_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
