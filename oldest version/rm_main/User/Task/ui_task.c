#include "ui_task.h"
#include "cmsis_os.h"
#include "prot_judge.h"
#include "prot_vision.h"
#include "prot_dr16.h"
#include "prot_power.h"
#include "us_time.h"
#include "wlr.h"
#include "ui_g.h"
#include "ui_interface.h"
#include "ui_types.h"
#include "leg_vmc.h"
#include "math_lib.h"
#include "control_def.h"
#include "drv_dji_motor.h"
#include "chassis_task.h"
#include "board_comm.h"
#include "container.h"
#include "shoot_task.h"

us_time_t ui_time;
int32_t lowhz_cnt;

static uint8_t last_status_high;
static uint8_t last_status_jump;
static uint8_t last_status_sky;
static uint8_t last_status_vision_online;
static uint8_t last_status_ID_choice;
static uint8_t last_status_ID_aiming;
static uint8_t fly_flag;
static uint8_t fly_cnt;

static vision_data_t ui_get_vision_data_container;

// 收到vision数据：
static void vision_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(vision_data_t))
		return;
    memcpy(&ui_get_vision_data_container,(vision_data_t*)data,len);
}

// --- 回调配置表  ---
static const ContainerBusCfg mb_callback[] = {
    { TAG_TRACE_VISION_DATA, vision_data_cb, NULL },
};

static int8_t ui_sign_function(void)
{
	return (!wlr.direction ? 1 : -1);
}


extern chassis_scale_t chassis_scale;

float armour_center,start_angle,end_angle;

void ui_init(void)
{
	ui_init_g_1();
	ui_init_g_2();
	ui_init_g_3();
	ui_init_g_4();
}

