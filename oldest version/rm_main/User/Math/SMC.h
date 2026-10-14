#ifndef SMC_H
#define SMC_H

#include <stdint.h> // 用于int8_t类型

typedef struct {
    // 公有参数
    float C;
    float K;
    float ref;
    float error_eps;
    float u_max;
    float J;
    float epsilon;
    
    // 状态变量
    float u;
    float angle;
    float ang_vel;
    
    // 私有变量
    float error;
    float error_last;
    float dref;
    float ddref;
    float refl;
    float s;
} SMC;

// 全局实例声明
extern SMC YawSMC;

// 函数声明
void SMC_Tick(SMC *smc, float angle_now, float angle_vel);

#endif // SMC_H