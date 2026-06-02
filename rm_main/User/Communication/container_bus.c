#include "container_bus.h"
#include "container.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>
#include <stdio.h>

// 内部订阅者节点
typedef struct Subscriber {
    uint32_t tag_id;
    BusCallback cb_set;
    BusCallback cb_get;
    
    uint32_t last_frame_id;
    uint32_t last_read_count;
    
    struct Subscriber* next;
} Subscriber;

// Bus 单例
static Subscriber* g_sub_head = NULL;

int container_bus_init(const ContainerBusCfg* cfg_table, size_t count) {
    if (!cfg_table || count == 0) return -1;
    
    // 遍历传入的数组，自动构建监听链表
    for (size_t i = 0; i < count; i++) {
        Subscriber* sub = (Subscriber*)malloc(sizeof(Subscriber));
        if (!sub) continue; // 内存不足跳过
        
        sub->tag_id = cfg_table[i].tag_id;
        sub->cb_set = cfg_table[i].cb_set;
        sub->cb_get = cfg_table[i].cb_get;
        
        // 初始化状态：尝试获取当前的 FrameID，防止刚启动就误触发
        ContainerStat stat;
        if (container_get_stat(sub->tag_id, &stat) == 0) {
            sub->last_frame_id = stat.frame_id;
            sub->last_read_count = stat.read_count;
        } else {
            sub->last_frame_id = 0;
            sub->last_read_count = 0;
        }
        
        // 插入链表 (头插法)
        taskENTER_CRITICAL();
        sub->next = g_sub_head;
        g_sub_head = sub;
        taskEXIT_CRITICAL();
    }
    
    return 0;
}

void container_bus_poll(void) {
    Subscriber* sub = g_sub_head;
    
    while (sub) {
        ContainerStat stat;
        
        // 调用 container.c 的全局接口
        if (container_get_stat(sub->tag_id, &stat) == 0) {
            
            // 1. Check SET
            if (stat.frame_id > sub->last_frame_id) {
                sub->last_frame_id = stat.frame_id;
                if (sub->cb_set) {
                    void* ptr = NULL;
                    size_t len = 0;
                    container_get(sub->tag_id, &ptr, &len, NULL);
                    sub->cb_set(sub->tag_id, ptr, len);
                }
            }
            
            // 2. Check GET
            if (stat.read_count > sub->last_read_count) {
                sub->last_read_count = stat.read_count;
                if (sub->cb_get) {
                    void* ptr = NULL;
                    size_t len = 0;
                    container_get(sub->tag_id, &ptr, &len, NULL);
                    sub->cb_get(sub->tag_id, ptr, len);
                }
            }
        }
        sub = sub->next;
    }
}
