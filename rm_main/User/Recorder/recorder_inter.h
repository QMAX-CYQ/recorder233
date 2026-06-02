#ifndef __RECPRDER_INTER_H
#define __RECPRDER_INTER_H

#define USE_RECORDER 1  //总开关
#define USE_RTC 1  //当设备支持RTC时
#define TSKD_FRE 500 //单位毫秒

#define KB 1024UL
#define MB (1024UL * KB)

#define PCFILE_BUFFER_SIZE (128UL * KB)
#define PCFILE_WRITE_BUFFER_SIZE (1UL * KB)
#define PCFILE_SIZE (8UL * MB)
#define FILE_MAX_NUM 3000
#define FILE_NAME_MAX_LEN 32
#define FREE_SIZE_LIMIT  (23552UL * 1024) //单位KB






#endif

