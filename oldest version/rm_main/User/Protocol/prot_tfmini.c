#include "prot_tfmini.h"
#include "wlr.h"

uint16_t tfmin_distance[2];
uint16_t tfmin_distance_sum[2];

/**
  * @brief  TFminiPlus 数据解算
  * @param  uint8_t *buff
  * @retval 距离信息cm
  */
void vTfGetData(uint8_t *buff,TF_Distance_e TF_Distance)
{
	uint16_t distance,strength;
//	static uint8_t i = 0;
	if(buff[0] == 0x59 && buff[1] == 0x59) //校验帧头
	{
		distance=buff[3]<<8 | buff[2];//合并高低八位 cm
		strength=buff[5]<<8 | buff[4];//合并高低八位
		if( strength > 500 && strength !=65535) //在强度范围内才为有效数据
		{
			tfmin_distance[TF_Distance] = distance ; 
			wlr.side[TF_Distance].Front_dis_fdb  = tfmin_distance[TF_Distance]  * 0.01f ;//m
		}
		else
		{
			tfmin_distance[TF_Distance] = 0x0fff ;
			wlr.side[TF_Distance].Front_dis_fdb  = tfmin_distance[TF_Distance]  * 0.01f ;//m
		
		}
	}
}

