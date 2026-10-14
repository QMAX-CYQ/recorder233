#include "wlr.h"
#include "chassis_task.h"
#include "leg_vmc.h"
#include "wheel_leg_model.h"
#include "prot_imu.h"
#include "prot_power.h"
#include "drv_dji_motor.h"
#include "prot_dr16.h"
#include "pid.h"
#include "kalman_filter.h"
#include "math_lib.h"
#include "math_matrix.h"
#include "KNN.h"
#include "control_def.h"
#include "drv_dm_motor.h"
#include "prot_tfmini.h"
#include "velocity_control.h"
#include "prot_ms53l0m.h"
extern uint32_t rescue_cnt_L;
extern uint32_t rescue_cnt_R;
extern uint8_t rotate_ramp_flag;
extern uint8_t rotate_stop_flag;


#define WLR_SIGN(x) ((x) > 0? (1): (-1))

#ifndef DO_ONCE
#define DO_ONCE(code_block) { static int _flag = 0; if (!_flag) { _flag = 1; code_block; } }
#endif

#define _CONCAT_IMPL(a, b) a##b
#define _MACRO_CONCAT(a, b) _CONCAT_IMPL(a, b)
#define _UNIQUE_VAR(prefix) _MACRO_CONCAT(prefix, __LINE__)


#ifndef DO_LAST
// condition: 触发条件
// count:     连续执行的次数
// 如果condition为真，后面的代码执行count次
#define DO_LAST(condition, count) \
    static int _UNIQUE_VAR(_burst_cnt_) = 0; \
    if (condition) _UNIQUE_VAR(_burst_cnt_) = (count); \
    if (_UNIQUE_VAR(_burst_cnt_) > 0 && _UNIQUE_VAR(_burst_cnt_)-- > 0)
#endif		
	

static inline uint8_t wlr_both_legs_flying(void)		//使用inline关键字修饰表示为内联函数，即 不是调用函数而是直接使用return后面的代码
{
    return wlr.side[WLR_SIDE_LEFT].fly_flag && wlr.side[WLR_SIDE_RIGHT].fly_flag;
}

static inline uint8_t wlr_either_leg_flying(void)
{
    return wlr.side[WLR_SIDE_LEFT].fly_flag || wlr.side[WLR_SIDE_RIGHT].fly_flag;
}

#define CHASSIS_PERIOD_DU 2

const float LegLengthParam[2] = {0.215f, 0.258f};//大小腿长度
const float LegLengthParam_five[5] = {0.215f, 0.258f, 0.258f, 0.215f, 0.0f};

const float mb = 22.75f , ml = 2.09f, mw = 0.715f;//机体质量 腿部质量 轮子质量 
const float BodyWidth = 0.48f;//两轮间距
const float WheelRadius = 0.055f;//0.075f//轮子半径 气胎
const float LegLengthMax = 0.34f, LegLengthMin = 0.11f;

const float LegLengthHighFly = 0.28f; //长腿腿长腾空 0.28
const float LegLengthFly 	 = 0.20f; //正常腿长腾空
const float LegLengthHigh2 	 = 0.34f; //超长腿
const float LegLengthHigh 	 = 0.21f; //长腿 0.23
const float LegLengthRotate  = 0.15f; //正常
const float LegLengthRotateHigh  = 0.23f; //正常
const float LegLengthNormal  = 0.16f; //正常
const float LegLengthStair   = 0.16f; //磕碰下台阶腿长

const float gas_spring_F = 310.0f;	//气弹簧行程为0时力	N
const float gas_spring_S = 0.1f;    //气弹簧行程  m 
const float gas_spring_D = 0.006f;	//气弹簧气缸直径  m
const float gas_spring_P = 10.6105f;//气弹簧行程为0时压强	Mpa
const float gas_spring_L = 0.245f;	//气弹簧初始长度  m
const float gas_spring_V = 7.97f * 1e-6;//气弹簧容器初始体积  m^3
const float Hinge_gas_Lengh = 0.0481f;//大小腿转轴到气弹簧固定支座距离
	

float x3_balance_zero = 0.08f, x5_balance_zero = 0.040f;//腿摆角角度偏置   负值：腿摆角向膝关节方向偏	正值：腿摆角向膝关节反方向偏
const float x3_balance_zero_normal = 0.0f; //车头朝前正常情况偏置

float Rotate_balance_zero 		 = 0.17f ;
float IMU_Roll_balance_zero		 = -0.0f;		//陀螺仪roll偏置

uint16_t quadrant_cnt = 0;
//int32_t double_cnt;
float real_vel;
float F_test[2];
float x_fdb;
float theta;
float F_fdb = 0.0f;
float yw_ddot;
float Fwy;
float F_wy[2];
float ff_Fy_0 = 20.0f;
float ff_Fy_1 = 20.0f;
//位移 速度 yaw wz 左腿摆角 左腿摆角速度 右腿摆角 右腿摆角速度 机体倾角 机体倾角速度 
//左轮转矩 右轮转矩 左腿转矩 右腿转矩

const float K_Array_Fly[4][10] = 
{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
{0, 0, 0, 0, 36.6923, 3.38355, -19.6919, -1.42373, -30.6724, -3.53106}, 
{0, 0, 0, 0,-19.6919, -1.42373, 36.6923, 3.38355, -30.6724, -3.53106}
};

const float K_Array_FOLDING[4][10] = 
{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
{0, 0, 0, 0, 31.8954, 0, -21.4194, 0,0, 0}, 
{0, 0, 0, 0, -21.4194, 0, 31.8954, 0,0, 0}, 
};

