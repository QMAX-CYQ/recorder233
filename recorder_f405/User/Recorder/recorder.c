#include "recorder.h"
#include "task.h"
#include "lwrb.h"
#include "calendar.h"
#include "log.h"
#include "data_log.h"
#include "recorder_inter.h"
#include "sdio.h"

#if USE_RECORDER

#define FILE_NAME "Rb"                  // 日志文件前缀
#define OLDEST_FILE_NAME "oldest.txt"   // 记录最早创建文件索引
#define LASTEST_FILE_NAME "lastest.txt" // 记录最新创建文件索引

#define WRITECOUNT_LIMIT 16
#define CREATE_SEQ_FILE_NAME "create_seq.txt"
#define RECORDER_FILE_MAGIC 0x52454344UL
#define RECORDER_FILE_VERSION 1UL
#define RECORDER_TEXT_HEADER_SIZE 352U

BYTE pcfile_buffer[PCFILE_BUFFER_SIZE];
BYTE pcfile_write_buffer[PCFILE_WRITE_BUFFER_SIZE];

lwrb_t pcfile_buffer_lwrb;

FIL pcfile_now;
FIL pcfile_lastest;
FIL pcfile_oldest;

BYTE current_filepath[FILE_NAME_MAX_LEN];

pcfile_status_t pcfile_status;
FRESULT isSdCardInit = FR_DISK_ERR; // 初始状态为未挂载

static uint32_t g_create_seq = 0;
static uint8_t sd_not_ready_count = 0;
uint32_t check_write_ok_count = 0;
uint32_t check_write_fail_count = 0;

FRESULT SdCardInit(void)
{
    isSdCardInit = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1); // SD卡挂载

    return isSdCardInit;
}

static uint8_t IsSdCardExist(void)
{
    if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
    {
        sd_not_ready_count = 0;
        return 1;
    }

    if (++sd_not_ready_count >= 3)
    {
        f_close(&pcfile_now);
        memset(&pcfile_now, 0, sizeof(pcfile_now));
    }

    return 0;
}

uint32_t start_time;
uint32_t end_time;
uint32_t cost_time;
uint32_t max_time;
uint32_t total_time;
float total_Mb;
float averange_time;
float averange_speed;
uint32_t averange_cnt;
extern uint8_t log_flag;
uint32_t max_lwrb_size;
// 同步缓存到文件
static void SyncToFile(void)
{
    UINT write_number = 0;
    FIL *temp = &pcfile_now;
    size_t used_size;
    uint8_t wrote = 0;
    FRESULT check_res = FR_DISK_ERR;
    FRESULT check_sync_res = FR_DISK_ERR;
    uint32_t current_write_size = 0;

    start_time = HAL_GetTick();
	uint32_t wait_time = start_time + 1000;//最多写1s
	uint32_t systick = start_time;
    do
    {
		systick = HAL_GetTick();
        used_size = lwrb_get_full(&pcfile_buffer_lwrb);
		max_lwrb_size = (used_size > max_lwrb_size ? used_size : max_lwrb_size);
        if (used_size >= PCFILE_WRITE_BUFFER_SIZE)
        {
            lwrb_read(&pcfile_buffer_lwrb, pcfile_write_buffer, PCFILE_WRITE_BUFFER_SIZE);

            check_res = f_write(temp, pcfile_write_buffer, PCFILE_WRITE_BUFFER_SIZE, &write_number);
            if ((check_res != FR_OK) || (write_number != PCFILE_WRITE_BUFFER_SIZE))
            {
                check_write_fail_count++;
                break;
            }
            current_write_size += write_number;
            wrote = 1;
        }
        else
        {
            break;
        }
    }while (systick < wait_time);

    if (wrote)
    {
        check_sync_res = f_sync(temp);
        if (check_sync_res == FR_OK)
        {
            check_write_ok_count++;
        }
        else
        {
            check_write_fail_count++;
        }
        end_time = HAL_GetTick();
        cost_time = end_time - start_time;
        max_time = (cost_time > max_time ? cost_time : max_time);
        total_time += cost_time;
        averange_cnt++;
        averange_time = (float)total_time / averange_cnt;
        total_Mb += (float)current_write_size / MB;
        averange_speed = (float)total_Mb / total_time * 1000; // MB/S
    }
}

static uint8_t IsRecorderName(const char *file_name, uint32_t *index)
{
    unsigned int tmp;
    char tail;

    if (file_name == NULL || index == NULL)
        return 0;

    if (sscanf(file_name, "Rb%u.txt%c", &tmp, &tail) == 1)
    {
        if (tmp < FILE_MAX_NUM)
        {
            *index = (uint32_t)tmp;
            return 1;
        }
    }
    return 0;
}

static uint8_t IndexToFileName(uint32_t index, BYTE *file_name)
{
    if (index <= FILE_MAX_NUM && file_name != NULL)
    {
        snprintf((char *)file_name, FILE_NAME_MAX_LEN, "%s%u.txt", FILE_NAME, (unsigned int)index);
        return 1;
    }

    return 0;
}

