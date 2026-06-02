#include "container.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_CAPACITY 512
#define ALIGN_SIZE 4
#define ALIGN_UP(size) (((size) + (ALIGN_SIZE - 1)) & ~(ALIGN_SIZE - 1))

// ---------------------------------------------------------
// 内部结构
// ---------------------------------------------------------

typedef struct Node {
    uint32_t tag_id;
    ContainerDataType type;
    size_t offset;
    size_t length;
    uint32_t frame_id;   
    uint32_t read_count; 
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    uint8_t* buffer;
    size_t capacity;
    size_t used_size;
    SemaphoreHandle_t mutex;
} Container;

// === 隐式全局单例 ===
static Container* g_instance = NULL;

// ---------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------

static Node* find_node(uint32_t tag_id) {
    Node* cur = g_instance->head;
    while (cur) {
        if (cur->tag_id == tag_id) return cur;
        cur = cur->next;
    }
    return NULL;
}

static int ensure_capacity(size_t required_len) {
    size_t needed_total = g_instance->used_size + required_len;

    if (g_instance->buffer == NULL) {
        size_t start_size = (required_len > INITIAL_CAPACITY) ? required_len : INITIAL_CAPACITY;
        start_size = ALIGN_UP(start_size);
        g_instance->buffer = (uint8_t*)malloc(start_size);
        if (!g_instance->buffer) return -1;
        g_instance->capacity = start_size;
        return 0;
    }

    if (needed_total > g_instance->capacity) {
        size_t new_cap = g_instance->capacity;
        while (new_cap < needed_total) new_cap *= 2; 
        
        uint8_t* new_buf = (uint8_t*)realloc(g_instance->buffer, new_cap);
        if (!new_buf) return -1;
        
        g_instance->buffer = new_buf;
        g_instance->capacity = new_cap;
    }
    return 0;
}

// ---------------------------------------------------------
// API 实现
// ---------------------------------------------------------

void container_sys_init(void) {
    if (g_instance != NULL) return; // 防止重复初始化

    g_instance = (Container*)malloc(sizeof(Container));
    if (g_instance) {
        memset(g_instance, 0, sizeof(Container));
        g_instance->mutex = xSemaphoreCreateMutex();
    }
}

// 内部函数：确保已初始化
static int check_init(void) {
    if (g_instance == NULL) {
        // 如果用户忘记调用 init，这里尝试补救（但在多线程下不安全，建议显式调用 init）
        container_sys_init(); 
        if (g_instance == NULL) return -1;
    }
    return 0;
}

int container_set(uint32_t tag_id, const void* data, size_t data_len, ContainerDataType type) 
{
	if (check_init() != 0) return -1;
	if (!data || data_len == 0) return -1;

	// 上锁
	if (xSemaphoreTake(g_instance->mutex, portMAX_DELAY) != pdTRUE) return -1;

	Node* node = find_node(tag_id);


	size_t current_offset = g_instance->used_size;
	size_t aligned_offset = ALIGN_UP(current_offset);
	size_t padding = aligned_offset - current_offset;
	size_t total_needed = padding + data_len;

	if (ensure_capacity(total_needed) != 0) {
		xSemaphoreGive(g_instance->mutex);
		return CONTAINER_ERROR_NOMEM_PARAM; 
	}

	if (node) {
		if (data_len > node->length) {
			// 不能覆盖，因为新数据更大（固定缓冲区无法扩容）
			xSemaphoreGive(g_instance->mutex);
			return CONTAINER_ERROR_INVALID_PARAM;
		}
		// 2. 直接覆盖原内存（不重新分配）
		memcpy(g_instance->buffer + node->offset, data, data_len);
		// 3. 更新长度和帧ID
		node->length = data_len;
		node->frame_id++;
	} else {
		 Node* new_node = (Node*)malloc(sizeof(Node));
		 if (new_node) {
		 new_node->tag_id = tag_id;
		 new_node->type = type;
		 new_node->offset = aligned_offset;
		 new_node->length = data_len;
		 new_node->frame_id = 1;
		 new_node->read_count = 0;
		 new_node->next = g_instance->head;
		 g_instance->head = new_node;
		 g_instance->used_size = aligned_offset + data_len;
	 } else {
		 xSemaphoreGive(g_instance->mutex);
		 return CONTAINER_ERROR_NOMEM_PARAM;
	 }
 }

	xSemaphoreGive(g_instance->mutex);
	return 0;
}

int container_get(uint32_t tag_id, void** out_data, size_t* out_len, ContainerDataType* out_type) {
    if (check_init() != 0) return -1;
    if (!out_data) return -1;

    if (xSemaphoreTake(g_instance->mutex, portMAX_DELAY) != pdTRUE) return -1;

    Node* node = find_node(tag_id);
    if (!node) {
        xSemaphoreGive(g_instance->mutex);
        return -1; 
    }

    *out_data = (g_instance->buffer + node->offset);
    if (out_len) *out_len = node->length;
    if (out_type) *out_type = node->type;
    node->read_count++; 

    xSemaphoreGive(g_instance->mutex);
    return 0;
}

int container_get_stat(uint32_t tag_id, ContainerStat* out_stat) {
    if (check_init() != 0) return -1;
    if (!out_stat) return -1;

    if (xSemaphoreTake(g_instance->mutex, portMAX_DELAY) != pdTRUE) return -1;

    Node* node = find_node(tag_id);
    if (!node) {
        xSemaphoreGive(g_instance->mutex);
        return -1; 
    }

    out_stat->frame_id = node->frame_id;
    out_stat->read_count = node->read_count;
    out_stat->length = node->length;

    xSemaphoreGive(g_instance->mutex);
    return 0;
}

void container_dump_info(void) {
    if (check_init() != 0) return;
    
    xSemaphoreTake(g_instance->mutex, portMAX_DELAY);
    printf("\n--- Container Dump ---\n");
    printf("Capacity : %zu bytes\n", g_instance->capacity);
    Node* cur = g_instance->head;
    while(cur) {
        printf("  [ID:%u] FrmID:%u Off:%zu\n", cur->tag_id, cur->frame_id, cur->offset);
        cur = cur->next;
    }
    printf("----------------------\n");
    xSemaphoreGive(g_instance->mutex);
}
