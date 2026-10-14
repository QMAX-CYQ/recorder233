
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef unsigned char BYTE;
typedef unsigned UINT;
typedef char TCHAR;
typedef int FRESULT;
#define FR_OK 0
#define FR_DISK_ERR 1
#define FR_INVALID_PARAMETER 19
#define FA_CREATE_ALWAYS 8
#define FA_OPEN_EXISTING 0
#define FA_READ 1
#define FA_WRITE 2
#define FILE_NAME_MAX_LEN 32
#define FILE_MAX_NUM 3000
#define CREATE_SEQ_FILE_NAME "create_seq.txt"
#define OLDEST_FILE_NAME "oldest.txt"
#define LASTEST_FILE_NAME "lastest.txt"
static struct { uint32_t oldest_index,current_index; } pcfile_status;
static struct { unsigned char data[128]; unsigned len; } slots[3];
typedef struct { unsigned slot,pos; } FIL;
static unsigned largest_read;
static int short_write,close_error,read_error;
static FRESULT f_open(FIL *fp,const TCHAR *path,unsigned flags) {
    fp->slot=strcmp(path,CREATE_SEQ_FILE_NAME)==0?0:strcmp(path,OLDEST_FILE_NAME)==0?1:2;
    fp->pos=0;if(flags&FA_CREATE_ALWAYS) slots[fp->slot].len=0;return FR_OK;
}
static FRESULT f_write(FIL *fp,const void *data,UINT len,UINT *written) {
    unsigned count=short_write&&len?len-1:len;assert(count<128);
    memcpy(slots[fp->slot].data+fp->pos,data,count);fp->pos+=count;
    if(fp->pos>slots[fp->slot].len) slots[fp->slot].len=fp->pos;
    *written=count;return FR_OK;
}
static FRESULT f_read(FIL *fp,void *data,UINT len,UINT *read_bytes) {
    unsigned count=slots[fp->slot].len<len?slots[fp->slot].len:len;
    if(len>largest_read)largest_read=len;
    if(read_error) {*read_bytes=0;return FR_DISK_ERR;}
    memcpy(data,slots[fp->slot].data,count);*read_bytes=count;return FR_OK;
}
static FRESULT f_close(FIL *fp) { (void)fp;return close_error?FR_DISK_ERR:FR_OK; }
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
static FRESULT WriteMetadataText(const char *path, const char *text)
{
    FIL fp;
    UINT written = 0U;
    UINT length;
    FRESULT result, close_result;
    if (path == NULL || text == NULL) return FR_INVALID_PARAMETER;
    length = (UINT)strlen(text);
    result = f_open(&fp, (const TCHAR *)path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) return result;
    result = f_write(&fp, text, length, &written);
    if (result == FR_OK && written != length) result = FR_DISK_ERR;
    close_result = f_close(&fp);
    return result == FR_OK ? close_result : result;
}
static uint8_t ReadMetadataText(const char *path, char *text, UINT capacity)
{
    FIL fp;
    UINT read_bytes = 0U;
    size_t length;
    FRESULT result, close_result;
    if (path == NULL || text == NULL || capacity < 2U) return 0U;
    result = f_open(&fp, (const TCHAR *)path, FA_OPEN_EXISTING | FA_READ);
    if (result != FR_OK) return 0U;
    result = f_read(&fp, text, capacity - 1U, &read_bytes);
    close_result = f_close(&fp);
    if (result != FR_OK || close_result != FR_OK || read_bytes == 0U) return 0U;
    text[read_bytes] = '\0';
    length = strlen(text);
    while (length != 0U && (text[length - 1U] == '\r' || text[length - 1U] == '\n' ||
                           text[length - 1U] == ' ' || text[length - 1U] == '\t'))
        text[--length] = '\0';
    return length != 0U;
}
static uint8_t LoadCreateSeq(uint32_t *seq)
{
    char buf[32] = {0};
    uint32_t value = 0U;
    size_t i;

    if (seq == NULL)
    {
        return 0;
    }

    if (!ReadMetadataText(CREATE_SEQ_FILE_NAME, buf, sizeof(buf))) return 0U;
    /* 只接受十进制数字，拒绝负数、尾部垃圾和 uint32 溢出。 */
    for (i = 0U; buf[i] != '\0'; i++) {
        uint32_t digit;
        if (buf[i] < '0' || buf[i] > '9') return 0U;
        digit = (uint32_t)(buf[i] - '0');
        if (value > (UINT32_MAX - digit) / 10U) return 0U;
        value = value * 10U + digit;
    }
    *seq = value;
    return 1;
}
static uint8_t SaveCreateSeq(uint32_t seq)
{
    char text[32];
    int length = snprintf(text, sizeof(text), "%lu\r\n", (unsigned long)seq);
    if (length < 0 || length >= (int)sizeof(text)) return 0U;
    return WriteMetadataText(CREATE_SEQ_FILE_NAME, text) == FR_OK;
}
static uint8_t ReadIndexFromFile(const char *meta_file, uint32_t *index)
{
    BYTE file_name[FILE_NAME_MAX_LEN] = {0};

    if ((meta_file == NULL) || (index == NULL))
    {
        return 0;
    }

    if (!ReadMetadataText(meta_file, (char *)file_name, sizeof(file_name))) return 0U;
    return FileNameToIndex(file_name, index);
}
static FRESULT SaveOldestIndex(void)
{
    char text[FILE_NAME_MAX_LEN];
    int length;
    if (pcfile_status.oldest_index >= FILE_MAX_NUM) return FR_INVALID_PARAMETER;
    length = snprintf(text, sizeof(text), "Rb%lu.txt\r\n", (unsigned long)pcfile_status.oldest_index);
    if (length < 0 || length >= (int)sizeof(text)) return FR_INVALID_PARAMETER;
    return WriteMetadataText(OLDEST_FILE_NAME, text);
}
static FRESULT SaveLastestIndex(void)
{
    char text[FILE_NAME_MAX_LEN];
    int length;
    if (pcfile_status.current_index >= FILE_MAX_NUM) return FR_INVALID_PARAMETER;
    length = snprintf(text, sizeof(text), "Rb%lu.txt\r\n", (unsigned long)pcfile_status.current_index);
    if (length < 0 || length >= (int)sizeof(text)) return FR_INVALID_PARAMETER;
    return WriteMetadataText(LASTEST_FILE_NAME, text);
}
int main(void) {
    uint32_t value=9999;
    memcpy(slots[0].data,"2\0" "7\0",4);slots[0].len=4;
    memcpy(slots[2].data,"Rb1.txt\000t\000",10);slots[2].len=10;
    assert(LoadCreateSeq(&value) && value==2);
    assert(ReadIndexFromFile(LASTEST_FILE_NAME,&value) && value==1);
    assert(SaveCreateSeq(2));
    assert(slots[0].len==3 && memcmp(slots[0].data,"2\r\n",3)==0);
    pcfile_status.current_index=1;
    assert(SaveLastestIndex()==FR_OK);
    assert(slots[2].len==9 && memcmp(slots[2].data,"Rb1.txt\r\n",9)==0);
    assert(ReadIndexFromFile(LASTEST_FILE_NAME,&value) && value==1);
    puts("PASS: exact observed dirty metadata replaced, no stale tail or NUL, legacy files readable");
    memcpy(slots[1].data,"Rb2999.txt",10);slots[1].len=10;
    pcfile_status.oldest_index=0;assert(SaveOldestIndex()==FR_OK);
    assert(slots[1].len==9 && memcmp(slots[1].data,"Rb0.txt\r\n",9)==0);
    assert(ReadIndexFromFile(OLDEST_FILE_NAME,&value) && value==0);
    assert(SaveCreateSeq(0));assert(LoadCreateSeq(&value)&&value==0);
    assert(SaveCreateSeq(1));assert(SaveCreateSeq(2));assert(LoadCreateSeq(&value)&&value==2);
    puts("PASS: shorter oldest name and reset/create-sequence round trip");
    assert(SaveCreateSeq(UINT32_MAX));assert(LoadCreateSeq(&value)&&value==UINT32_MAX);
    memcpy(slots[0].data,"4294967296",10);slots[0].len=10;assert(!LoadCreateSeq(&value));
    memcpy(slots[0].data,"2junk",5);slots[0].len=5;assert(!LoadCreateSeq(&value));
    memcpy(slots[0].data,"-1",2);slots[0].len=2;assert(!LoadCreateSeq(&value));
    memset(slots[2].data,'A',32);slots[2].len=32;assert(!ReadIndexFromFile(LASTEST_FILE_NAME,&value));
    assert(largest_read==31);
    puts("PASS: uint32 boundaries, malformed sequence rejection, bounded read reserves NUL space");
    short_write=1;assert(!SaveCreateSeq(2));short_write=0;
    close_error=1;assert(SaveLastestIndex()==FR_DISK_ERR);assert(!SaveCreateSeq(2));close_error=0;
    read_error=1;assert(!ReadIndexFromFile(LASTEST_FILE_NAME,&value));
    puts("PASS: short-write, close/sync error and read error propagation");
    return 0;
}