static uint8_t FileNameToIndex(BYTE *file_name, uint32_t *index)
{
    unsigned int tmp;
    char tail;

    if (file_name != NULL && index != NULL)
    {
        if (sscanf((char *)file_name, "Rb%u.txt%c", &tmp, &tail) == 1)
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
    char text_header[RECORDER_TEXT_HEADER_SIZE + 1U];
    char *line_end;
    char extra;
    unsigned int version;
    unsigned int index;
    unsigned long magic_value;
    unsigned long create_seq;

    if ((file_name == NULL) || (header == NULL))
    {
        return 0;
    }

    res = f_open(&fp, (const TCHAR *)file_name, FA_OPEN_EXISTING | FA_READ);
    if (res != FR_OK)
    {
        return 0;
    }

    /* Keep existing binary recorder files readable. */
    res = f_read(&fp, header, sizeof(*header), &br);
    if ((res == FR_OK) &&
        (br == sizeof(*header)) &&
        (header->magic == RECORDER_FILE_MAGIC) &&
        (header->version == RECORDER_FILE_VERSION) &&
        (header->index < FILE_MAX_NUM))
    {
        f_close(&fp);
        return 1;
    }

    res = f_lseek(&fp, 0);
    if (res != FR_OK)
    {
        f_close(&fp);
        return 0;
    }

    res = f_read(&fp, text_header, RECORDER_TEXT_HEADER_SIZE, &br);
    f_close(&fp);
    if (res != FR_OK)
    {
        return 0;
    }
    text_header[br] = '\0';

    if ((br == RECORDER_TEXT_HEADER_SIZE) &&
        (sscanf(text_header,
                "+%*[^\r]\r\n"
                "| MAGIC   : 0x%lx%*[^\r]\r\n"
                "| VERSION : %u%*[^\r]\r\n"
                "| INDEX   : %u%*[^\r]\r\n"
                "| SEQ     : %lu%*[^\r]\r\n"
                "| AUTHOR  : CYQ,LJY%*[^\r]\r\n"
                "+%*[^\r]\r\n"
                "\r\n",
                &magic_value, &version, &index, &create_seq) == 4) &&
        (magic_value == RECORDER_FILE_MAGIC) &&
        (version == RECORDER_FILE_VERSION) &&
        (index < FILE_MAX_NUM))
    {
        header->magic = RECORDER_FILE_MAGIC;
        header->version = version;
        header->index = index;
        header->create_seq = (uint32_t)create_seq;
        return 1;
    }

    if ((sscanf(text_header,
                "+%*[^\r]\r\n"
                "| MAGIC   : 0x%lx%*[^\r]\r\n"
                "| VERSION : %u%*[^\r]\r\n"
                "| INDEX   : %u%*[^\r]\r\n"
                "| SEQ     : %lu%*[^\r]\r\n"
                "+%*[^\r]\r\n"
                "\r\n",
                &magic_value, &version, &index, &create_seq) == 4) &&
        (magic_value == RECORDER_FILE_MAGIC) &&
        (version == RECORDER_FILE_VERSION) &&
        (index < FILE_MAX_NUM))
    {
        header->magic = RECORDER_FILE_MAGIC;
        header->version = version;
        header->index = index;
        header->create_seq = (uint32_t)create_seq;
        return 1;
    }

    line_end = strchr(text_header, '\n');
    if (line_end == NULL)
    {
        return 0;
    }
    if ((line_end > text_header) && (*(line_end - 1) == '\r'))
    {
        line_end--;
    }
    *line_end = '\0';

    if ((sscanf(text_header, "magic=0x%lx,version=%u,index=%u,seq=%lu%c",
                &magic_value, &version, &index, &create_seq, &extra) != 4) ||
        (magic_value != RECORDER_FILE_MAGIC) ||
        (version != RECORDER_FILE_VERSION) ||
        (index >= FILE_MAX_NUM))
    {
        return 0;
    }

    header->magic = RECORDER_FILE_MAGIC;
    header->version = version;
    header->index = index;
    header->create_seq = (uint32_t)create_seq;

    return 1;
}

static uint8_t WriteRecorderFileHeader(FIL *fp, uint32_t index)
{
    FRESULT res;
    UINT bw;
    int len;
    char border[49];
    char magic_text[32];
    char version_text[32];
    char index_text[32];
    char seq_text[32];
    char author_text[] = "AUTHOR  : CYQ,LJY";
    char text_header[RECORDER_TEXT_HEADER_SIZE + 1U];

    if ((fp == NULL) || (index >= FILE_MAX_NUM))
    {
        return 0;
    }

    g_create_seq++;

    border[0] = '+';
    memset(&border[1], '-', 46U);
    border[47] = '+';
    border[48] = '\0';

    snprintf(magic_text, sizeof(magic_text), "MAGIC   : 0x%08lX",
             (unsigned long)RECORDER_FILE_MAGIC);
    snprintf(version_text, sizeof(version_text), "VERSION : %010lu",
             (unsigned long)RECORDER_FILE_VERSION);
    snprintf(index_text, sizeof(index_text), "INDEX   : %010lu",
             (unsigned long)index);
    snprintf(seq_text, sizeof(seq_text), "SEQ     : %010lu",
             (unsigned long)g_create_seq);

    len = snprintf(text_header, sizeof(text_header),
                   "%s\r\n"
                   "| %-44s |\r\n"
                   "| %-44s |\r\n"
                   "| %-44s |\r\n"
                   "| %-44s |\r\n"
                   "| %-44s |\r\n"
                   "%s\r\n"
                   "\r\n",
                   border, magic_text, version_text, index_text, seq_text, author_text, border);
    if ((len != RECORDER_TEXT_HEADER_SIZE) || ((len & 0x03) != 0))
    {
        g_create_seq--;
        return 0;
    }

    res = f_write(fp, text_header, (UINT)len, &bw);
    if ((res != FR_OK) || (bw != (UINT)len))
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

// 判断日志文件是否存在
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

    res = f_open(&fp, (const TCHAR *)meta_file, FA_OPEN_EXISTING | FA_READ);
    if (res != FR_OK)
    {
        return 0;
    }

    res = f_read(&fp, file_name, FILE_NAME_MAX_LEN, &br);
    f_close(&fp);
    log_printf("%s\r\n", file_name);

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
    if (res != FR_OK)
    {
        DEBUG_INFO("delete file failed!\r\n");
        return 0;
    }

    return 1;
}

// 保存最早的索引
static FRESULT SaveOldestIndex(void)
{
    FRESULT res;
    UINT bw;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    IndexToFileName(pcfile_status.oldest_index, file_name);

    res = f_open(&pcfile_oldest, (const TCHAR *)OLDEST_FILE_NAME, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return res;
    }

    res = f_write(&pcfile_oldest, file_name, strlen((char *)file_name) + 1, &bw);
    f_close(&pcfile_oldest);

    return res;
}

// 保存最新的索引
static FRESULT SaveLastestIndex(void)
{
    FRESULT res;
    UINT bw;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    IndexToFileName(pcfile_status.current_index, file_name);

    res = f_open(&pcfile_lastest, (const TCHAR *)LASTEST_FILE_NAME, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return res;
    }

    res = f_write(&pcfile_lastest, file_name, strlen((char *)file_name) + 1, &bw);
    f_close(&pcfile_lastest);

    return res;
}

// 创建新文件
static void CreateNewFile(void)
{
    FRESULT res = FR_DISK_ERR;
    uint32_t next_index;
    FIL new_file;
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    next_index = (pcfile_status.current_index + 1U) % FILE_MAX_NUM;
    IndexToFileName(next_index, file_name);

    if (pcfile_status.state != RS_INIT)
    {
        res = f_close(&pcfile_now);
        if (res != FR_OK)
        {
            return;
        }
    }

    // 新建当前文件
    res = f_open(&pcfile_now, (const TCHAR *)file_name, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (res != FR_OK)
    {
        return;
    }

    if (!WriteRecorderFileHeader(&pcfile_now, next_index))
    {
        f_close(&pcfile_now);
        return;
    }

    pcfile_status.current_index = next_index;
    strcpy((char *)current_filepath, (char *)file_name);
    log_printf("new file created:%s\r\n", file_name);
    SaveLastestIndex();
}

// 检查文件大小
static void FileSizeCheck(void)
{
    FIL *temp = &pcfile_now;

    if (f_size(temp) >= PCFILE_SIZE)
    {
        CreateNewFile();
    }
}

DWORD free_size = 0;
// 检查剩余空间
static void RemainingSpaceCheck(void)
{
    FATFS *pfs = &SDFatFS;
    DWORD free_clust;

    uint32_t res = f_getfree(SDPath, &free_clust, &pfs);
    if (res == FR_OK)
    {
        free_size = free_clust * pfs->csize / 2; // 单位是kbyte
        if (free_size <= FREE_SIZE_LIMIT)
        {
            DeleteFile(pcfile_status.oldest_index); // 删除最早创建的文件
            pcfile_status.oldest_index = (pcfile_status.oldest_index + 1U) % FILE_MAX_NUM;
            SaveOldestIndex();
        }
    }
    else
    {
        return;
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
    if (isSdCardInit != FR_OK || !IsSdCardExist())
        return;
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
        if (cnt % 10 == 0)
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

void RecorderData(const char *buf, size_t len)
{
    (void)RecorderDataWrite(buf, len);
}

uint8_t RecorderDataWrite(const char *buf, size_t len)
{
    uint8_t written;

    if ((buf == NULL) || (len == 0U))
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    written = lwrb_write_ex(&pcfile_buffer_lwrb, buf, (lwrb_sz_t)len, NULL, LWRB_FLAG_WRITE_ALL);
    taskEXIT_CRITICAL();
    return written;
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
    for (;;)
    {
        RecorderStateUpdate();
        osDelay(TSKD_FRE);
    }
}
#else
void RecorderInit(void)
{
    return;
}

void RecorderData(const char *buf, size_t len)
{
    UNUSED(buf);
    UNUSED(len);
}

void tskd_Task(const void *argu)
{
    for (;;)
    {
        osDelay(10000);
    }
}
#endif
