/*控制开关*/
#ifndef __CONTROL_DEF_H
#define __CONTROL_DEF_H

#define INFO_FROM_DEVICE

#define COMM_INTERFACE_NONE 0U
#define COMM_INTERFACE_USB  1U
#define COMM_INTERFACE_SPI  2U
#define COMM_INTERFACE_UART 3U

#ifndef COMM_INTERFACE
#ifdef INFO_FROM_DEVICE
#define COMM_INTERFACE COMM_INTERFACE_SPI
#else
#define COMM_INTERFACE COMM_INTERFACE_NONE
#endif
#endif

#if COMM_INTERFACE == COMM_INTERFACE_USB
#define USB_COMM
#elif COMM_INTERFACE == COMM_INTERFACE_SPI
#define SPI_COMM
#elif COMM_INTERFACE == COMM_INTERFACE_UART
#error "UART communication backend is reserved but not implemented."
#elif COMM_INTERFACE != COMM_INTERFACE_NONE
#error "Invalid COMM_INTERFACE selection."
#endif

#ifndef INFO_FROM_DEVICE
	#define DEBUG_USE

#endif //#if INFO_FROM_DEVICE

#ifndef RTC_FLASH_SAVE_ENABLED
	#define RTC_FLASH_SAVE_ENABLED 0U
#endif

#define RTC_INITIAL_YEAR         2026U
#define RTC_INITIAL_MONTH        10U
#define RTC_INITIAL_DAY          23U
#define RTC_INITIAL_HOUR         22U
#define RTC_INITIAL_MINUTE       29U
#define RTC_INITIAL_SECOND       0U

#endif //#ifndef __CONTROL_DEF_H
