#ifndef __RECORDER_H
#define __RECORDER_H

#include "stdio.h"
#include "fatfs.h"

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t index;
    uint32_t create_seq;
} recorder_file_header_t;

typedef enum
{
  RS_INIT = 0,
  RS_READY,
  RS_CLOSE,
  RS_ERROR
}file_state_t;


typedef struct 
{
   file_state_t state;
   
   uint32_t current_index;
   uint32_t oldest_index;

}pcfile_status_t;

typedef struct 
{

  void (*init) (void);
}recorder_t;
/* Log-data writes only; excludes file headers and index metadata. */
typedef struct {
    uint32_t ring_attempts, ring_accepted_bytes, ring_rejected, ring_rejected_bytes;
    uint32_t dequeued_bytes, write_attempts, write_error, write_short;
    uint32_t write_reported_bytes, unconfirmed_bytes;
    uint32_t last_write_result, last_write_bytes, write_active;
    uint32_t last_write_ms, max_write_ms;
    uint32_t sync_attempts, sync_error, last_sync_result, max_sync_ms;
    uint32_t last_write_tick, last_sync_tick, last_write_error_tick;
    uint32_t mount_result, card_not_ready;
} recorder_diag_t;
extern volatile recorder_diag_t g_recorder_diag;

void RecorderData(const char *buf,size_t len);
uint8_t RecorderDataWrite(const char *buf, size_t len);
uint8_t RecorderDataGetBufferStatus(size_t *free_size);
FRESULT SdCardInit(void);
/* 文件头已经写入并同步、文件可以接收日志。 */
uint8_t RecorderLogIsReady(void);


#endif