const float K_Array_Prone[4][10] =
{{0, 0.7, -2.0, -1.0, 0, 0, 0, 0, 0, 0}, 
{0, 0.7, 2.0, 1.0, 0, 0, 0, 0, 0, 0}, 
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

float K_Array_Leg_030[4][10] = 
{{-0.756302, -3.0585, -4.4927, -0.979326, -17.344, -2.24202, -5.41762, -0.883117, -3.56398, -0.76058},
{-0.756302, -3.0585, 4.4927, 0.979326, -5.41762, -0.883117, -17.344, -2.24202, -3.56398, -0.76058},
{0.498229, 1.97748, -9.28385, -2.39842, 32.7315, 3.13266, -19.6108, -1.32552, -21.7883, -3.34093},
{0.498229, 1.97748, 9.28385, 2.39842, -19.6108, -1.32552, 32.7315, 3.13266, -21.7883, -3.34093}
};

 float K_Array_Leg_020[4][10] =  
{{-0.732809, -2.99337, -3.93903, -1.03763, -15.3597, -1.89766, -5.59902, -0.76385, -4.66208, -0.987495},
{-0.732809, -2.99337, 3.93903, 1.03763, -5.59902, -0.76385, -15.3597, -1.89766, -4.66208, -0.987495},
{0.648849, 2.59934, -6.86643, -2.00251, 30.3815, 3.07933, -13.9648, -0.967449, -20.8131, -3.14478},
{0.648849, 2.59934, 6.86643, 2.00251, -13.9648, -0.967449, 30.3815, 3.07933, -20.8131, -3.14478}
};

float K_Array_Leg_018[4][10] = 
{{-1.40605, -3.30473, -7.97889, -1.79854, -13.2061, -1.34814, -5.97407, -0.709838, -6.2552, -1.1978},
{-1.40605, -3.30473, 7.97889, 1.79854, -5.97407, -0.709838, -13.2061, -1.34814, -6.2552, -1.1978},
{1.75045, 3.95914, -10.779, -2.60538, 32.6298, 2.71171, -11.8714, -0.650329, -23.9471, -2.87401},
{1.75045, 3.95914, 10.779, 2.60538, -11.8714, -0.650329, 32.6298, 2.71171, -23.9471, -2.87401}
};

float K_Array_Leg_015[4][10] = 
{{-0.82901, -2.9273, -7.97889, -1.0643, -9.2547, -0.99003, -4.8267, -0.6026, -6.0625, -1.2483},
{-0.82901, -2.9273, 7.97889, 1.0643, -4.8267, -0.6026, -9.2547, -0.99003, -6.0625, -1.2483},
{1.48502, 5.08335, -10.779, -1.58449, 25.7709, 2.2352, -7.17445, -0.280636, -19.2445, -2.1996},
{1.48502, 5.08335, 10.779, 1.58449, -7.17445, -0.280636, 25.7709, 2.2352, -19.2445, -2.1996}
};

float K_Array_Energy[4][10] = 
{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{0, 0, 0, 0, 34.2222, 3.22149, -14.0909, -0.889731, 0, 0},
{0, 0, 0, 0, -14.0909, -0.889731, 34.2222, 3.22149, 0, 0}
};

float K_Array_Sky[4][10] = 
{{-1.40605, -3.30473, -5.61603, -1.73306, -13.261, -1.35136, -5.91916, -0.706618, -6.2552, -1.1978},
{-1.40605, -3.30473, 5.61603, 1.73306, -5.91916, -0.706618, -13.261, -1.35136, -6.2552, -1.1978},
{1.75045, 3.95914, -7.73801, -2.51907, 32.5549, 2.70732, -11.7965, -0.645937, -23.9471, -2.87401},
{1.75045, 3.95914, 7.73801, 2.51907, -11.7965, -0.645937, 32.5549, 2.70732, -23.9471, -2.87401}
};

float K_Array_Land[4][10] = 
{{0, -3.09417, 0,0, -17.002, -2.25729, -4.68966, -0.890387, 0, 0},
{0, -3.09417, 0, 0, -4.68966, -0.890387, -17.002, -2.25729, 0, 0},
{0, 1.76186, 0,0, 31.8954, 3.02506, -21.4194, -1.44492, 0, 0},
{0, 1.76186, 0, 0, -21.4194, -1.44492, 31.8954, 3.02506, 0, 0}
};  

float K_Array_Leg_recover[4][10] = 
{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
{0, 0, 0, 0, 32.6298, 2.71171, -11.8714, -0.650329, 0, 0},
{0, 0, 0, 0, -11.8714, -0.650329, 32.6298, 2.71171, 0, 0}
};

float K_Array_Leg_rotate[4][10] = 
{{0, -3.24553, 0, -1.23895, -17.1345, -2.25814, -5.86629, -0.830853, -7.25895, -1.13856},
{0, -3.24553, 0, 1.23895, -5.86629, -0.830853, -17.1345, -2.25814, -7.25895, -1.13856},
{0, 1.96624, 0, -2.17273, 27.4088, 2.91566, -14.71, -1.15579, -36.082, -3.93633},
{0, 1.96624, 0, 2.17273, -14.71, -1.15579, 27.4088, 2.91566, -36.082, -3.93633}
};

float K_Array_Stop[4][10] = 
{{-3.71, -5.42496, -13.4919, -2.87021, -20.451, -2.04802, -8.269, -0.977558, -5.70532, -1.20161},
{-3.71, -5.42496, 13.4919, 2.87021, -8.269, -0.977558, -20.451, -2.04802, -5.70532, -1.20161},
{1.51124, 2.06361, -11.5417, -2.79868, 29.1424, 2.2726, -17.2795, -1.08967, -19.6641, -3.04676},
{1.51124, 2.06361, 11.5417, 2.79868, -17.2795, -1.08967, 29.1424, 2.2726, -19.6641, -3.04676}
};

const float K_Fit_Array[40][3][3] = 
{{{-1.50772,-0.201288,1.74873},{-0.174393,-3.76534,0},{2.48358,0,0}},{{-3.24058,-0.347737,3.66648},{1.46602,-9.05607,0},{2.57215,0,0}},{{-4.91297,-4.4151,-0.53714},{22.322,21.6979,0},{-33.8252,0,0}},{{-1.27698,-1.59112,0.0465232},{8.25481,7.83571,0},{-12.5106,0,0}},{{-2.61953,10.6752,11.36},{-81.2722,-103.514,0},{98.6671,0,0}},{{-1.21525,1.47504,1.8075},{-7.82801,-14.9352,0},{9.59973,0,0}},{{-5.83705,-16.7586,-6.51043},{59.0041,123.214,0},{-98.0578,0,0}},{{-0.952356,-3.52993,1.66281},{9.96847,8.02318,0},{-13.3617,0,0}},{{-5.75986,2.8643,3.66412},{16.2791,-28.6718,0},{2.21668,0,0}},{{-2.29619,0.953316,1.8891},{6.9863,-12.3387,0},{0.36472,0,0}},{{-1.50772,-0.174393,2.48358},{-0.201288,-3.76534,0},{1.74873,0,0}},{{-3.24058,1.46602,2.57215},{-0.347737,-9.05607,0},{3.66648,0,0}},{{4.91297,-22.322,33.8252},{4.4151,-21.6979,0},{0.53714,0,0}},{{1.27698,-8.25481,12.5106},{1.59112,-7.83571,0},{-0.0465232,0,0}},{{-5.83705,59.0041,-98.0578},{-16.7586,123.214,0},{-6.51043,0,0}},{{-0.952356,9.96847,-13.3617},{-3.52993,8.02318,0},{1.66281,0,0}},{{-2.61953,-81.2722,98.6671},{10.6752,-103.514,0},{11.36,0,0}},{{-1.21525,-7.82801,9.59973},{1.47504,-14.9352,0},{1.8075,0,0}},{{-5.75986,16.2791,2.21668},{2.8643,-28.6718,0},{3.66412,0,0}},{{-2.29619,6.9863,0.36472},{0.953316,-12.3387,0},{1.8891,0,0}},{{0.199645,2.23515,-3.25561},{-2.85191,0.0883145,0},{3.7967,0,0}},{{0.398063,4.96975,-7.30163},{-6.28643,0.106763,0},{8.6668,0,0}},{{-0.930465,-3.21701,6.30601},{-4.35736,-4.18175,0},{10.1397,0,0}},{{-0.509699,-0.932181,1.7568},{-1.25037,-1.25889,0},{3.07325,0,0}},{{1.56636,15.7418,-29.8362},{20.2154,28.388,0},{-40.6369,0,0}},{{0.663193,2.78914,-4.79139},{-0.884351,3.17267,0},{-0.243134,0,0}},{{-0.56926,-15.7434,25.1009},{-20.7429,-27.7502,0},{43.8098,0,0}},{{-0.422595,0.948891,-0.72993},{-3.4919,-3.54838,0},{6.83277,0,0}},{{-7.12249,7.53699,-7.6356},{-9.45193,-0.49529,0},{10.9732,0,0}},{{-2.32959,3.55112,-3.91312},{-4.39327,-0.217266,0},{5.39055,0,0}},{{0.199645,-2.85191,3.7967},{2.23515,0.0883145,0},{-3.25561,0,0}},{{0.398063,-6.28643,8.6668},{4.96975,0.106763,0},{-7.30163,0,0}},{{0.930465,4.35736,-10.1397},{3.21701,4.18175,0},{-6.30601,0,0}},{{0.509699,1.25037,-3.07325},{0.932181,1.25889,0},{-1.7568,0,0}},{{-0.56926,-20.7429,43.8098},{-15.7434,-27.7502,0},{25.1009,0,0}},{{-0.422595,-3.4919,6.83277},{0.948891,-3.54838,0},{-0.72993,0,0}},{{1.56636,20.2154,-40.6369},{15.7418,28.388,0},{-29.8362,0,0}},{{0.663193,-0.884351,-0.243134},{2.78914,3.17267,0},{-4.79139,0,0}},{{-7.12249,-9.45193,10.9732},{7.53699,-0.49529,0},{-7.6356,0,0}},{{-2.32959,-4.39327,5.39055},{3.55112,-0.217266,0},{-3.91312,0,0}}};

const float K_Fit_Array2[40][3][3] = 
{{{-2.01738,0.619157,0.561419},{-1.14183,-3.81284,0},{3.92777,0,0}},{{-4.169,1.22219,1.23516},{0.0687191,-9.18838,0},{4.45458,0,0}},{{-5.19993,-4.09213,-0.850394},{22.5581,22.4976,0},{-34.3481,0,0}},{{-1.29569,-1.28832,-0.277364},{7.94806,7.84303,0},{-12.0676,0,0}},{{-3.25979,11.1385,9.25514},{-82.7641,-103.138,0},{97.9959,0,0}},{{-1.36892,1.60971,1.30682},{-7.92552,-14.6136,0},{8.51295,0,0}},{{-6.48552,-17.0253,-5.67787},{56.7834,120.048,0},{-94.1846,0,0}},{{-1.1207,-3.21235,0.565515},{9.59504,7.62117,0},{-12.7821,0,0}},{{-6.26313,4.25921,1.11382},{16.4493,-27.7753,0},{1.58917,0,0}},{{-2.55444,1.65792,0.638259},{7.15658,-11.9863,0},{-0.0380284,0,0}},{{-2.01738,-1.14183,3.92777},{0.619157,-3.81284,0},{0.561419,0,0}},{{-4.169,0.0687191,4.45458},{1.22219,-9.18838,0},{1.23516,0,0}},{{5.19993,-22.5581,34.3481},{4.09213,-22.4976,0},{0.850394,0,0}},{{1.29569,-7.94806,12.0676},{1.28832,-7.84303,0},{0.277364,0,0}},{{-6.48552,56.7834,-94.1846},{-17.0253,120.048,0},{-5.67787,0,0}},{{-1.1207,9.59504,-12.7821},{-3.21235,7.62117,0},{0.565515,0,0}},{{-3.25979,-82.7641,97.9959},{11.1385,-103.138,0},{9.25514,0,0}},{{-1.36892,-7.92552,8.51295},{1.60971,-14.6136,0},{1.30682,0,0}},{{-6.26313,16.4493,1.58917},{4.25921,-27.7753,0},{1.11382,0,0}},{{-2.55444,7.15658,-0.0380284},{1.65792,-11.9863,0},{0.638259,0,0}},{{0.285877,2.49044,-3.64987},{-3.36299,0.260743,0},{4.29609,0,0}},{{0.542454,5.41621,-7.98522},{-7.17667,0.391461,0},{9.5801,0,0}},{{-0.949327,-3.46488,6.47959},{-4.60436,-3.96777,0},{10.4229,0,0}},{{-0.510705,-0.982773,1.71583},{-1.26871,-1.07161,0},{2.99343,0,0}},{{1.6327,16.3883,-30.1977},{19.9453,28.1593,0},{-40.0339,0,0}},{{0.682502,2.91383,-4.89648},{-0.996772,3.16041,0},{-0.0729002,0,0}},{{-0.468162,-15.3817,24.1509},{-21.6527,-27.342,0},{44.3754,0,0}},{{-0.404202,1.04296,-0.84007},{-3.67243,-3.5285,0},{6.98738,0,0}},{{-7.0644,7.65992,-7.7833},{-9.85481,-0.385766,0},{11.4317,0,0}},{{-2.30102,3.62527,-4.01723},{-4.60912,-0.162355,0},{5.65669,0,0}},{{0.285877,-3.36299,4.29609},{2.49044,0.260743,0},{-3.64987,0,0}},{{0.542454,-7.17667,9.5801},{5.41621,0.391461,0},{-7.98522,0,0}},{{0.949327,4.60436,-10.4229},{3.46488,3.96777,0},{-6.47959,0,0}},{{0.510705,1.26871,-2.99343},{0.982773,1.07161,0},{-1.71583,0,0}},{{-0.468162,-21.6527,44.3754},{-15.3817,-27.342,0},{24.1509,0,0}},{{-0.404202,-3.67243,6.98738},{1.04296,-3.5285,0},{-0.84007,0,0}},{{1.6327,19.9453,-40.0339},{16.3883,28.1593,0},{-30.1977,0,0}},{{0.682502,-0.996772,-0.0729002},{2.91383,3.16041,0},{-4.89648,0,0}},{{-7.0644,-9.85481,11.4317},{7.65992,-0.385766,0},{-7.7833,0,0}},{{-2.30102,-4.60912,5.65669},{3.62527,-0.162355,0},{-4.01723,0,0}}};

	
float P_Array[2][8] = 
{{15.3846, -3.6923, -0.5488, -0.6627, 0.2536, 0.1212, -0.0351, -0.0424}, 
{15.3846, 3.6923, -0.6627, -0.5488, 0.1212, 0.2536, -0.0424, -0.0351}};
    
const float P_Fit_Array[16][3][3] = 
{{{15.3846,0,0},{0,0,0},{0,0,0}},
{{-3.86154,0,0},{0,0,0},{0,0,0}},
{{0.0133423,0.00586344,-0.000413327},{-0.316935,-0.00202615,0},{-0.025402,0,0}},
{{0.050454,-0.990208,-0.0361836},{0.00192015,-0.000677019,0},{-0.00012967,0,0}},
{{0.106136,0.0767992,-0.00588455},{0.0736214,0.00376196,0},{-0.0421193,0,0}},
{{-0.0508608,0.241513,-0.155217},{0.0251495,0.00104189,0},{-0.00184647,0,0}},
{{-0.00132327,0.00125407,-0.0000458756},{-0.0299709,-0.00317036,0},{0.0495896,0,0}},
{{-0.00165051,-0.1033,0.169188},{0.000410662,-0.00103957,0},{-0.0000143599,0,0}},
{{15.3846,0,0},{0,0,0},{0,0,0}},
{{3.86154,0,0},{0,0,0},{0,0,0}},
{{0.050454,0.00192015,-0.00012967},{-0.990208,-0.000677019,0},{-0.0361836,0,0}},
{{0.0133423,-0.316935,-0.025402},{0.00586344,-0.00202615,0},{-0.000413327,0,0}},
{{-0.0508608,0.0251495,-0.00184647},{0.241513,0.00104189,0},{-0.155217,0,0}},
{{0.106136,0.0736214,-0.0421193},{0.0767992,0.00376196,0},{-0.00588455,0,0}},
{{-0.00165051,0.000410662,-0.0000143599},{-0.1033,-0.00103957,0},{0.169188,0,0}},
{{-0.00132327,-0.0299709,0.0495896},{0.00125407,-0.00317036,0},{-0.0000458756,0,0}}
};

wlr_t wlr;
lqr_t lqr;

kalman_filter_t kal_fn[2];
kalman_filter_t tfmini_fn[2];
	
ramp_t height_ramp;
ramp_t jump_ramp;
ramp_t stair_ramp;
ramp_t wz_ramp;
ramp_t Fy_ramp[2];	
ramp_t sky_ramp[2];
ramp_t sky_height_ramp;
	
pid_t pid_rescue[2];
pid_t pid_leg_recover[2];
pid_t pid_leg_sky_cover[2];	
pid_t pid_leg_sky_jump[2];	
pid_t pid_leg_length_fly[2];
pid_t pid_roll;
pid_t pid_L_test[2];
pid_t pid_ascend[2];
pid_t pid_rotate_leg[2];
pid_t pid_energy_leg[2];
pid_t pid_rotate_balance_zero;
float global_v;

static float wlr_fn_calc(float az, float Fy_fdb, float T0_fdb, float L0[3], float theta[3])
{
    Fwy = Fy_fdb * cosf(theta[0]) + T0_fdb * sinf(theta[0]) / L0[0];//轮子受到腿部机构竖直方向的作用力
	yw_ddot = az
				- L0[2] * cosf(theta[0])
				+ 2 * L0[1] * theta[1] * sinf(theta[0])
				+ L0[0] * theta[2] * sinf(theta[0])
				+ L0[0] * powf(theta[1], 2) * cosf(theta[0]);//轮子竖直方向的加速度
    return Fwy + mw * GRAVITY + mw * yw_ddot;
}

static float gas_spring_F_Calc(vmc_t v)
{
	//活塞有效面积计算
	const float A = PI * powf(gas_spring_D,2) / 4.0f;
	//当前行程获取(类VMC思路求解)
	x_fdb = gas_spring_L - \
		sqrtf(powf(v.mp_fdb.xd + Hinge_gas_Lengh * arm_cos_f32(v.q_fdb[3]),2) + powf(v.mp_fdb.yd + Hinge_gas_Lengh * arm_sin_f32(v.q_fdb[3]),2));
	//通过当前行程计算当前力
	F_fdb = gas_spring_P * 1e+6 * gas_spring_V * A / (gas_spring_V - A * x_fdb);
	//计算气弹簧摆角
	theta = atan2f(v.mp_fdb.yd + Hinge_gas_Lengh * arm_sin_f32(v.q_fdb[3]) - v.mp_fdb.ym, 
			v.mp_fdb.xd + Hinge_gas_Lengh * arm_cos_f32(v.q_fdb[3]) - v.mp_fdb.xm);
	//这里让气弹簧摆角为定值，减少腿摆角对支持力的影响导致离地检测误判
	theta = 0.508f;
	//0.28    0.18
	//0.51	  0.23
	//分解到竖直方向上
	float Fn_fdb = F_fdb * arm_sin_f32(theta);
	
	return Fn_fdb;
}

static uint8_t check_is_statir(void)
{
	static float last_v_fdb;
	static uint8_t stair_cnt = 0;
	if(fabsf(last_v_fdb) > fabsf(wlr.v_fdb) && fabsf(wlr.v_fdb) < 0.9f && fabsf(wlr.v_fdb) > 0.3f)
		stair_cnt++;
	else 
		stair_cnt = 0;
	last_v_fdb = wlr.v_fdb;
	if(stair_cnt > 5)
		return 1;
	else
		return 0;
	
}

static void k_array_fit(float K[4][10], float Ll_fdb, float Lr_fdb)
{
    float temp;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 10; j++) {
            temp = 0;
            for(int x = 0; x < 3; x++)
                for(int y = 0; x + y < 3; y++)
                    temp += (K_Fit_Array[i * 10 + j][x][y] * powf(Ll_fdb, x) * powf(Lr_fdb, y));
            K[i][j] = temp;
        }
}

