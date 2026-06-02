#ifndef _CONTAINER_H_
#define _CONTAINER_H_

#include <stdint.h>
#include <stddef.h>

// 引入 FreeRTOS
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// 支持的数据类型
typedef enum {
    CONTAINER_TYPE_UNKNOWN = 0,
    CONTAINER_TYPE_INT,
    CONTAINER_TYPE_FLOAT,
    CONTAINER_TYPE_STRING,
    CONTAINER_TYPE_STRUCT,
    CONTAINER_TYPE_RAW
} ContainerDataType;

// 状态结构体
typedef struct {
    uint32_t frame_id;   
    uint32_t read_count; 
    size_t length;       
} ContainerStat;

typedef enum {
	CONTAINER_ERROR_INVALID_PARAM = -1,
	CONTAINER_ERROR_NOMEM_PARAM = -2,
} ContainerErrorType;

/**
 * @brief 系统初始化 (建议在 main 函数开始时调用一次)
 * @note 初始化内部的单例对象和互斥锁
 */
void container_sys_init(void);

/**
 * @brief 存入数据 (不需要句柄，直接调用)
 * @note 线程安全
 */
int container_set(uint32_t tag_id, const void* data, size_t data_len, ContainerDataType type);

/**
 * @brief 获取数据
 * @note 线程安全，零拷贝
 */
int container_get(uint32_t tag_id, void** out_data, size_t* out_len, ContainerDataType* out_type);

/**
 * @brief 获取状态 (用于 Bus 轮询)
 */
int container_get_stat(uint32_t tag_id, ContainerStat* out_stat);

/**
 * @brief 打印调试信息
 */
void container_dump_info(void);

#ifdef __cplusplus
}
#endif

#endif // _CONTAINER_H_
