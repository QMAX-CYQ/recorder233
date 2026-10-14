#ifndef __PROT_HIPNUC_H
#define __PROT_HIPNUC_H
#include "stdint.h"
#include "string.h"
#include "math_lib.h"
/* HiPNUC protocol constants */
#define HIPNUC_MAX_RAW_SIZE     (512)
/**
 * Packet 0x91: IMU data (floating point)
 */
typedef struct __attribute__((__packed__))
{
    uint8_t         tag;            /* Data packet tag, if tag = 0x00, means that this packet is null */
    uint16_t        main_status;    /* reserved */
    int8_t          temp;           /* Temperature */
    float           air_pressure;   /* Pressure */
    uint32_t        system_time;    /* Timestamp */
    float           acc[3];         /* Accelerometer data (x, y, z) */
    float           gyr[3];         /* Gyroscope data (x, y, z) */
    float           mag[3];         /* Magnetometer data (x, y, z) */
    float           roll;           /* Roll angle */
    float           pitch;          /* Pitch angle */
    float           yaw;            /* Yaw angle */
    float           quat[4];        /* Quaternion (w, x, y, z) */
} hi91_t;

typedef struct
{
//    int nbyte;                          /* Number of bytes in message buffer */ 
//    int len;                            /* Message length (bytes) */
	uint16_t SOF;
	uint16_t data_length;
	uint16_t CRC16;
//    uint8_t buf[HIPNUC_MAX_RAW_SIZE];   /* Message raw buffer */
    hi91_t hi91;                        /* Decoded 0x91 packet data */
//    hi81_t hi81;                        /* Decoded 0x81 packet data */
//    hi83_t hi83;                        /* Decoded 0x83 packet data */
} hipnuc_raw_t;

extern hipnuc_raw_t hipnuc_data;

void hipnuc_get_data(uint8_t *raw_buff);

#endif