static void p_array_fit(float P[2][8], float Ll_fdb, float Lr_fdb)
{
    float temp;
    for (int i = 0; i < WLR_SIDE_COUNT; i++)
        for (int j = 0; j < 8; j++) {
            temp = 0;
            for(int x = 0; x < 3; x++)
                for(int y = 0; x + y < 3; y++)
                    temp += (P_Fit_Array[i * 8 + j][x][y] * powf(Ll_fdb, x) * powf(Lr_fdb, y));
            P[i][j] = temp;
        }
}

static void state_predict(void)
{
    for (int i = 0; i < WLR_SIDE_COUNT; i++) {
        wlr.side[i].predict_wy = 0;
        wlr.side[i].predict_wy += (P_Array[i][0] * lqr.X_fdb[1] + P_Array[i][1] * lqr.X_fdb[3] 
		+ P_Array[i][2] * lqr.X_fdb[4] + P_Array[i][3] * lqr.X_fdb[6]);
//        for (int j = 0; j < 4; j++) {
//            wlr.side[i].predic   t_wy -= (P_Array[i][j + 4] * wlr.side[i].Tw);
//        }
        wlr.side[i].predict_wy += P_Array[i][4] * wlr.side[0].Tw + P_Array[i][5] * wlr.side[1].Tw \
                               + P_Array[i][6] * wlr.side[0].T0 + P_Array[i][7] * wlr.side[1].T0;//待校对极性
    }
}

//限速函数
static void stable_velocity_control(void)
{
    if(fabsf(lqr.X_fdb[8]) > 0.3f)  
        data_limit(&lqr.X_ref[1],0.0f,0.0f);
    else if(fabsf(lqr.X_fdb[8]) > 0.2f)
        data_limit(&lqr.X_ref[1],-1.0f,1.0f);
    else if(fabsf(lqr.X_fdb[8]) > 0.1f)
        data_limit(&lqr.X_ref[1],-2.0f,2.0f);
    if(wlr.sky_flag == WLR_SKY_IDLE && wlr.jump_flag == WLR_JUMP_IDLE && wlr.high_flag == 0 && !g_robot_ctx.output.chassis_speed)//磕台阶，跳跃，飞坡时不限制速度输入
    {
        data_limit(&lqr.X_ref[1],-power_control_target_velocity(),power_control_target_velocity());
        limit_velocity(&lqr.X_ref[1],&lqr.X_fdb[3]);//摩擦圆
    }
	if(rotate_flag)
	{
		if(g_robot_ctx.output.chassis_speed)
			data_limit(&lqr.X_ref[3],-9.0f,9.0f);
		else
			data_limit(&lqr.X_ref[3],-power_control_target_Vrotate(),power_control_target_Vrotate());
	}
    if(wlr.sky_flag == WLR_SKY_FOLDING)
		data_limit(&lqr.X_ref[1],-3.0f,3.0f);
	else
		data_limit(&lqr.X_ref[1],-2.3f,2.3f);
}
int32_t Last_cnt;

