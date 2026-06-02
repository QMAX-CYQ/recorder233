#include "recorder.h"
#include "task.h" 
#include "lwrb.h"
#include "calendar.h"
#include "log.h"
#include "data_log.h"
#include "recorder_inter.h"
#include "sdmmc.h"


#if USE_RECORDER

#define FILE_NAME "Rb"  //日志文件前缀
#define OLDEST_FILE_NAME "oldest.txt"  //记录最早创建文件索引
#define LASTEST_FILE_NAME "lastest.txt"  //记录最新创建文件索引


#define WRITECOUNT_LIMIT 4
#define CREATE_SEQ_FILE_NAME "create_seq.txt"
#define RECORDER_FILE_MAGIC 0x52454344UL
#define RECORDER_FILE_VERSION 1UL

BYTE pcfile_buffer[PCFILE_BUFFER_SIZE];
BYTE pcfile_write_buffer[PCFILE_WRITE_BUFFER_SIZE];

lwrb_t pcfile_buffer_lwrb;

FIL pcfile_now;
FIL pcfile_lastest;
FIL pcfile_oldest;

BYTE current_filepath[FILE_NAME_MAX_LEN];

pcfile_status_t pcfile_status;
FRESULT isSdCardInit = FR_DISK_ERR; //初始状态为未挂载

static uint32_t g_create_seq = 0;
static uint8_t sd_not_ready_count = 0;
uint32_t check_write_ok_count = 0;
uint32_t check_write_fail_count = 0;

FRESULT SdCardInit(void)
{
   isSdCardInit = f_mount(&SDFatFS,(TCHAR const*)SDPath,1);  //SD卡挂载

   return isSdCardInit;
}



static uint8_t IsSdCardExist(void)
{
   if(BSP_SD_GetCardState() == SD_TRANSFER_OK)
   {
      sd_not_ready_count = 0;
      return 1;
   }

   if(++sd_not_ready_count >= 3)
   {
      f_close(&pcfile_now);
      memset(&pcfile_now, 0, sizeof(pcfile_now));
   }

   return 0;
}


//同步缓存到文件
static void SyncToFile(void)
{
   int write_count = WRITECOUNT_LIMIT;   //每次只写四次来均匀化写入
   UINT write_number = 0;
   FIL *temp = &pcfile_now;
   size_t used_size;
   uint8_t wrote = 0;
   FRESULT check_res = FR_DISK_ERR;
   FRESULT check_sync_res = FR_DISK_ERR;
   
   while(write_count > 0)
   {
		  used_size = lwrb_get_full(&pcfile_buffer_lwrb);
      if (used_size >= PCFILE_WRITE_BUFFER_SIZE)
      {
         lwrb_read(&pcfile_buffer_lwrb, pcfile_write_buffer, PCFILE_WRITE_BUFFER_SIZE);

         check_res = f_write(temp,pcfile_write_buffer,PCFILE_WRITE_BUFFER_SIZE,&write_number);
		   if((check_res != FR_OK) || (write_number != PCFILE_WRITE_BUFFER_SIZE))
       {
          check_write_fail_count++;
          break;
       }
       wrote = 1;
      }
      else 
      {
         break;
      }
   
     write_count --;
   }

   if(wrote)
   {
      check_sync_res = f_sync(temp);
      if(check_sync_res == FR_OK)
      {
         check_write_ok_count++;
      }
      else
      {
         check_write_fail_count++;
      }
   }
	 

} 


static uint8_t IsRecorderName(const char *file_name, uint32_t *index)
{
    unsigned int tmp;
    char tail;
    
    if(file_name == NULL || index == NULL)
    return 0;

    if(sscanf(file_name, "Rb%u.txt%c", &tmp, &tail) == 1)
    {
      if(tmp < FILE_MAX_NUM)
      {
         *index = (uint32_t)tmp;
           return 1;
      }
    
    }
    return 0;

}

static uint8_t IndexToFileName(uint32_t index, BYTE *file_name)
{
    if(index <= FILE_MAX_NUM && file_name != NULL)
    {
      snprintf((char*)file_name, FILE_NAME_MAX_LEN, "%s%u.txt", FILE_NAME, (unsigned int)index);
			return 1;
    }
		 
		return 0;

}

