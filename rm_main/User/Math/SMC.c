#include "smc.h"
#include <math.h>
#include <stdint.h>

// 静态辅助函数声明
static float Sat(float y);
static int8_t Signal(float y);

// 全局实例定义及初始化
SMC YawSMC = {
    .C = 20.0f,
    .K = 120.0f,
    .ref = 0.0f,
    .error_eps = 0.001f,
    .u_max = 28000.0f,
    .J = 0.8f,
    .epsilon = 0.5f
    // 其余字段默认初始化为0
};

void SMC_Tick(SMC *smc, float angle_now, float angle_vel) {
    // 更新反馈值
    smc->angle = angle_now;
    smc->ang_vel = angle_vel;
    
    // 计算误差
    smc->error = smc->angle - smc->ref;
    
    // 计算导数项（注意计算顺序）
    float old_dref = smc->dref;
    smc->dref = smc->ref - smc->refl;      // 一阶导数
    smc->ddref = smc->dref - old_dref;     // 二阶导数

    // 误差边界检查
    if (fabsf(smc->error) < smc->error_eps) {
        smc->u = 0.0f;
        return;
    }

    // 计算滑模面
    smc->s = smc->C * smc->error + (smc->ang_vel - smc->dref);

    // 计算控制量
    float term = smc->ddref 
               - smc->C * (smc->ang_vel - smc->dref)
               - smc->epsilon * Sat(smc->s)
               - smc->K * smc->s;
               
    smc->u = smc->J * term;

    // 输出限幅
    if (smc->u > smc->u_max) {
        smc->u = smc->u_max;
    } else if (smc->u < -smc->u_max) {
        smc->u = -smc->u_max;
    }

    // 保存历史值
    smc->refl = smc->ref;
}

// 饱和函数实现
static float Sat(float y) {
    if (fabsf(y) <= 1.0f) {
        return y;
    } else {
        return (float)Signal(y);
    }
}

// 符号函数实现
static int8_t Signal(float y) {
    if (y > 0.0f) return 1;
    if (y < 0.0f) return -1;
    return 0;
}