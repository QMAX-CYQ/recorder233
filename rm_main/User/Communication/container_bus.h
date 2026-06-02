#ifndef _CONTAINER_BUS_H_
#define _CONTAINER_BUS_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif



/*
 * 回调函数类型
 */
typedef void (*BusCallback)(uint32_t tag_id, void* data, size_t len);

/**
 * @brief Bus 配置项结构体
 * @note 用于构建配置数组
 */
typedef struct {
    uint32_t tag_id;      // 监听的 Tag
    BusCallback cb_set;   // 当数据被 Set 时触发 (可为 NULL)
    BusCallback cb_get;   // 当数据被 Get 时触发 (可为 NULL)
} ContainerBusCfg;

/**
 * @brief 初始化 Bus 系统
 * @param cfg_table 配置数组指针
 * @param count 数组元素个数
 * @return 0 成功
 */
int container_bus_init(const ContainerBusCfg* cfg_table, size_t count);

/**
 * @brief 轮询任务 (需要在任务中循环调用)
 * @note 自动遍历配置表，检查数据变化并触发回调
 */
void container_bus_poll(void);

#ifdef __cplusplus
}
#endif

#endif // _CONTAINER_BUS_H_




/* example
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#include "container.h"
#include "container_bus.h"

// --- 1. 定义回调函数 ---

void on_imu_data(uint32_t tag_id, void* data, size_t len) {
    float angle = *(float*)data;
    printf("[BUS] IMU Angle Updated: %.2f\n", angle);
}

void on_motor_cmd(uint32_t tag_id, void* data, size_t len) {
    int cmd = *(int*)data;
    printf("[BUS] Motor Command: %d\n", cmd);
}

// --- 2. 定义配置表 (Array) ---

static const ContainerBusCfg my_bus_config[] = {
    // { TagID, SetCallback, GetCallback }
    { 0x10,  on_imu_data,   NULL },
    { 0x20,  on_motor_cmd,  NULL },
    // 可以继续添加...
};

// --- 3. 任务实现 ---

void Task_Sensor(void *arg) {
    float angle = 0.0f;
    while(1) {
        angle += 1.5f;
        // 直接调用 set，不需要传入任何句柄
        container_set(0x10, &angle, sizeof(float), CONTAINER_TYPE_FLOAT);
        osDelay(100);
    }
}

void Task_Controller(void *arg) {
    int cmd = 1000;
    while(1) {
        cmd += 10;
        // 直接调用 set
        container_set(0x20, &cmd, sizeof(int), CONTAINER_TYPE_INT);
        osDelay(500);
    }
}

void Task_BusPoll(void *arg) {
    while(1) {
        // 轮询并自动触发回调
        container_bus_poll();
        osDelay(20);
    }
}

// --- Main ---

int main(void) {
    // HAL_Init()...
    
    printf("System Start\n");

    // 1. 系统初始化 (必须先做)
    container_sys_init();

    // 2. Bus 初始化 (传入配置表)
    container_bus_init(my_bus_config, sizeof(my_bus_config)/sizeof(ContainerBusCfg));

    // 3. 创建任务
    xTaskCreate(Task_Sensor, "Sensor", 256, NULL, 2, NULL);
    xTaskCreate(Task_Controller, "Ctrl", 256, NULL, 2, NULL);
    xTaskCreate(Task_BusPoll, "Bus", 512, NULL, 3, NULL);

    vTaskStartScheduler();
    
    while(1);
}






















*/