static void update_leg_height_and_balance(float yaw_error)
{
    if (wlr.high_flag == 2) {
        wlr.high_set = ramp_calc(&height_ramp, LegLengthHigh2);
        x3_balance_zero = x3_balance_zero_normal;
    } else if (wlr.high_flag == 1) {
        if (wlr_both_legs_flying()) {
            wlr.high_set = 0.33f;
            height_ramp.out = LegLengthHigh;	
        } else if (wlr_either_leg_flying()) {
			x3_balance_zero = x3_balance_zero_normal;
        } else if (wlr.jump_flag == WLR_JUMP_IDLE && wlr.sky_flag == WLR_SKY_IDLE) {
            wlr.high_set = ramp_calc(&height_ramp, LegLengthHigh);
			if(wlr.direction)
				x3_balance_zero = x3_balance_zero_normal;
			else
				x3_balance_zero = x3_balance_zero_normal;
        }
		
		(wlr.direction == 0) ? (Last_cnt = 200) : (Last_cnt = 200);
		
		DO_LAST(wlr_both_legs_flying(),Last_cnt){
			if(wlr.direction == 0){
				x3_balance_zero = x3_balance_zero_normal;
				data_limit(&wlr.v_ref,-1.0f,1.0f);
			}
			else{
				x3_balance_zero = x3_balance_zero_normal;	
				data_limit(&wlr.v_ref,-1.0f,1.0f);
			}
		}
		x5_balance_zero = 0.0f;
    } else {
        if (wlr.jump_flag == WLR_JUMP_IDLE && wlr.sky_flag == WLR_SKY_IDLE && wlr.stair_flag == WLR_STAIR_IDLE && !wlr.energy_flag) {
            wlr.high_set = ramp_calc(&height_ramp, LegLengthNormal);
            x3_balance_zero = (wlr.direction == 0 ? x3_balance_zero_normal : -x3_balance_zero_normal - 0.01f);
			
        }
		else if (wlr.energy_flag) {
			wlr.high_set = 0.12f;
			x3_balance_zero = x3_balance_zero_normal + 0.1f;
		}
		x5_balance_zero = 0.00f;
    }

    if (wlr.jump_flag == WLR_JUMP_ASCEND) {
        x3_balance_zero = 0.0f;   
    }
	if(wlr.last_high_flag != wlr.high_flag && chassis.recover_flag == 0)
	{
		pid_L_test[0].i_out = 0;
		pid_L_test[1].i_out = 0;
	}
	wlr.last_high_flag = wlr.high_flag;
}

static uint16_t sky_ccc = 0;

static void reset_jump_state(void)
{
    if (wlr.jump_flag == WLR_JUMP_IDLE) {
        wlr.jump_pre = 0;
        wlr.jump_cnt = 0;
        wlr.crash_flag = 0;
        wlr.jump_run = 0;
    }

    if (wlr.sky_flag == WLR_SKY_IDLE) {
		wlr.sky_over = 0;
		wlr.sky_cnt = 0;
		sky_height_ramp.out = 0.23f;
    }
}

static void handle_jump_state(void)
{
//    if (wlr.jump_flag == WLR_JUMP_ASCEND && (wlr.jump_pre || 1) ){
////        if (double_cnt <= 0) {
//////			sky_height_ramp.out = wlr.high_set;
////            wlr.v_ref = ramp_calc(&jump_ramp, -1.5f);
////        } else {
////            wlr.v_ref = ramp_calc(&jump_ramp, -0.8f);
////        }
//        wlr.jump_cnt++;
//        if (wlr.jump_cnt > 30) {
//            if (double_cnt <= 0) {
//                wlr.high_set = ramp_calc(&height_ramp, 0.35f);
//				wlr.v_ref = ramp_calc(&jump_ramp, -1.5f);
//            } else {
//                wlr.high_set = ramp_calc(&height_ramp, 0.28f);
//            }
//            wlr.jump_run++;
//        } else {
//            jump_ramp.out = wlr.v_fdb;
//        }

//        if (double_cnt <= 0 && wlr.jump_run > 400) {
//			wlr.v_ref = ramp_calc(&jump_ramp, -1.5f);
//            if (fabsf(lqr.X_fdb[4]) > 0.30f && fabsf(lqr.X_fdb[6]) > 0.30f) {
//                wlr.high_set = 0.35f;
//                wlr.v_ref = 0;
//                wlr.crash_flag = 1;
//            }
//        } else if (double_cnt > 0 && wlr.jump_run > 400) {
//            if (fabsf(lqr.X_fdb[4]) > 0.20f && fabsf(lqr.X_fdb[6]) > 0.20f) {
//                wlr.high_set = 0.12f;
//                wlr.v_ref = 0;
//                wlr.crash_flag = 1;
//                wlr.jump2_over = 1;
//            }
//        }

//        if ((fabs(lqr.X_fdb[4]) > 1.40f && fabs(lqr.X_fdb[6]) > 1.40f && wlr.crash_flag && double_cnt <= 0)
//            || (fabs(lqr.X_fdb[4]) > 1.50f && fabs(lqr.X_fdb[6]) > 1.50f && wlr.crash_flag && double_cnt > 0)) {
//            if (wlr.crash_flag) {
//                wlr.jump_flag = WLR_JUMP_RECOVER_SHORT;
//                wlr.crash_flag = 0;
//                wlr.high_flag = 0;
//                chassis.recover_flag = 1;
//                chassis.rescue_inter_flag = 2;
//            }
//        }
//    } else if (wlr.jump_flag == WLR_JUMP_RECOVER_SHORT) {
//        wlr.high_set = 0.12f;
//        double_cnt = 3000;
//        if (fabs(wlr.high_set - vmc[0].L_fdb) < 0.06f && fabs(wlr.high_set - vmc[1].L_fdb) < 0.06f) {
//            wlr.jump_flag = WLR_JUMP_RECOVER_LONG;
//            wlr.crash_flag = 0;
//            height_ramp.out = 0.12f;
//        } else if (wlr.jump_flag == WLR_JUMP_RECOVER_LONG) {
//            wlr.high_set = 0.12f;
//        }
//    }
//    if (double_cnt) {
//        double_cnt--;
//    }
    static float jump_leg_length = 0.0f;
	static float limit_q = 0.0f;
	if(wlr.jump_flag == WLR_JUMP_ASCEND)
	{
		 Fy_ramp[0].out = Fy_ramp[1].out= 0;
		 pid_leg_recover[0].i_out = 0,pid_leg_recover[1].i_out = 0;

         jump_leg_length = (wlr.double_flag ? 0.33f : 0.33f);
		 limit_q = (wlr.double_flag ? 0.4f : 0.4f);
		 wlr.high_set = ramp_calc(&height_ramp, jump_leg_length);
		 x3_balance_zero = x3_balance_zero_normal - 0.04F;
         x5_balance_zero = -0.0f;
		 wlr.jump_run++;

         if(wlr.double_flag)
            wlr.v_ref = ramp_calc(&jump_ramp, -2.5f);
        else
            jump_ramp.out = 0.0f;

		 if(fabsf(lqr.X_fdb[4]) > limit_q && fabsf(lqr.X_fdb[6]) > limit_q && wlr.jump_run > 200) {
			wlr.high_set = jump_leg_length;
			wlr.v_ref = 0;
            jump_ramp.out = 0.0f;
			wlr.crash_flag = 1;
//			wlr.high_set = 0.16f;
//			 wlr.jump_flag = WLR_JUMP_RECOVER_SHORT;
		 }
		 if((fabs(lqr.X_fdb[4]) > 1.0f && fabs(lqr.X_fdb[6]) > 1.0f && wlr.crash_flag))
		 {
			wlr.jump_flag = WLR_JUMP_RECOVER_SHORT;
			wlr.crash_flag = 0;
			wlr.high_flag = 0;
			chassis.recover_flag = 1;
			up_ready = 101;
//			chassis.rescue_inter_flag = 2;
		 }
	}
	else if(wlr.jump_flag == WLR_JUMP_RECOVER_SHORT)
	{
		 wlr.high_set = 0.16f;
		 if (fabsf(vmc[0].L_fdb - wlr.high_set) < 0.05f && fabsf(vmc[1].L_fdb - wlr.high_set) < 0.05f) {
			 wlr.jump_cnt++;
			 if(wlr.jump_cnt > 50)
			 {
				 wlr.jump_flag = WLR_JUMP_RECOVER_LONG;
				 wlr.crash_flag = 0;
				 height_ramp.out = 0.16f;
				 pid_leg_recover[0].i_out = 0;
				 pid_leg_recover[1].i_out = 0;
			 }
        }
	}
//	else if(wlr.jump_flag == WLR_JUMP_RECOVER_LONG)
//	{
//		wlr.high_set = ramp_calc(&height_ramp, 0.18f);
//	}
}