static uint8_t FileNameToIndex(BYTE *file_name,uint32_t *index)
{
   unsigned int tmp;
   char tail;

   if(file_name != NULL && index != NULL)
   {
      if(sscanf((char*)file_name, "Rb%u.txt%c", &tmp, &tail) == 1)
      {
        if (tmp < FILE_MAX_NUM)
        {
          *index = (uint32_t)tmp;
          return 1;
        }
      }

   }
	 return 0;
}

static uint8_t LoadCreateSeq(uint32_t *seq)
{
    FRESULT res;
    FIL fp;
    UINT br;
    BYTE buf[32] = {0};

    if (seq == NULL)
    {
        return 0;
    }

    res = f_open(&fp, (const TCHAR *)CREATE_SEQ_FILE_NAME, FA_OPEN_EXISTING | FA_READ);
    if (res != FR_OK)
    {
        return 0;
    }

    res = f_read(&fp, buf, sizeof(buf) - 1, &br);
    f_close(&fp);
    if ((res != FR_OK) || (br == 0))
    {
        return 0;
    }

    buf[br] = '\0';
    if (sscanf((char *)buf, "%u", seq) != 1)
    {
        return 0;
    }

    return 1;
}

static uint8_t SaveCreateSeq(uint32_t seq)
{
    FRESULT res;
    FIL fp;
    UINT bw;
    BYTE buf[32] = {0};

    snprintf((char *)buf, sizeof(buf), "%u", seq);

    res = f_open(&fp, (const TCHAR *)CREATE_SEQ_FILE_NAME, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return 0;
    }

    res = f_write(&fp, buf, strlen((char *)buf) + 1, &bw);
    f_close(&fp);

    return ((res == FR_OK) && (bw == (strlen((char *)buf) + 1))) ? 1 : 0;
}

static uint8_t ReadRecorderFileHeader(const char *file_name, recorder_file_header_t *header)
{
    FRESULT res;
    FIL fp;
    UINT br;

    if ((file_name == NULL) || (header == NULL))
    {
        return 0;
    }

    res = f_open(&fp, (const TCHAR *)file_name, FA_OPEN_EXISTING | FA_READ);
    if (res != FR_OK)
    {
        return 0;
    }

    res = f_read(&fp, header, sizeof(recorder_file_header_t), &br);
    f_close(&fp);
    if ((res != FR_OK) || (br != sizeof(recorder_file_header_t)))
    {
        return 0;
    }

    if ((header->magic != RECORDER_FILE_MAGIC) ||
        (header->version != RECORDER_FILE_VERSION) ||
        (header->index >= FILE_MAX_NUM))
    {
        return 0;
    }

    return 1;
}

static uint8_t WriteRecorderFileHeader(FIL *fp, uint32_t index)
{
    FRESULT res;
    UINT bw;
    recorder_file_header_t header;

    if (fp == NULL)
    {
        return 0;
    }

    g_create_seq++;

    header.magic = RECORDER_FILE_MAGIC;
    header.version = RECORDER_FILE_VERSION;
    header.index = index;
    header.create_seq = g_create_seq;
   
    res = f_write(fp, &header, sizeof(recorder_file_header_t), &bw);
		if(res != FR_OK)
		{
		  g_create_seq--;
        return 0;
		}

    if (!SaveCreateSeq(g_create_seq))
    {
        g_create_seq--;
        return 0;
    }


    return 1;
}


//判断日志文件是否存在
static uint8_t IsFileExist(uint32_t index)
{
    BYTE file_name[FILE_NAME_MAX_LEN];
    FILINFO fno;

    IndexToFileName(index, file_name);

    if (f_stat((const TCHAR *)file_name, &fno) == FR_OK)
    {
        return 1;
    }
    log_printf("file not exist!\r\n");
    return 0;
}



static uint8_t ReadIndexFromFile(const char *meta_file, uint32_t *index)
{

    FIL fp;
    UINT br;
    FRESULT res;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    if ((meta_file == NULL) || (index == NULL))
    {
        return 0;
    }

    res = f_open(&fp,(const TCHAR*)meta_file, FA_OPEN_EXISTING | FA_READ);
    if (res != FR_OK)
    {
        return 0;
    }

    res = f_read(&fp, file_name, FILE_NAME_MAX_LEN, &br);
    f_close(&fp);
		log_printf("%s\r\n",file_name);

    file_name[br] = '\0';
    return FileNameToIndex(file_name, index);
}

