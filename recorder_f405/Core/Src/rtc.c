/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
#include "rtc.h"

/* USER CODE BEGIN 0 */

#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"
#include "control_def.h"

#define RTC_INIT_MARKER 0x52544332UL
#define RTC_LEGACY_BKUP_INIT_MARKER 0x52544331UL
#define RTC_INIT_FLASH_ADDRESS 0x080E0000UL

typedef struct
{
  uint32_t marker;
  uint32_t hours;
  uint32_t minutes;
  uint32_t seconds;
  uint32_t weekday;
  uint32_t month;
  uint32_t date;
  uint32_t year;
} RTC_InitRecord;

static uint8_t RTC_ReadInitRecord(RTC_InitRecord *record)
{
  volatile const RTC_InitRecord *flashRecord =
      (volatile const RTC_InitRecord *)RTC_INIT_FLASH_ADDRESS;

  if (flashRecord->marker != RTC_INIT_MARKER)
  {
    return 0U;
  }

  record->hours = flashRecord->hours;
  record->minutes = flashRecord->minutes;
  record->seconds = flashRecord->seconds;
  record->weekday = flashRecord->weekday;
  record->month = flashRecord->month;
  record->date = flashRecord->date;
  record->year = flashRecord->year;
  return 1U;
}

static uint8_t RTC_ToBcd(uint8_t value)
{
  return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static uint8_t RTC_GetWeekday(uint16_t year, uint8_t month, uint8_t day)
{
  static const uint8_t daysPerMonth[] =
      {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
  uint32_t days = 0U;

  for (uint16_t y = 2000U; y < year; y++)
  {
    days += (((y % 4U) == 0U) ? 366U : 365U);
  }
  for (uint8_t m = 1U; m < month; m++)
  {
    days += daysPerMonth[m - 1U];
    if ((m == 2U) && ((year % 4U) == 0U))
    {
      days++;
    }
  }
  days += (uint32_t)day - 1U;

  /* 2000-01-01 was Saturday; HAL numbers weekdays Monday=1 through Sunday=7. */
  return (uint8_t)(((days + 5U) % 7U) + 1U);
}

HAL_StatusTypeDef RTC_SetInitialTime(uint16_t year,
                                     uint8_t month,
                                     uint8_t day,
                                     uint8_t hour,
                                     uint8_t minute,
                                     uint8_t second)
{
#if RTC_FLASH_SAVE_ENABLED
  RTC_InitRecord record = {0};
  RTC_InitRecord storedRecord = {0};
  uint8_t hasStoredRecord = RTC_ReadInitRecord(&storedRecord);
  static const uint8_t daysPerMonth[] =
      {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

  if ((year < 2000U) || (year > 2099U) ||
      (month < 1U) || (month > 12U) ||
      (hour > 23U) || (minute > 59U) || (second > 59U))
  {
    return HAL_ERROR;
  }

  uint8_t maxDay = daysPerMonth[month - 1U];
  if ((month == 2U) && ((year % 4U) == 0U))
  {
    maxDay++;
  }
  if ((day < 1U) || (day > maxDay))
  {
    return HAL_ERROR;
  }

  record.hours = RTC_ToBcd(hour);
  record.minutes = RTC_ToBcd(minute);
  record.seconds = RTC_ToBcd(second);
  record.weekday = RTC_GetWeekday(year, month, day);
  record.month = RTC_ToBcd(month);
  record.date = RTC_ToBcd(day);
  record.year = RTC_ToBcd((uint8_t)(year - 2000U));

  if ((hasStoredRecord != 0U) &&
      (storedRecord.hours == record.hours) &&
      (storedRecord.minutes == record.minutes) &&
      (storedRecord.seconds == record.seconds) &&
      (storedRecord.weekday == record.weekday) &&
      (storedRecord.month == record.month) &&
      (storedRecord.date == record.date) &&
      (storedRecord.year == record.year))
  {
    return HAL_OK;
  }

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return HAL_ERROR;
  }

  FLASH_EraseInitTypeDef eraseConfig = {0};
  uint32_t sectorError = 0;
  eraseConfig.TypeErase = FLASH_TYPEERASE_SECTORS;
  eraseConfig.Sector = FLASH_SECTOR_11;
  eraseConfig.NbSectors = 1;
  eraseConfig.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  if (HAL_FLASHEx_Erase(&eraseConfig, &sectorError) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return HAL_ERROR;
  }

  const uint32_t recordWords[] =
  {
    RTC_INIT_MARKER,
    record.hours,
    record.minutes,
    record.seconds,
    record.weekday,
    record.month,
    record.date,
    record.year
  };

  /* Write the marker last so incomplete records are ignored on the next boot. */
  for (uint32_t i = 1; i < (sizeof(recordWords) / sizeof(recordWords[0])); i++)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          RTC_INIT_FLASH_ADDRESS + (i * sizeof(uint32_t)),
                          recordWords[i]) != HAL_OK)
    {
      HAL_FLASH_Lock();
      return HAL_ERROR;
    }
  }

  if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                        RTC_INIT_FLASH_ADDRESS,
                        recordWords[0]) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return HAL_ERROR;
  }
  HAL_FLASH_Lock();

  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};
  time.Hours = (uint8_t)record.hours;
  time.Minutes = (uint8_t)record.minutes;
  time.Seconds = (uint8_t)record.seconds;
  time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BCD) != HAL_OK)
  {
    return HAL_ERROR;
  }

  date.WeekDay = (uint8_t)record.weekday;
  date.Month = (uint8_t)record.month;
  date.Date = (uint8_t)record.date;
  date.Year = (uint8_t)record.year;
  if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BCD) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_INIT_MARKER);
  return HAL_OK;
#else
  return HAL_ERROR;
#endif
}

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  uint32_t initMarker = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
  if (initMarker == RTC_LEGACY_BKUP_INIT_MARKER)
  {
    /* Upgrade the old backup marker without changing the running calendar. */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_INIT_MARKER);
  }
  else if (initMarker != RTC_INIT_MARKER)
  {
    /* Set the calendar only when the backup domain has no valid init marker. */
    RTC_InitRecord initRecord = {0};
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if (RTC_ReadInitRecord(&initRecord) == 0U)
    {
      initRecord.hours = 0x22;
      initRecord.minutes = 0x03;
      initRecord.seconds = 0x50;
      initRecord.weekday = RTC_WEEKDAY_THURSDAY;
      initRecord.month = RTC_MONTH_JANUARY;
      initRecord.date = 0x01;
      initRecord.year = 0x26;
    }

    sTime.Hours = initRecord.hours;
    sTime.Minutes = initRecord.minutes;
    sTime.Seconds = initRecord.seconds;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
    {
      Error_Handler();
    }

    sDate.WeekDay = initRecord.weekday;
    sDate.Month = initRecord.month;
    sDate.Date = initRecord.date;
    sDate.Year = initRecord.year;
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
    {
      Error_Handler();
    }

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_INIT_MARKER);
  }

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