static void handle_sky_state(void)
{
    static float sky_leg_length = 0.0f;
	static uint16_t target_cnt = 0;
    if (wlr.sky_flag == WLR_SKY_FOLDING) {
        pid_leg_sky_jump[0].i_out = pid_leg_sky_jump[1].i_out = \
	    pid_leg_sky_cover[0].i_out = pid_leg_sky_cover[1].i_out = \
	    Fy_ramp[0].out = Fy_ramp[1].out= 0;
		sky_ramp[0].out = sky_ramp[1].out= 0;

        sky_leg_length = (wlr.double_flag ? 0.16f : 0.11f);
        x3_balance_zero = x3_balance_zero_normal - 0.03f;
        x5_balance_zero = (wlr.double_flag ? 0.0f : -0.08f);
        wlr.high_set = ramp_calc(&sky_height_ramp, sky_leg_length);
        
       if(g_robot_ctx.output.top_mode == TOP_MODE_REMOTE) {
			sky_ccc++;
			if (abs(rc.ch2) > 500) { 
				wlr.sky_cnt++;
			}
			if (wlr.sky_cnt > 50 || (check_tof_jump(1.7f,0.65f) && sky_ccc > 700 && (ms53l0m_data_valid[0] == 0 || ms53l0m_data_valid[0] == 7)  && (ms53l0m_data_valid[1] == 0 || ms53l0m_data_valid[1] == 7))) {
				wlr.sky_cnt = 0;
				wlr.sky_flag = WLR_SKY_EXTENDING;
			} 
		}
		if(wlr.double_flag)
            wlr.v_ref = ramp_calc(&jump_ramp, -2.0f);
        else
            wlr.v_ref = ramp_calc(&jump_ramp, -3.0f);
    } else if (wlr.sky_flag == WLR_SKY_EXTENDING) {
		sky_ccc = 0;
        wlr.high_set = 0.35f;
        jump_ramp.out = 0.0f;
        wlr.v_ref = -2.5f;
        x3_balance_zero = x3_balance_zero_normal + 0.05F;
        x5_balance_zero = (wlr.double_flag ? -0.0f : -0.05f);

        if (fabsf(0.30f - vmc[0].L_fdb) < 0.03f && fabsf(0.30f - vmc[1].L_fdb) < 0.03f && !wlr.double_flag) {
            wlr.sky_cnt++;
        }
        else if (vmc[0].L_fdb - 0.25f > 0.0f && vmc[1].L_fdb - 0.25f > 0.0f && wlr.double_flag) {
            wlr.sky_cnt++;
        }
        if (wlr.sky_cnt > 5) {
            wlr.sky_cnt = 0;
            wlr.sky_flag = WLR_SKY_AIR_FOLDING;
        }	
    } else if (wlr.sky_flag == WLR_SKY_AIR_FOLDING) {
        wlr.high_set = 0.12f;
		sky_height_ramp.out = wlr.high_set;
        if(wlr.double_flag)
            x3_balance_zero = x3_balance_zero_normal;
        else
            x3_balance_zero = -0.1f;
        
        x5_balance_zero = 0.0f;
        wlr.sky_cnt++;
        target_cnt = (wlr.double_flag ? 175 : 175);
        if (wlr.sky_cnt > target_cnt) {
            wlr.sky_cnt = 0;
            wlr.sky_flag = WLR_SKY_LANDING;
        }
    } else if (wlr.sky_flag == WLR_SKY_LANDING) {
        wlr.high_set = 0.33f;
		sky_height_ramp.out = wlr.high_set;
		x3_balance_zero = x3_balance_zero_normal;
		x5_balance_zero = (wlr.double_flag ? x3_balance_zero_normal + 0.1f : x3_balance_zero_normal);
		if(wlr.double_flag)
			data_limit(&wlr.v_ref,-2.0f,2.0f);
		else
			data_limit(&wlr.v_ref,-2.0f,2.0f);
        x5_balance_zero = 0.0f;
		wlr.sky_cnt++;
		if((wlr.side[0].Fn_kal > 150.0f && wlr.side[1].Fn_kal > 150.0f) || (wlr.sky_cnt > 150))
		{
			wlr.sky_cnt = 0;
			wlr.sky_flag = WLR_SKY_STAND;
		}
    } 
	else if(wlr.sky_flag == WLR_SKY_STAND)
	{
        sky_leg_length = (wlr.double_flag ? 0.33f : 0.16f);
		wlr.high_set = ramp_calc(&sky_height_ramp, sky_leg_length);
		target_cnt = (wlr.double_flag ? 0 : 400);
		DO_LAST(!wlr.sky_cnt,target_cnt){
			wlr.sky_cnt++;
			if(wlr.double_flag)
				data_limit(&wlr.v_ref,-2.0f,2.0f);
			else
				data_limit(&wlr.v_ref,-2.0f,2.0f);
			x3_balance_zero = x3_balance_zero_normal;
		};
        x5_balance_zero = 0.0f;
		if(wlr.sky_cnt >= target_cnt)
		{
			x3_balance_zero = x3_balance_zero_normal;
			wlr.sky_over = 1;
            jump_ramp.out = 0.0f;
		}
	}
//	if (wlr.jump_flag == WLR_JUMP_IDLE && wlr.sky_flag == WLR_SKY_IDLE) {
//		if (!wlr_either_leg_flying()) {
//			if (fabs(chassis_imu.pit) > 0.20f || fabs(lqr.X_diff[4]) > 0.7f || fabs(lqr.X_diff[6]) > 0.7f) {
//				wlr.high_set = 0.18f;
//			}
//		}
//	}
}

static void handle_stair_state(void)
{
	static uint8_t stair_cnt;
	static uint8_t stair_ascend;
    if(wlr.stair_flag == WLR_STAIR_ASCEND)
	{
		stair_cnt = 0;
		wlr.v_ref = ramp_calc(&stair_ramp, (wlr.direction == 1) ? 1.3f : -1.5f);
		 Fy_ramp[0].out = Fy_ramp[1].out= 0;
		 wlr.high_set = ramp_calc(&height_ramp, LegLengthStair);
		 x3_balance_zero = x3_balance_zero_normal;
         x5_balance_zero = 0.0f;
		 if(check_is_statir())
			 stair_ascend = 1;
		 if(fabsf(wlr.pit_fdb) > 0.1f && stair_ascend) {
			 stair_ascend = 0;
			wlr.high_set = 0.12f;
			wlr.stair_flag = WLR_STAIR_RECOVER_SHORT;
		 }
	}
    else if(wlr.stair_flag == WLR_STAIR_RECOVER_SHORT)
	{
		wlr.high_set = 0.12f;
		wlr.v_ref = ramp_calc(&stair_ramp, (wlr.direction == 1) ? 1.3f : -1.5);//		data_limit(&wlr.v_ref,-1.5f,1.5f);        wlr.high_set = 0.12f;
        x3_balance_zero = x3_balance_zero_normal;
        if (fabsf(wlr.high_set - vmc[0].L_fdb) < 0.015f && fabsf(wlr.high_set - vmc[1].L_fdb) < 0.015f) {
			wlr.stair_flag = WLR_STAIR_RECOVER_LONG;
			height_ramp.out = 0.12f;
        }
	}
    else if(wlr.stair_flag == WLR_STAIR_RECOVER_LONG)
	{
//		data_limit(&wlr.v_ref,-1.5f,1.5f);
		wlr.v_ref = ramp_calc(&stair_ramp, (wlr.direction == 1) ? 1.3f : -1.5f);
		x3_balance_zero = ((wlr.direction == 1) ? 1.5f : -2.0f);
		wlr.high_set = 0.12f;
		stair_cnt++;
		if(stair_cnt > 25){
			stair_cnt = 0;
			wlr.stair_flag = WLR_STAIR_LANDING;
			
		}
	}
    else if(wlr.stair_flag == WLR_STAIR_LANDING)
	{
//		data_limit(&wlr.v_ref,-1.5f,1.5f);
		wlr.v_ref = ramp_calc(&stair_ramp, (wlr.direction == 1) ? 2.0f : -2.0f);
		x3_balance_zero = x3_balance_zero_normal;
		wlr.high_set = 0.21f;
		DO_LAST(wlr_both_legs_flying(),100){
			if(wlr.direction == 0){
				x3_balance_zero = x3_balance_zero_normal;
				data_limit(&wlr.v_ref,-1.0f,1.0f);
			}
			else{
				x3_balance_zero = x3_balance_zero_normal + 0.18f;	
				data_limit(&wlr.v_ref,-1.0f,1.0f);
			}
		}
	}
	else if(wlr.stair_flag == WLR_STAIR_IDLE) {
		stair_ramp.out = 0.0f;
		stair_ascend = 0;
	}
}