static uint8_t FileIndexUpdate(void)
{
    uint32_t current_index;
    uint32_t oldest_index;

    if (!ReadIndexFromFile(LASTEST_FILE_NAME, &current_index))
    {
        return 0;
    }

    if (!ReadIndexFromFile(OLDEST_FILE_NAME, &oldest_index))
    {
        return 0;
    }

    if (current_index >= FILE_MAX_NUM || oldest_index >= FILE_MAX_NUM)
    {
        return 0;
    }

    pcfile_status.current_index = current_index;
    pcfile_status.oldest_index = oldest_index;

    return 1;
}


static uint8_t DeleteFile(uint32_t index)
{
   BYTE file_name[FILE_NAME_MAX_LEN];
   FRESULT res = FR_DISK_ERR; 

   IndexToFileName(index, file_name);

   res = f_unlink((const TCHAR *)file_name);
   if(res != FR_OK)
   {
     DEBUG_INFO("delete file failed!\r\n");
     return 0;
   }
   
   return 1;
}

//保存最早的索引
static FRESULT SaveOldestIndex(void)
{
    FRESULT res;
    UINT bw;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    IndexToFileName(pcfile_status.oldest_index, file_name);

    res = f_open(&pcfile_oldest, (const TCHAR*)OLDEST_FILE_NAME, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return res;
    }

    res = f_write(&pcfile_oldest, file_name, strlen((char *)file_name) + 1, &bw);
    f_close(&pcfile_oldest);

    return res;
}

//保存最新的索引
static FRESULT SaveLastestIndex(void)
{
    FRESULT res;
    UINT bw;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    IndexToFileName(pcfile_status.current_index, file_name);

    res = f_open(&pcfile_lastest, (const TCHAR*)LASTEST_FILE_NAME, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return res;
    }

    res = f_write(&pcfile_lastest, file_name, strlen((char *)file_name) + 1, &bw);
    f_close(&pcfile_lastest);

    return res;
}

//创建新文件
static void CreateNewFile(void)
{
    FRESULT res = FR_DISK_ERR;
    uint32_t next_index;
    FIL new_file;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    next_index = (pcfile_status.current_index + 1U) % FILE_MAX_NUM;
    IndexToFileName(next_index, file_name);
		
		if(pcfile_status.state != RS_INIT)
		{
		    res = f_close(&pcfile_now);
		    if(res != FR_OK)
       {
         return ;
       }
		
		}

    //新建当前文件
    res = f_open(&pcfile_now, (const TCHAR*)file_name, FA_CREATE_ALWAYS| FA_WRITE | FA_READ);
    if(res != FR_OK)
    {
      return ;
    }

    if (!WriteRecorderFileHeader(&pcfile_now, next_index))
    {
      f_close(&pcfile_now);
      return;
    }

    pcfile_status.current_index = next_index;
    strcpy((char *)current_filepath, (char *)file_name);
	  log_printf("new file created:%s\r\n",file_name);
    SaveLastestIndex();

}

//检查文件大小
static void FileSizeCheck(void)
{
    FIL  *temp = &pcfile_now;

    if (f_size(temp) >= PCFILE_SIZE)
    {
        CreateNewFile();
    }
	
}	


DWORD free_size = 0;
//检查剩余空间
static void RemainingSpaceCheck(void)
{
   FATFS *pfs = &SDFatFS;
   DWORD free_clust;

   uint32_t res = f_getfree(SDPath, &free_clust, &pfs);
   if(res == FR_OK)
   {
     free_size = free_clust * pfs->csize / 2; //单位是kbyte
     if(free_size <= FREE_SIZE_LIMIT)
     {
       DeleteFile(pcfile_status.oldest_index); //删除最早创建的文件
       pcfile_status.oldest_index = (pcfile_status.oldest_index + 1U) % FILE_MAX_NUM;
	   SaveOldestIndex();

     }
   }
   else 
   {
      return ;
   }

   

}

static uint8_t NeedRebuildFileIndex(void)
{
    if (!IsFileExist(pcfile_status.oldest_index))
    {
        return 1;
    }

    if (!IsFileExist(pcfile_status.current_index))
    {
        return 1;
    }

    return 0;
}

