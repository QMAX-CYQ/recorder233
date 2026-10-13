/*¿ØÖÆ¿ª¹Ø*/
#ifndef __CONTROL_DEF_H
#define __CONTROL_DEF_H

#define INFO_FROM_DEVICE

#ifdef INFO_FROM_DEVICE
	#define USB_COMM

#else
	#define DEBUG_USE

#endif //#if INFO_FROM_DEVICE



#endif //#ifndef __CONTROL_DEF_H
