#include "prot_hipnuc.h"
#include "prot_imu.h"


hipnuc_raw_t hipnuc_data;

void hipnuc_get_data(uint8_t *raw_buff)
{
	memcpy(&hipnuc_data,raw_buff,82);
	hipnuc_data.hi91.roll *= 0.01745329251994329576923690768489f;
	hipnuc_data.hi91.pitch *= 0.01745329251994329576923690768489f;
	hipnuc_data.hi91.yaw *= 0.01745329251994329576923690768489f;
	
	if(hipnuc_data.hi91.yaw < 0)
		hipnuc_data.hi91.yaw += 2 * PI;
	else if(hipnuc_data.hi91.yaw > 2 * PI)
		hipnuc_data.hi91.yaw -= 2 * PI;
		
	for(uint8_t i = 0;i < 3;i++)
	{
		hipnuc_data.hi91.gyr[i] *= 0.01745329251994329576923690768489f;
		hipnuc_data.hi91.acc[i] *= 0.01745329251994329576923690768489f;
	}
	chassis_imu.online = 1;
	chassis_imu.pit = hipnuc_data.hi91.pitch;
	chassis_imu.yaw = hipnuc_data.hi91.yaw;
	chassis_imu.rol = hipnuc_data.hi91.roll;
	chassis_imu.wy = hipnuc_data.hi91.gyr[0];
	chassis_imu.wx = hipnuc_data.hi91.gyr[1];
	chassis_imu.wz = hipnuc_data.hi91.gyr[2];
	chassis_imu.ay = hipnuc_data.hi91.acc[0];
	chassis_imu.ax = hipnuc_data.hi91.acc[1];
	chassis_imu.az = hipnuc_data.hi91.acc[2];
}
