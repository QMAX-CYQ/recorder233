#ifndef TEST_SPI_H
#define TEST_SPI_H
#include <stdint.h>
#include <stddef.h>
#define __attribute__(x)
#define SPI_MODE_MASTER 1U
#define SPI_DIRECTION_2LINES_TXONLY 2U
#define SPI_DATASIZE_8BIT 8U
#define DMA_NORMAL 0U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_BYTE 0U
#define HAL_SPI_STATE_READY 0U
#define HAL_OK 0
typedef int HAL_StatusTypeDef;
typedef struct { struct { uint32_t Mode, PeriphDataAlignment, MemDataAlignment; } Init; } DMA_HandleTypeDef;
typedef struct {
    struct { uint32_t Mode, Direction, DataSize; } Init;
    DMA_HandleTypeDef *hdmatx;
    uint32_t ErrorCode, State;
} SPI_HandleTypeDef;
extern SPI_HandleTypeDef hspi1;
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))
#define __DSB() ((void)0)
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *);
uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *);
#endif
