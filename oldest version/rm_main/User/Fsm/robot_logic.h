#ifndef ROBOT_LOGIC_H
#define ROBOT_LOGIC_H

#include "fsm_core.h" 
#include <stdint.h>

// --- 编译器宏适配 ---
#ifndef __PACKED_STRUCT
#define __PACKED_STRUCT struct __attribute__((packed))
#endif
#ifndef __packed
#define __packed __attribute__((packed))
#endif

// ==========================================
// 1. 输入数据结构 (RoboMaster 遥控器)
// ==========================================
typedef __PACKED_STRUCT {
    int16_t ch1; int16_t ch2; int16_t ch3; int16_t ch4; int16_t ch5;
    uint8_t sw1; uint8_t sw2;
    __PACKED_STRUCT {
        int16_t x; int16_t y; int16_t z;
        uint8_t l; uint8_t r;
    } mouse;
    __packed union {
        uint16_t key_code;
        __PACKED_STRUCT {
            uint16_t W:1; uint16_t S:1; uint16_t A:1; uint16_t D:1;
            uint16_t SHIFT:1; uint16_t CTRL:1; uint16_t Q:1; uint16_t E:1;
            uint16_t R:1; uint16_t F:1; uint16_t G:1;
            uint16_t Z:1; uint16_t X:1; uint16_t C:1; uint16_t V:1; uint16_t B:1;
        } bit;
    } kb;
} RC_Ctrl_t;

#define RC_SW_UP   1
#define RC_SW_MID  3
#define RC_SW_DOWN 2

// ==========================================
// 2. 输出枚举 (各模块目标状态)
// ==========================================
typedef enum {
    TOP_MODE_PROTECT = 0,
    TOP_MODE_REMOTE,
    TOP_MODE_KEYBOARD
} TopMode_e;

typedef enum {
    CHASSIS_STOP = 0,
    CHASSIS_LOW,                
    CHASSIS_FIGHT,              
    CHASSIS_LOW_SPIN,           
    CHASSIS_HIGH,               
    CHASSIS_TERRAIN_READY,              // 加速靠近障碍物（平地收腿）
    CHASSIS_TERRAIN_EXECUTING,          // 地形执行中（平地伸腿，空中收腿，空中伸腿，落地）
	CHASSIS_ASCEND,                     // 站高高，装备上台阶
	CHASSIS_EXECUTING_FOLLOW_ASCEND,    // 上二级台阶（先跳，再磕）
	CHASSIS_ENERGY,                     // 能量机关
    CHASSIS_STAIR,                      // 下二级台阶模式
} ChassisState_e;

typedef enum { 
    GIMBAL_STOP=0, 
    GIMBAL_GYRO_STABILIZE,      // 遥控
    GIMBAL_MOUSE_CONTROL,       // 键鼠
    GIMBAL_AUTO_AIM             // 自瞄 
} GimbalState_e;

typedef enum { 
    SHOOT_PROTECT = 0, 
    SHOOT_STOP,        
    SHOOT_READY,       // 摩擦轮开启，拨盘锁死 (准备待命)
    SHOOT_SINGLE,      // 单发点射
    SHOOT_SERIES       // 连发扫射
} ShootState_e;

// ==========================================
// 3. 全局机器人上下文 (Context)
// ==========================================
typedef struct {
    RC_Ctrl_t input;      
    
    struct {
        TopMode_e      top_mode;   
        ChassisState_e chassis;    
        GimbalState_e  gimbal;     
        ShootState_e   shoot;      
        
        uint8_t chassis_speed;     
    } output;
    
    uint8_t is_online;    
    
    int16_t  last_ch3;      
    uint16_t last_key_code; 
    uint32_t ctrl_tick;     
    
	//允许跳跃标志位
	uint8_t sky_start_flag;
	//跳跃完成标志位
	uint8_t sky_finish_flag;
	uint8_t jump_finish_flag;
} RobotContext_t;

extern RobotContext_t g_robot_ctx;
extern FsmMachine_t g_top_fsm; 

void robot_logic_init(void);
void robot_logic_update(const RC_Ctrl_t* rc_data);

#endif // ROBOT_LOGIC_H