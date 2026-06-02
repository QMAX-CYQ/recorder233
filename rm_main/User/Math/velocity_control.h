#ifndef VELOCITY_CONTROL_H
#define VELOCITY_CONTROL_H

// 机器人的参数宏定义
#define MASS 24.0f              // 机器人的质量（单位：kg）
#define MU 0.8f                // 摩擦系数（假设为0.8）
#define WHEEL_BASE 0.48f        // 机器人轮距（单位：米）

// 设定的常量
#define MAX_FRICTION 40.0f    // 设定最大摩擦力（单位：牛顿）
#define MAX_SPEED 2.6f        // 设定最大速度（单位：米/秒）
#define MAX_ANGULAR_VELOCITY 2.0f // 最大旋转速度（单位：rad/s）
#define MAX_ACCELERATION 2.0f  // 最大加速度（单位：米/秒^2）

// 函数声明
void limit_velocity(float *v_x, float *omega);

#endif // VELOCITY_CONTROL_H
