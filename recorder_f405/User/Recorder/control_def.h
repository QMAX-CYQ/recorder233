/*¿ØÖÆ¿ª¹Ø*/
#ifndef __CONTROL_DEF_H
#define __CONTROL_DEF_H

#define INFO_FROM_DEVICE

#ifdef INFO_FROM_DEVICE
	#define USB_COMM

#else
	#define DEBUG_USE

#endif //#if INFO_FROM_DEVICE

#ifndef RTC_FLASH_SAVE_ENABLED
	#define RTC_FLASH_SAVE_ENABLED 0U
#endif

#define RTC_INITIAL_YEAR         2026U
#define RTC_INITIAL_MONTH        10U
#define RTC_INITIAL_DAY          13U
#define RTC_INITIAL_HOUR         18U
#define RTC_INITIAL_MINUTE       53U
#define RTC_INITIAL_SECOND       0U

#endif //#ifndef __CONTROL_DEF_H
