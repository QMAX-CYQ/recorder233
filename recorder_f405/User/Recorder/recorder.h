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
void RecorderData(const char *buf,size_t len);
FRESULT SdCardInit(void);


#endif