void ui_update(void)
{	
	static uint8_t one_second = 0;
	
	one_second++;
	if(one_second > 50)
		one_second = 0;
	//ui_group1 update begin
	ui_g_1_left_big_leg->end_x = ui_g_1_left_big_leg->start_x - ui_sign_function() * (vmc[((!wlr.direction) ? 1 : 0)].mp_fdb.xd * 300);
	ui_g_1_left_big_leg->end_y = ui_g_1_left_big_leg->start_y - (vmc[((!wlr.direction) ? 1 : 0)].mp_fdb.yd * 300);
	ui_g_1_left_small_leg->start_x = ui_g_1_left_big_leg->end_x;
	ui_g_1_left_small_leg->start_y = ui_g_1_left_big_leg->end_y;
	ui_g_1_left_small_leg->end_x = ui_g_1_left_big_leg->start_x - ui_sign_function() * (vmc[((!wlr.direction) ? 1 : 0)].mp_fdb.xc * 300);
	ui_g_1_left_small_leg->end_y = ui_g_1_left_big_leg->start_y - (vmc[((!wlr.direction) ? 1 : 0)].mp_fdb.yc * 300);
	
	ui_g_1_right_big_leg->end_x = ui_g_1_right_big_leg->start_x + ui_sign_function() * (vmc[((!wlr.direction) ? 0 : 1)].mp_fdb.xd * 300);
	ui_g_1_right_big_leg->end_y = ui_g_1_right_big_leg->start_y - (vmc[((!wlr.direction) ? 0 : 1)].mp_fdb.yd * 300);
	ui_g_1_right_small_leg->start_x = ui_g_1_right_big_leg->end_x;
	ui_g_1_right_small_leg->start_y = ui_g_1_right_big_leg->end_y;
	ui_g_1_right_small_leg->end_x = ui_g_1_right_big_leg->start_x + ui_sign_function() * (vmc[((!wlr.direction) ? 0 : 1)].mp_fdb.xc * 300);
	ui_g_1_right_small_leg->end_y = ui_g_1_right_big_leg->start_y - (vmc[((!wlr.direction) ? 0 : 1)].mp_fdb.yc * 300);
	
	ui_g_1_supercap_capcity->end_x = ui_g_1_supercap_capcity->start_x + (570.0f) * (supercap.volume_percent / 100.0f);
	ui_g_1_supcap_voltage->number = (int)(supercap.volage * 1000.0f) / 100 * 100;
	if(supercap.volage < 20.0f)
	{
		ui_g_1_supercap_capcity->color = 4;
		ui_g_1_supcap_voltage->color = 5;
	}
	else
	{
		ui_g_1_supercap_capcity->color = 2;
		ui_g_1_supcap_voltage->color = 6;
	}

	if(wlr.high_flag == 1)
		ui_g_1_current_high_flag->start_y = 735;
	else if(wlr.jump_flag == WLR_JUMP_ASCEND)
		ui_g_1_current_high_flag->start_y = 685;
	else if(wlr.high_flag == 0)
		ui_g_1_current_high_flag->start_y = 785;

	if(one_second <= 25)
		ui_g_1_current_high_flag->width = 10;
	else		
		ui_g_1_current_high_flag->width = 0;

	ui_update_g_1();
	//ui_group1 update end
	
	//ui_group2 update begin
	ui_g_2_target_trigger->number = (int)((shoot.trigger_ecd.fdb / 65535.0f * 360.0f) * 1000.0f) / 100 * 100;
	ui_g_2_current_trigger->number = (int)((shoot.trigger_ecd.ref / 65535.0f * 360.0f) * 1000.0f) / 100 * 100;
	
	float yaw_err;
    yaw_err = circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, (float)yaw_motor.ecd / 8192 * 2 * PI, 2 * PI);
    armour_center = yaw_err * 180.0f / PI;
	
	start_angle = armour_center - 30.0f;
	if(start_angle < 0.0f)
        start_angle += 360.0f;
    if(start_angle > 360.0f)
        start_angle -= 360.0f;
	ui_g_2_head_position->start_angle = start_angle;     

	end_angle = armour_center + 30.0f;
	if(end_angle < 0.0f)
        end_angle += 360.0f;
    if(end_angle > 360.0f)
        end_angle -= 360.0f;
    ui_g_2_head_position->end_angle = end_angle; 	
	
	ui_g_2_vision_order_id->number = ID_judge;
	ui_g_2_vision_trice_id->number = ui_get_vision_data_container.vision_trace_id;
	
	ui_g_2_current_shoot_mode->start_y = (wlr.energy_flag ? 638 : 673);
	if(one_second <= 25)
		ui_g_2_current_shoot_mode->width = 10;
	else		
		ui_g_2_current_shoot_mode->width = 0;
	
	ui_g_2_vision_frame->color = (ui_get_vision_data_container.vision_trace_id ? (ui_get_vision_data_container.vision_enanle ? 4 : 1) : 8);
	
	ui_update_g_2();
	//ui_group2 update end
	
	//ui_group4 update begin
	if(wlr.side[0].fly_flag == 1 && wlr.side[1].fly_flag == 1)
		fly_cnt += 20;
	if(fly_cnt > 20)
		fly_cnt = 20;
	
	if(fly_cnt > 0)
	{
		fly_cnt--;
		strcpy(ui_g_4_fly_flag->string, "off_land");
		ui_update_g_4();
	}
	else
	{
		strcpy(ui_g_4_fly_flag->string, "        ");
		ui_update_g_4();
	}
	//ui_group4 update end
	
	
}

uint32_t ui_update_cnt = 0;
void ui_task(void const* argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    ui_init();
	container_bus_init(mb_callback, sizeof(mb_callback)/sizeof(ContainerBusCfg));
    for(int i = 0; i < 30; i++) {
        thread_wake_time = osKernelSysTick();
        ui_init();
        osDelayUntil(&thread_wake_time, 1);
    }	
    for(;;)
    {
        thread_wake_time = osKernelSysTick();
        us_timer_interval_test_start(&ui_time);
		ui_update_cnt++;
        if (game_status.game_progress == 0 || game_status.game_progress == 1 || game_status.game_progress == 5) 
		{
			if(ui_update_cnt % 20 == 0)	
				ui_update();
			else
				ui_init();
        } 
		else if(rc.kb.bit.X)
		{
			ui_init();
        }
		else
		{
			ui_update();
		}
        us_timer_interval_test_end(&ui_time);
        osDelayUntil(&thread_wake_time, 10);
    }
}
