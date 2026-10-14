#include <math.h>
#include <stdint.h>
#include "velocity_control.h"

// 静态变量声明，避免每次调用时传递
static float max_friction = MAX_FRICTION;  // 最大摩擦力（单位：牛顿）

// 摩擦圆限制函数
void limit_velocity(float *v_x, float *omega) {
	
	static float vx_limit;

    // 计算纵向和横向摩擦力，判断是否超出摩擦力极限
    if (fabs(*omega) * MASS * WHEEL_BASE > max_friction) {
            vx_limit = ( *v_x > 0) ? max_friction / MASS : -max_friction / MASS;
		if( fabs(vx_limit) < fabs(*v_x) )
			*v_x = vx_limit;		
    }
}
