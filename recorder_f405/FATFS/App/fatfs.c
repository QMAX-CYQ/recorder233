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
#include "calendar.h"

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
  /* RTC 不可用时返回有效的固定日期：2000-01-01 00:00:00。 */
  const DWORD fallback_time = ((DWORD)20U << 25) | ((DWORD)1U << 21) | ((DWORD)1U << 16);

  /* 与日志取时共用完整日历校验和并发保护。 */
  if (!RTC_ReadSnapshot(&rtc_time, &rtc_date))
  {
    RTC_RecordFatTimeFallback();
    return fallback_time;
  }

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
