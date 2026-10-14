/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */
#include "rtc.h"

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;
  HAL_StatusTypeDef time_status;
  HAL_StatusTypeDef date_status;
  uint32_t irq_state;
  /* RTC 不可用时返回有效的固定日期：2000-01-01 00:00:00。 */
  const DWORD fallback_time = ((DWORD)20U << 25) | ((DWORD)1U << 21) | ((DWORD)1U << 16);

  if (hrtc.Instance == NULL)
    return fallback_time;

  /* 必须先读时间再读日期，以释放 RTC 影子寄存器锁。
   * 短临界区避免其他任务在两次读取之间访问 RTC；不使用共享日历变量。 */
  irq_state = __get_PRIMASK();
  __disable_irq();
  time_status = HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
  date_status = HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);
  __set_PRIMASK(irq_state);

  if ((time_status != HAL_OK) || (date_status != HAL_OK) ||
      (rtc_date.Year > 99U) || (rtc_date.Month < 1U) || (rtc_date.Month > 12U) ||
      (rtc_date.Date < 1U) || (rtc_date.Date > 31U) ||
      (rtc_time.Hours > 23U) || (rtc_time.Minutes > 59U) || (rtc_time.Seconds > 59U))
    return fallback_time;

  /* RTC 年份以 2000 为基准，FAT 年份以 1980 为基准；秒字段精度为 2 秒。 */
  return ((DWORD)(rtc_date.Year + 20U) << 25) |
         ((DWORD)rtc_date.Month << 21) |
         ((DWORD)rtc_date.Date << 16) |
         ((DWORD)rtc_time.Hours << 11) |
         ((DWORD)rtc_time.Minutes << 5) |
         ((DWORD)rtc_time.Seconds >> 1);
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
