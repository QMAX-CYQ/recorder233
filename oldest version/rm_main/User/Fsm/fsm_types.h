#ifndef FSM_TYPES_H
#define FSM_TYPES_H

#include <stdint.h>

// ==========================================
// 1. 顶层模式 (Top Level Modes)
// ==========================================
typedef enum {
    TOP_MODE_PROTECT = 0, // 默认/保护模式 (掉电、急停)
    TOP_MODE_REMOTE,      // 遥控器模式
    TOP_MODE_KEYBOARD,    // 键鼠模式 (自瞄等)
} TopMode_e;

// ==========================================
// 2. 底盘子状态 (Chassis States)
// ==========================================
typedef enum {
    CHASSIS_STOP = 0,     // 停止/无力
    CHASSIS_FOLLOW,       // 跟随云台
    CHASSIS_GYRO,         // 小陀螺 (自旋)
    CHASSIS_INDEPENDENT   // 独立运动 (键鼠常用)
} ChassisState_e;

// ==========================================
// 3. 云台子状态 (Gimbal States)
// ==========================================
typedef enum {
    GIMBAL_STOP = 0,      // 停止/无力
    GIMBAL_GYRO_STABILIZE,// 陀螺稳定 (常规控制)
    GIMBAL_AUTO_AIM,      // 自瞄接管
} GimbalState_e;

// ==========================================
// 4. 发射机构子状态 (Shoot States)
// ==========================================
typedef enum {
    SHOOT_STOP = 0,       // 停止/摩擦轮关闭
    SHOOT_READY,          // 摩擦轮启动/待命
    SHOOT_FIRING,         // 正在射击 (拨弹轮转动)
} ShootState_e;

#endif // FSM_TYPES_H