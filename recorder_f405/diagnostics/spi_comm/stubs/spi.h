#ifndef TEST_SPI_H
#define TEST_SPI_H
#include <stdint.h>
#define __attribute__(x)
#define DMA_CIRCULAR 1U
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_BUSY 2
typedef int HAL_StatusTypeDef;
typedef struct { struct { uint32_t Mode; } Init; uint32_t count, tc; } DMA_HandleTypeDef;
typedef struct { DMA_HandleTypeDef *hdmarx; uint32_t ErrorCode; } SPI_HandleTypeDef;
extern SPI_HandleTypeDef hspi1;
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))
void test_barrier(void);
#define __DMB() test_barrier()
#define __HAL_DMA_GET_FLAG(h, f) ((void)(f), (h)->tc)
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) 1U
#define __HAL_DMA_GET_COUNTER(h) ((h)->count)
#define __HAL_SPI_CLEAR_OVRFLAG(h) ((void)(h))
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *);
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *);
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *);
#endif
