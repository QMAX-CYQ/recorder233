#include <stdint.h>
typedef struct {uint32_t CCR;} TestSCB;
extern TestSCB test_scb;
#define SCB (&test_scb)
#define SCB_CCR_DC_Msk (1U << 16)
void SCB_CleanDCache_by_Addr(uint32_t *,int32_t);
void SCB_InvalidateDCache_by_Addr(uint32_t *,int32_t);