static void update_rotate_state(void)
{
    if (rotate_flag) {
        wlr.high_set = LegLengthRotate;
		if(g_robot_ctx.input.kb.bit.C)
			wlr.high_set = LegLengthRotateHigh;
		else
			wlr.high_set = LegLengthRotate;
		pid_L_test[0].i_out = pid_L_test[1].i_out = 0;
       	 if (g_robot_ctx.output.chassis  == CHASSIS_LOW_SPIN) {
			x5_balance_zero = -0.02f;
			Rotate_balance_zero = pid_calc(&pid_rotate_balance_zero,0,(fabsf(driver_motor[0].velocity) - fabsf(driver_motor[1].velocity)));
        } else {
            Rotate_balance_zero = 0.063f;
			x5_balance_zero = 0.06f;
        }
//        K_Array_Leg_rotate[0][3] = -ramp_calc(&wz_ramp, 0.254976f);		//K_Array_Leg_rotate[0][3] 越大 小陀螺越不稳定
//        K_Array_Leg_rotate[1][3] = ramp_calc(&wz_ramp, 0.254976f);		//K_Array_Leg_rotate[1][3] 越大 小陀螺越不稳定
		if(fabsf(wlr.v_ref) > 0.5f)
		{
			K_Array_Leg_rotate[0][3] = -ramp_calc(&wz_ramp, 0.23574f);		//K_Array_Leg_rotate[0][3] 越大 小陀螺越不稳定
			K_Array_Leg_rotate[1][3] = ramp_calc(&wz_ramp, 0.23574f);
			K_Array_Leg_rotate[2][3] = -1.17273f;		                    //K_Array_Leg_rotate[0][3] 越大 小陀螺越不稳定
			K_Array_Leg_rotate[3][3] = 1.17273f;
		}
		else
		{
			K_Array_Leg_rotate[0][3] = -ramp_calc(&wz_ramp, 1.23895f);		//K_Array_Leg_rotate[0][3] 越大 小陀螺越不稳定
			K_Array_Leg_rotate[1][3] = ramp_calc(&wz_ramp, 1.23895f);
			K_Array_Leg_rotate[2][3] = -2.17273f;		                    //K_Array_Leg_rotate[0][3] 越大 小陀螺越不稳定
			K_Array_Leg_rotate[3][3] = 2.17273f;
		}
		
    } else {
        Rotate_balance_zero = 0.0f;
//        wz_ramp.out = 2.0f;
    }
}

static void update_leg_references(void)
{
    tlm_gnd_roll_calc(&tlm, -(wlr.roll_fdb + IMU_Roll_balance_zero), vmc[0].L_fdb, vmc[1].L_fdb);		//计算地形倾角
    if (wlr.sky_flag != 0 || wlr.jump_flag != 0 || wlr.stair_flag != 0
        || wlr_both_legs_flying() || chassis.recover_flag != 0) {
        tlm.l_ref[0] = tlm.l_ref[1] = wlr.high_set;
    } else {
        tlm_leg_length_calc(&tlm, wlr.high_set, 0);							//根据地形倾角不断更新两腿腿长，调整到机身与地面平行
    }

    if (!rotate_flag) {
        for (int i = 0; i < WLR_SIDE_COUNT; i++) {
            if (wlr.side[i].fly_flag) {
                tlm.l_ref[i] += 0.04f;
                if (tlm.l_ref[i] > LegLengthMax) {
                    tlm.l_ref[i] = LegLengthMax;
                }
            }
        }
    }
}

static void select_control_matrix(void)
{
    if (wlr.ctrl_mode != 2) {
        return;
    }
    if (wlr.prone_flag) {		//匍匐模式
        aMartix_Cover(lqr.K, (float*)K_Array_Prone, 4, 10);
    } 
	else if ((wlr_both_legs_flying() && wlr.jump_flag == WLR_JUMP_IDLE && !chassis.recover_flag)) {		//双腿离地 && ,,, && chassis.recover_flag == 0
        aMartix_Cover(lqr.K, (float*)K_Array_Fly, 4, 10);
    } 
	else if (chassis.recover_flag == 2) {		//收腿起立时，先把车身以前导轮撑起，再收腿
        aMartix_Cover(lqr.K, (float*)K_Array_Leg_recover, 4, 10);
    } 
	else if (rotate_flag == 1 || rotate_ramp_flag == 1) {					//小陀螺
        aMartix_Cover(lqr.K, (float*)K_Array_Leg_rotate, 4, 10);
    } 
	else if(wlr.energy_flag) {
		aMartix_Cover(lqr.K, (float*)K_Array_Energy, 4, 10);
	}
	else if (wlr.jump_flag >= WLR_JUMP_ASCEND) {
        aMartix_Cover(lqr.K, (float*)K_Array_Leg_030, 4, 10);
    } 
	else if (wlr.sky_flag == WLR_SKY_FOLDING) {		//平地收腿运动
        if(wlr.double_flag)
            aMartix_Cover(lqr.K, (float*)K_Array_Leg_018, 4, 10);
        else
            aMartix_Cover(lqr.K, (float*)K_Array_Leg_015, 4, 10);
    } 
    else if (wlr.sky_flag > WLR_SKY_FOLDING) {		//蹬腿、空中收腿、落地缓冲、站立
        aMartix_Cover(lqr.K, (float*)K_Array_Sky, 4, 10);
    } 
	else if (wlr.high_flag == 2) {					//最长腿
        aMartix_Cover(lqr.K, (float*)K_Array_Leg_030, 4, 10);
    } 
	else if (wlr.high_flag == 1) {					//长腿
        aMartix_Cover(lqr.K, (float*)K_Array_Leg_020, 4, 10);
    } 
	else if (wlr.high_flag == 0) {					//短腿
		if(g_robot_ctx.output.chassis == CHASSIS_LOW && lqr.X_ref[1] == 0 && fabsf(lqr.X_fdb[1]) < 0.5f)
			aMartix_Cover(lqr.K, (float*)K_Array_Stop, 4, 10);
		else
			aMartix_Cover(lqr.K, (float*)K_Array_Leg_018, 4, 10);
    } 
	else {
        aMartix_Cover(lqr.K, (float*)K_Array_Leg_018, 4, 10);
    }
}

static void update_motion_reference(void)
{
	const static uint16_t swait_time = 700;
    wlr.roll_offs = pid_calc(&pid_roll, 0, wlr.roll_fdb + IMU_Roll_balance_zero);

    if (fabs(wlr.v_ref) < 1e-3f && wlr.jump_flag == WLR_JUMP_IDLE && wlr.sky_flag == WLR_SKY_IDLE && rotate_flag == 0) {
//		if(wlr.high_flag == 0)
//		{
//			K_Array_Leg_018[0][0] = -3.5025;
//			K_Array_Leg_018[1][0] = -3.5025;
//			K_Array_Leg_018[2][0] = 2.50324;
//			K_Array_Leg_018[3][0] = 2.50324;
//		}
        wlr.s_wait++;
        if (wlr.s_wait > swait_time) {
			wlr.s_wait = swait_time + 1;
            lqr.X_ref[0] = wlr.s_ref = wlr.s_adapt;
        } else {
            lqr.X_ref[0] = wlr.s_ref = wlr.s_adapt = wlr.s_fdb;
        }
    } else {
		K_Array_Leg_018[0][0] = -1.18614;
		K_Array_Leg_018[1][0] = -1.18614;
		K_Array_Leg_018[2][0] = 1.9169;
		K_Array_Leg_018[3][0] = 1.9169;
        wlr.s_wait = 0;
        lqr.X_ref[0] = wlr.s_ref = wlr.s_fdb;
        wlr.s_adapt = wlr.s_fdb;
    }

    if (wlr_both_legs_flying() || wlr.sky_flag >= WLR_SKY_EXTENDING) {
        lqr.X_ref[0] = wlr.s_ref = wlr.s_adapt = wlr.s_fdb;
    }

    if (wlr_either_leg_flying() && !wlr.prone_flag) {
        wlr.v_ref = 0;
//		lqr.X_diff[1] = 0;
    }

    if (rotate_flag == 1) {
        data_limit(&wlr.v_ref,-1.5f,1.5f);
		if(fabsf(wlr.v_ref) > 0.5f)
		{
			K_Array_Leg_rotate[0][1] = -3.24553f; K_Array_Leg_rotate[1][1] = -3.24553f;
			K_Array_Leg_rotate[2][1] = 1.96624f;  K_Array_Leg_rotate[3][1] = 1.96624f;
		}
		else 
		{
			K_Array_Leg_rotate[0][1] = 0.0f; K_Array_Leg_rotate[1][1] = 0.0f;
			K_Array_Leg_rotate[2][1] = 0.0f; K_Array_Leg_rotate[3][1] = 0.0f;
		}
			
    }

    lqr.X_ref[1] = wlr.v_ref;
    lqr.X_ref[2] = wlr.yaw_ref;
    lqr.X_ref[3] = -wlr.wz_ref;

    wlr.inertial_offs = (mb / 2) * wlr.high_set * lqr.X_fdb[3] * lqr.X_ref[1] / (BodyWidth / 2) / 2;
}

static void update_fly_state(uint8_t index, float yaw_err)
{
    if ( fabs(chassis_imu.pit) < 0.50f &&  wlr.side[index].Fn_kal < 145.0f && rotate_flag == 0 && chassis.recover_flag == 0
		&& (wlr.sky_flag == WLR_SKY_IDLE) && (wlr.sky_flag == WLR_JUMP_IDLE))  {
        wlr.side[index].fly_cnt += 10;
    } else if (wlr.side[index].fly_cnt > 0) {
        wlr.side[index].fly_cnt -= 5;
        if (wlr.side[index].Fn_kal > 160.0f) {
            wlr.side[index].fly_cnt -= 40;
        }
        if (wlr.side[index].fly_cnt < 0) {
            wlr.side[index].fly_cnt = 0;
        }
    }
    if (wlr.side[index].fly_cnt > 30) {
        wlr.side[index].fly_cnt = 30;
        wlr.side[index].fly_flag = 1;
		wlr.K_adapt = 0.0f;
		pid_L_test[index].i_out = 0.0f;
		
    } else if (wlr.side[index].fly_cnt == 0) {
        wlr.side[index].fly_flag = 0;
		wlr.K_adapt = -0.0f;
    }
}