static uint8_t RebuildFileIndexByHeader(void)
{
    FRESULT res;
    DIR dir;
    FILINFO fno;
    uint8_t found = 0;
    uint32_t oldest_index = 0;
    uint32_t lastest_index = 0;
    uint64_t min_seq = 0;
    uint64_t max_seq = 0;
    recorder_file_header_t header;

    res = f_opendir(&dir, "/");
    if (res != FR_OK)
    {
        return 0;
    }

    while (1)
    {
        res = f_readdir(&dir, &fno);
        if ((res != FR_OK) || (fno.fname[0] == 0))
        {
            break;
        }

        if (fno.fattrib & AM_DIR)
        {
            continue;
        }

        if (!IsRecorderName((char *)fno.fname, &header.index))
        {
            continue;
        }

        if (!ReadRecorderFileHeader((char *)fno.fname, &header))
        {
            continue;
        }

        if (!found)
        {
            found = 1;
            min_seq = header.create_seq;
            max_seq = header.create_seq;
            oldest_index = header.index;
            lastest_index = header.index;
        }
        else
        {
            if (header.create_seq < min_seq)
            {
                min_seq = header.create_seq;
                oldest_index = header.index;
            }

            if (header.create_seq > max_seq)
            {
                max_seq = header.create_seq;
                lastest_index = header.index;
            }
        }
    }

    f_closedir(&dir);

    if (!found)
    {
        return 0;
    }

    pcfile_status.oldest_index = oldest_index;
    pcfile_status.current_index = lastest_index;
    g_create_seq = max_seq;
    SaveCreateSeq(g_create_seq);

    return 1;
}


static void RecorderCreateFileOnBoot(void)
{
    uint8_t recovered = 0;

    if (FileIndexUpdate())
    {
        recovered = 1;
        if (NeedRebuildFileIndex())
        {
            if (RebuildFileIndexByHeader())
            {
                SaveOldestIndex();
                SaveLastestIndex();
            }
            else
            {
                recovered = 0;
                pcfile_status.current_index = FILE_MAX_NUM - 1;
                pcfile_status.oldest_index = 0;
                g_create_seq = 0;
                SaveCreateSeq(g_create_seq);
            }
        }
    }
    else
    {
        if (RebuildFileIndexByHeader())
        {
            recovered = 1;
            SaveOldestIndex();
            SaveLastestIndex();
        }
        else
        {
            recovered = 0;
            pcfile_status.current_index = FILE_MAX_NUM - 1;
            pcfile_status.oldest_index = 0;
            g_create_seq = 0;
            SaveCreateSeq(g_create_seq);
        }
    }

    if (recovered && (g_create_seq == 0) && (!LoadCreateSeq(&g_create_seq)))
    {
        RebuildFileIndexByHeader();
    }

    CreateNewFile();

    if (!recovered)
    {
        pcfile_status.oldest_index = pcfile_status.current_index;
    }

   SaveOldestIndex();
    SaveLastestIndex();

}


static void RecorderStateUpdate(void)
{
  if(isSdCardInit != FR_OK || ! IsSdCardExist())
  return ;
  static uint8_t cnt = 0;

   switch (pcfile_status.state)
   {
      case RS_INIT:
      {
        RecorderCreateFileOnBoot();
		pcfile_status.state = RS_READY;
        break;
      }

      case RS_CLOSE:
      {

        break;
      }


      case RS_READY:
      {
        cnt++;
        if(cnt % 10 == 0)
        {
           RemainingSpaceCheck();
		   FileSizeCheck();
           cnt = 0;
        }
        SyncToFile();

        break;
      }
      /* code */
    
   
        default:
          break;
   }
}

void RecorderData(const char *buf,size_t len)
{ 
    uint32_t write_number;
   if(buf != NULL)
	{
	  write_number = lwrb_write(&pcfile_buffer_lwrb,buf,len);
	}
}


void RecorderInit(void)
{
   lwrb_init(&pcfile_buffer_lwrb, pcfile_buffer, PCFILE_BUFFER_SIZE);
   SdCardInit();
   pcfile_status.state = RS_INIT;
}


 void tskd_Task(const void *argu)
{

  RecorderInit();
  for(;;)  {

	RecorderStateUpdate();
    osDelay(TSKD_FRE);
  }

}
#else 
void RecorderInit(void)
{
  return ;
}

void RecorderData(const char *buf,size_t len)
{
    UNUSED(buf);
    UNUSED(len);
}

void tskd_Task(const void *argu)
{
   for(;;)
  {
    osDelay(10000);
  }
  
}
#endif








