#include "prot_imu.h"
#include "string.h"

#ifndef PI
    #define PI 3.14159265358979323846f
#endif

imu_t chassis_imu, gimbal_imu;

/*
 * @brief     Taurus战队imu数据接收函数
 * @param[in] imu: 陀螺仪数据结构体
 * @param[in] data: 数据指针
 * @retval    void
 */
void imu_get_data(imu_t *imu, uint32_t id, uint8_t *data)
{
    float buffer[2],array[16];
	if(id != IMU_ALL_ID && id != 0x016)
		memcpy(buffer, data, 8);
	else
		memcpy(array, data, 64);
    switch(id) {
    case IMU_PIT_ID: {
        imu->pit = -1.0f * buffer[0] * PI / 180;
        imu->wy = -1.0f * buffer[1] ;
        break;
    }
    case IMU_YAW_ID: {
        imu->yaw = 1.0f * buffer[0] * PI / 180;
        imu->wz = 1.0f * buffer[1] ;
        break;
    }
    case IMU_ROL_ID: {
        imu->rol = 1.0f * buffer[0] * PI / 180;
        imu->wx = 1.0f * buffer[1] ;
        break;
    }
    case IMU_ACC_ID: {
        imu->ax = 1.0f * buffer[0];
        imu->az = -1.0f * buffer[1];
        break;
    }
	case IMU_ALL_ID:{
		imu->pit = -1.0f * array[0] * PI / 180;
		imu->yaw = 1.0f * array[1] * PI / 180;
		imu->rol = 1.0f * array[2] * PI / 180;
		
		imu->wy = -1.0f * array[3];
		imu->wz = 1.0f * array[4];
		imu->wx = 1.0f * array[5];
		
		imu->ay = 1.0f * array[11];
		imu->az = -1.0f * array[13];
		imu->ax = 1.0f * array[9];
	}break;
	case 0x016:
	{
		imu->pit = -1.0f * array[2] * PI / 180;
		imu->yaw = 1.0f * array[1] * PI / 180;
		imu->rol = 1.0f * array[0] * PI / 180;
		
		imu->wy = -1.0f * array[5];
		imu->wz = 1.0f * array[4];
		imu->wx = 1.0f * array[3];
		
		imu->ay = 1.0f * array[9];
		imu->az = 1.0f * array[13];
		imu->ax = 1.0f * array[11];
	}break;
	default:break;
    }
    imu->online = 1;
}

uint8_t imu_check_offline(void)
{
    if (gimbal_imu.online == 0) {
        chassis_imu.online = 0;
        return 1;
    } else if (chassis_imu.online == 0) {
        gimbal_imu.online = 0;
        return 2;
    } else {
        gimbal_imu.online = 0;
        chassis_imu.online = 0;
        return 0;
    }
}