static void handle_quadrant_protection(uint8_t index)
{
    if ((vmc[index].quadrant == 4 || vmc[index].quadrant == 3 || fabs(chassis_imu.pit) > PI / 3.0f
         || fabs(lqr.X_fdb[4] - lqr.X_fdb[6]) > 0.8f || fabs(lqr.X_diff[4]) > 0.8f
         || fabs(lqr.X_diff[6]) > 0.8f)
        && (wlr.sky_flag == WLR_SKY_IDLE) && (wlr.jump_flag == WLR_JUMP_IDLE) && (wlr.stair_flag == WLR_STAIR_IDLE)
        && chassis.recover_flag == 0) {
        quadrant_cnt++;
        if (quadrant_cnt > 200) {
            chassis.recover_flag = 1;
            wlr.high_flag = 0;
        }
    } else {
        quadrant_cnt = 0;
    }

    if (wlr.prone_flag) {
        chassis.recover_flag = 0;
    }
}

static void map_virtual_force(uint8_t index)
{
    float Fy_temp;

    if (wlr_both_legs_flying() && wlr.jump_flag == WLR_JUMP_IDLE	//两条腿都在空中 && 
        && wlr.sky_flag == WLR_SKY_IDLE && !chassis.recover_flag) {	// && 未进入翻倒自起立 && 跳跃未完成
        wlr.side[index].Fy = pid_calc(&pid_leg_length_fly[index], tlm.l_ref[index], vmc[index].L_fdb) - 30.0f;
//		wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - ff_Fy_1;
    } 
	else if ((chassis.recover_flag >= 1 && chassis.rescue_inter_flag == CHASSIS_RESCUE_RECOVER) || wlr.jump_flag == WLR_JUMP_RECOVER_SHORT
		|| wlr.stair_flag == WLR_STAIR_RECOVER_SHORT) {		//进入翻倒自起立 && 进入收腿阶段
		Fy_temp = pid_calc(&pid_leg_recover[index], wlr.recover_length, vmc[index].L_fdb) - 75.0f;
        wlr.side[index].Fy = ramp_calc(&Fy_ramp[index], Fy_temp);
    } 
	else if (rotate_flag || rotate_ramp_flag == 1) {//小陀螺和小陀螺斜坡停
        wlr.side[index].Fy = pid_calc(&pid_rotate_leg[index], tlm.l_ref[index], vmc[index].L_fdb)
                              + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs) - 20.0f;
    }
	else if(wlr.jump_flag == WLR_JUMP_ASCEND){//磕台阶站高
		wlr.side[index].Fy = pid_calc(&pid_ascend[index], tlm.l_ref[index], vmc[index].L_fdb) - 30.0f
                            + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
	}
	else if (wlr.sky_flag == WLR_SKY_FOLDING) {//准备跳
        if(wlr.double_flag)
            wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - ff_Fy_0
                              + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
        else
            wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - 30.0f
								+ WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
    } 
	else if (wlr.sky_flag == WLR_SKY_EXTENDING) {//蹬腿跳
//		Fy_temp = pid_calc(&pid_leg_sky_jump[index], tlm.l_ref[index], vmc[index].L_fdb);
		Fy_temp = 400.0f;
		wlr.side[index].Fy = ramp_calc(&sky_ramp[index], Fy_temp);
    } 
	else if (wlr.sky_flag == WLR_SKY_AIR_FOLDING) {//空中收腿
        Fy_temp = pid_calc(&pid_leg_sky_cover[index], tlm.l_ref[index], vmc[index].L_fdb) - 100.0f ;
        wlr.side[index].Fy = ramp_calc(&Fy_ramp[index], Fy_temp);
    } 
	else if (wlr.sky_flag == WLR_SKY_LANDING || wlr.stair_flag == WLR_STAIR_RECOVER_LONG) {//跳跃落地缓冲
         wlr.side[index].Fy = pid_calc(&pid_leg_length_fly[index], tlm.l_ref[index], vmc[index].L_fdb);
    } 
	else if (wlr.sky_flag == WLR_SKY_STAND) {//跳跃结束
        if(wlr.double_flag)
            wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - 60.0f
                              + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
        else
            wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - 30.0f
                              + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
    } 
	else if (wlr.energy_flag){//击打能量机关
		wlr.side[index].Fy = pid_calc(&pid_energy_leg[index], tlm.l_ref[index], vmc[index].L_fdb) - 60.0f;
	}
	else if (wlr.high_flag == 1){//中腿长
        wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - 10.0f
                              + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
    }
	else {//低腿长
        wlr.side[index].Fy = pid_calc(&pid_L_test[index], tlm.l_ref[index], vmc[index].L_fdb) - (45.0f * fabsf(arm_cos_f32(vmc[index].q_fdb[0])) + ff_Fy_0)
                              + WLR_SIGN(index) * (wlr.roll_offs + wlr.inertial_offs);
    }
    if (chassis.recover_flag == 1) {
        wlr.side[index].T0 = 0;												
    } else {																
        wlr.side[index].T0 = lqr.U_ref[2 + index];
    }

    handle_quadrant_protection(index);

    vmc_inverse_solution_five(&vmc[index], wlr.high_set, PI / 2 + x3_balance_zero, wlr.side[index].T0, wlr.side[index].Fy);
}

static void apply_output_limits(void)
{
    for (int i = 0; i < WLR_SIDE_COUNT; i++) {

        data_limit(&lqr.U_ref[i], -4.0f, 4.0f);
        
        if (wlr.crash_flag || wlr.energy_flag || (wlr.jump_flag == WLR_JUMP_RECOVER_SHORT)) {
            lqr.U_ref[i] *= 0.0f;
        } else if (chassis.recover_flag >= 1 || wlr.sky_flag == WLR_SKY_AIR_FOLDING || wlr.sky_flag == WLR_SKY_LANDING) {
            lqr.U_ref[i] *= 0.0f;
        }

        wlr.side[i].T1 = vmc[i].T_ref.e.T2_ref;
        wlr.side[i].T2 = vmc[i].T_ref.e.T1_ref;
        wlr.side[i].Tw = lqr.U_ref[i];
        wlr.side[i].P1 = vmc[i].q_ref[1];
        wlr.side[i].P2 = vmc[i].q_ref[2];
    }
}

void wlr_init(void)
{
	wlr.high_set = LegLengthNormal;
	wlr.crash_flag = 0;
	wlr.K_adapt = -0.0f;
    wlr.recover_length = 0.13f;
    
	ramp_init(&height_ramp, 0.001f, LegLengthMin, LegLengthMax);			//日常腿长斜坡
	ramp_init(&sky_height_ramp, 0.001f, LegLengthMin, LegLengthMax);		//空中腿长斜坡
	ramp_init(&jump_ramp, 0.008f, -3.0f, 3.0f);
	ramp_init(&stair_ramp, 0.004f, -1.5f, 1.5f);
	ramp_init(&wz_ramp, 0.05f,  0,  3.0f);									//小陀螺加速K矩阵wz项斜坡
	ramp_init(&sky_ramp[0], 10.0f, -450.0f,  450.0f);						//伸腿支持力斜坡
	ramp_init(&sky_ramp[1], 10.0f, -450.0f,  450.0f);						//伸腿支持力斜坡
	ramp_init(&Fy_ramp[0], 4.0f, -600.0f,  600.0f);							//收腿支持力斜坡
	ramp_init(&Fy_ramp[1], 4.0f, -600.0f,  600.0f);							//收腿支持力斜坡
	
    for (int i = 0; i < WLR_SIDE_COUNT; i++) 
	{
		//腿部长度初始化
//		vmc_init(&vmc[i], LegLengthParam);
		
		vmc_init_five(&vmc[i], LegLengthParam_five);
		//PID参数初始化      
        pid_init(&pid_leg_sky_cover[i], NONE, 1800, 1.5f, 0.0f,150,500);		    //空中收腿专用pid
		pid_init(&pid_leg_sky_jump[i],  NONE, 2500, 3.0, 0.0f, 150.0, 500);			//跳跃专用pid
		pid_init(&pid_leg_recover[i], NONE, 1800, 1.5f, 20000.0f, 300, 500);		//起身专用pid
        pid_init(&pid_leg_length_fly[i], NONE, 1000, 0.0, 0, 0, 300);			//离地腿长/缓冲腿长pid
        pid_init(&pid_L_test[i], CHANG_I_RATE,900, 2.0, 40000, 70, 300);			//日常腿长pid
		pid_L_test[i].threshold_a = 0.01f;
		pid_L_test[i].threshold_b = 0.03f;
		pid_init(&pid_rescue[i], NONE, 2.0f, 0.5f, 0, 45, 50);						//翻倒起身腿转速pid
		pid_init(&pid_rotate_leg[i], NONE, 1500.0f, 0.0f, 40000.0f, 0, 300);		
		pid_init(&pid_energy_leg[i], NONE, 1000.0f, 0.0f, 40000.0f, 0, 300);
		pid_init(&pid_ascend[i], NONE, 700, 0.0f, 80000, 70, 300);			//磕台阶腿长pid
		pid_init(&pid_rotate_balance_zero, NONE, 0.015f, 0.0f, 0, 0.0f, 0.1f);			//磕台阶腿长pid
	}
	pid_init(&pid_roll, NONE, 300, 0,0, 0, 40);								//roll偏移支持力补偿
	//卡尔曼滤波器初始化
	DO_ONCE({
		twm_init(&twm, BodyWidth, WheelRadius);
		tlm_init(&tlm, LegLengthMax, LegLengthMin, BodyWidth);
		for(uint8_t i = 0;i < WLR_SIDE_COUNT; i++){
			kalman_filter_init(&kal_fn[i], 1, 0, 1);
			kal_fn[i].A_data[0] = 1;
			kal_fn[i].H_data[0] = 1;
			kal_fn[i].Q_data[0] = 1;
			kal_fn[i].R_data[0] = 500;
							
			kalman_filter_init(&tfmini_fn[i], 1, 0, 1);
			tfmini_fn[i].A_data[0] = 1;
			tfmini_fn[i].H_data[0] = 1;
			tfmini_fn[i].Q_data[0] = 1;
			tfmini_fn[i].R_data[0] = 1000;
		}
	});
}
//wlr控制保护处理
void wlr_protest(void)
{
	pid_leg_recover[0].i_out = 0.0f;
	pid_leg_recover[1].i_out = 0.0f;
	pid_L_test[0].i_out = 0.0f;
	pid_L_test[1].i_out = 0.0f;
    height_ramp.out = 0.1f;
	sky_height_ramp.out = 0.1f;
    wlr.s_ref = wlr.s_fdb;
	wlr.s_adapt = wlr.s_fdb;
}

//王工知乎开源 求得整车实际速度
float get_real_vel(void)
{
	float vel;
	float wheel_left = -driver_motor[0].velocity - vmc[0].V_fdb.e.vw0_fdb - wlr.wy_fdb;
	float wheel_right = driver_motor[1].velocity - vmc[1].V_fdb.e.vw0_fdb - wlr.wy_fdb;
	vel = (wheel_left * WheelRadius + vmc[0].V_fdb.e.vy0_fdb * arm_sin_f32(lqr.X_fdb[4]) + vmc[0].L_fdb * lqr.X_fdb[5] * arm_cos_f32(lqr.X_fdb[4]) \
		+ wheel_right * WheelRadius + vmc[1].V_fdb.e.vy0_fdb * arm_sin_f32(lqr.X_fdb[6]) + vmc[1].L_fdb * lqr.X_fdb[7] * arm_cos_f32(lqr.X_fdb[6])) \
		/ 2.0f;
	return vel;
}

//轮子：位移、速度   摆角：角度、角速度   机体俯仰：角度、角速度
void wlr_control(void)
{
    wlr.s_fdb = (wlr.side[0].qy * WheelRadius + wlr.side[1].qy * WheelRadius) / 2.0f;
    wlr.v_fdb = (wlr.side[0].wy * WheelRadius + wlr.side[1].wy * WheelRadius) / 2.0f;

	real_vel = get_real_vel();
	Fusion_Vel_Acc_Test();
    for (int i = 0; i < WLR_SIDE_COUNT; i++) {
        vmc_forward_solution_five(&vmc[i], wlr.side[i].q2, wlr.side[i].q1, wlr.side[i].w2,
                                  wlr.side[i].w1, wlr.side[i].t2, wlr.side[i].t1);
    }
    lqr.X_fdb[0] = wlr.s_fdb;
    //小陀螺下不用加速度融合速度
	if(rotate_flag || rotate_ramp_flag)
		lqr.X_fdb[1] = wlr.v_fdb;
	else
		lqr.X_fdb[1] = kal_fusion_vel.filter_vector[1];
    lqr.X_fdb[2] = wlr.yaw_fdb;
    lqr.X_fdb[3] = -wlr.wz_fdb;
	
    lqr.X_fdb[8] = x5_balance_zero + wlr.pit_fdb;		//wlr.pit_fdb = -chassis_imu.pit;
    lqr.X_fdb[9] = wlr.wy_fdb;
	
    lqr.X_fdb[4] = x3_balance_zero + (-PI / 2 + lqr.X_fdb[8] + vmc[0].q_fdb[0]);
    lqr.X_fdb[5] = lqr.X_fdb[9] + vmc[0].V_fdb.e.vw0_fdb;
    lqr.dot_leg_w[0] = (lqr.X_fdb[5] - lqr.last_leg_w[0]) / 0.002f;
    lqr.last_leg_w[0] = lqr.X_fdb[5];
    
	lqr.X_fdb[6] = x3_balance_zero + (-PI / 2 + lqr.X_fdb[8] + vmc[1].q_fdb[0]) + Rotate_balance_zero;
    lqr.X_fdb[7] = lqr.X_fdb[9] + vmc[1].V_fdb.e.vw0_fdb;
    lqr.dot_leg_w[1] = (lqr.X_fdb[7] - lqr.last_leg_w[1]) / 0.002f;
    lqr.last_leg_w[1] = lqr.X_fdb[7];

    for (int i = 0; i < WLR_SIDE_COUNT; i++) {
		wlr.side[i].Front_dis_fdb = ms53l0m_distance_m[i];
        tfmini_fn[i].measured_vector[0] = wlr.side[i].Front_dis_fdb;
        kalman_filter_update(&tfmini_fn[i]);
        wlr.side[i].Front_dis_kal = tfmini_fn[i].filter_vector[0];
    }

    for (int i = 0; i < WLR_SIDE_COUNT; i++) {
        float L0_array[3] = {vmc[i].L_fdb, vmc[i].V_fdb.e.vy0_fdb, vmc[i].Acc_fdb.L0_ddot};
        float theta_array[3] = {lqr.X_fdb[4 + 2 * i], lqr.X_fdb[5 + 2 * i], lqr.dot_leg_w[i]};
        wlr.side[i].Fn_fdb = wlr_fn_calc(wlr.az_fdb, vmc[i].F_fdb.e.Fy_fdb, vmc[i].F_fdb.e.T0_fdb, L0_array, theta_array);
		//期望虚拟杆支持力，转矩
		//大于0表示腿正在往外蹬，小于0表示正在往内收
		F_wy[i] = Fwy;
		F_test[i] = gas_spring_F_Calc(vmc[i]);//气弹簧力解算
        kal_fn[i].measured_vector[0] = wlr.side[i].Fn_fdb;
        kalman_filter_update(&kal_fn[i]);		
		wlr.side[i].Fn_kal = (kal_fn[i].filter_vector[0] + F_test[i]);
    }

    float yaw_err = circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, wlr.yaw_fdb, 2 * PI);
    yaw_err = fabsf(yaw_err);
	
    for (int i = 0; i < WLR_SIDE_COUNT; i++) {
        update_fly_state(i, yaw_err);			//更新fly_flag和fly_cnt
    }
	
    update_leg_height_and_balance(yaw_err);		//更新腿长与x3 x5偏置
    reset_jump_state();							//上台阶 || 飞天标志位清零
	handle_stair_state();
    handle_jump_state();						//磕上二级台阶
    handle_sky_state();							//飞天全过程
    update_rotate_state();						//更新在小陀螺下的K矩阵（K_Array_Leg_rotate[0][3]和K_Array_Leg_rotate[1][3]）
    update_leg_references();					//更新不同倾角下两腿腿长
    select_control_matrix();					//选择对应K矩阵
    update_motion_reference();					//更新不同运动状态下，状态变量的值  
    stable_velocity_control();                  //速度限制   

    aMartix_Add(1, lqr.X_ref, -1, lqr.X_fdb, lqr.X_diff, 10, 1);
	lqr.X_diff[2] = circle_error(lqr.X_ref[2],lqr.X_fdb[2],2 * PI);
	
    if (g_robot_ctx.output.chassis  == CHASSIS_LOW_SPIN) {
        data_limit(&lqr.X_diff[1], -1.25f, 1.25f);
    } else {
        data_limit(&lqr.X_diff[1], -1.8f, 1.8f);
    }
    if(chassis.turn_back_flag || (g_robot_ctx.output.chassis == CHASSIS_ASCEND && wlr.direction == 1)
		|| (g_robot_ctx.output.chassis == CHASSIS_STAIR && wlr.direction == 0))//切换跟随时屏蔽运动有关项且限制yaw_err的差值
    {
        data_limit(&lqr.X_diff[0], 0.0f, 0.0f);
        data_limit(&lqr.X_diff[1], 0.0f, 0.0f);
        data_limit(&lqr.X_diff[2], -0.25f, 0.25f);
        data_limit(&lqr.X_diff[3], 0.0f, 0.0f);
    }
	//功率限制
    power_limit_current();
	//LQR的K增益带入计算
    aMartix_Mul(lqr.K, lqr.X_diff, lqr.U_ref, 4, 10, 1);

//    p_array_fit(P_Array, vmc[0].L_fdb, vmc[1].L_fdb);
	//上交2023管易恒开源速度预测
    state_predict();
	wlr.side[0].T_adapt = wlr.K_adapt * (wlr.side[0].predict_wy - wlr.side[0].wy);
	wlr.side[1].T_adapt = wlr.K_adapt * (wlr.side[1].predict_wy - wlr.side[1].wy);
	
    for (int i = 0; i < WLR_SIDE_COUNT; i++) {
        map_virtual_force(i);		//虚拟力映射（计算Fy和T0 + 五连杆逆解算）
    }
    apply_output_limits();
}

