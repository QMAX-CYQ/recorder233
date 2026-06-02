#ifndef __BSP_JUDGE_H
#define __BSP_JUDGE_H

#include "usart.h"
#include "stdint.h"


//帧头
typedef __PACKED_STRUCT
{
    uint8_t SOF;            //数据帧起始字节，固定值为 0xA5
    uint16_t data_length;   //数据帧中 data 的长度
    uint8_t seq;            //包序号
    uint8_t CRC8;           //帧头 CRC8 校验
} frame_header_t;

//命令码
typedef enum
{
    ID_game_status                  = 0x0001, //比赛状态数据，固定以1Hz频率发送
    ID_game_result                  = 0x0002, //比赛结果数据，比赛结束触发发送
    ID_game_robot_HP                = 0x0003, //机器人血量数据，固定以3Hz频率发送
	
    ID_event_data                   = 0x0101, //场地事件数据，固定以1Hz频率发送
    ID_referee_warning              = 0x0104, //裁判警告数据，己方判罚/判负时触发发送，其余时间以1Hz频率发送
    ID_dart_info                    = 0x0105, //飞镖发射相关数据，固定以1Hz频率发送

    ID_robot_status                 = 0x0201, //机器人性能体系数据，固定以10Hz频率发送
    ID_power_heat_data              = 0x0202, //实时底盘功率和枪口热量数据，固定以50Hz频率发送
    ID_robot_pos                    = 0x0203, //机器人位置数据，固定以1Hz频率发送
    ID_buff                         = 0x0204, //机器人增益数据，固定以3Hz频率发送
    ID_hurt_data                    = 0x0206, //伤害状态数据，伤害发生后发送
    ID_shoot_data                   = 0x0207, //实时射击数据，弹丸发射后发送
    ID_projectile_allowance         = 0x0208, //允许发弹量，固定以10Hz频率发送
    ID_rfid_status                  = 0x0209, //机器人RFID模块状态，固定以3Hz频率发送
    ID_dart_client_cmd              = 0x020A, //飞镖选手端指令数据，固定以3Hz频率发送
    ID_ground_robot_position        = 0x020B, //地面机器人位置数据，固定以1Hz频率发送
    ID_radar_mark_data              = 0x020C, //雷达标记进度数据，固定以1Hz频率发送
    ID_sentry_info                  = 0x020D, //哨兵自主决策信息同步，固定以1Hz频率发送
    ID_radar_info                   = 0x020E, //雷达自主决策信息同步，固定以1Hz频率发送

    ID_robot_interaction_data       = 0x0301, //机器人交互数据，发送方触发发送，频率上限为30Hz
    ID_custom_robot_data            = 0x0302, //自定义控制器与机器人交互数据，发送方触发发送，频率上限为30Hz 图传链路
    ID_map_command                  = 0x0303, //选手端小地图交互数据，选手端触发发送
    ID_map_robot_data               = 0x0305, //选手端小地图接收雷达数据，频率上限为5Hz
    ID_custom_client_data           = 0x0306, //自定义控制器与选手端交互数据，发送方触发发送，频率上限为30Hz
    ID_map_data                     = 0x0307, //选手端小地图接收路径数据，频率上限为1Hz
    ID_custom_info                  = 0x0308, //选手端小地图接收机器人数据，频率上限为3Hz
	ID_robot_custom_data			= 0x0309, //自定义控制器接收机器人数据，频率上限为10Hz 图传链路
	
} cmd_id_e;

//数据段长度
typedef enum
{
    LEN_game_status                 = 11,
    LEN_game_result                 = 1,
    LEN_game_robot_HP               = 16,

    LEN_event_data                  = 4,
    LEN_referee_warning             = 3,
    LEN_dart_info                   = 3,

    LEN_robot_status                = 13,
    LEN_power_heat_data             = 14,
    LEN_robot_pos                   = 12,//手册有矛盾 有16和12
    LEN_buff                        = 8,
    LEN_hurt_data                   = 1,
    LEN_shoot_data                  = 7,
    LEN_projectile_allowance        = 6,
    LEN_rfid_status                 = 5,
    LEN_dart_client_cmd             = 6,
    LEN_ground_robot_position       = 40,
    LEN_radar_mark_data             = 2,
    LEN_sentry_info                 = 6,
    LEN_radar_info                  = 1,

    LEN_robot_interaction_data      = 118,
    LEN_custom_robot_data           = 30,
    LEN_map_command                 = 12,//手册有矛盾 有15和12
    LEN_map_robot_data              = 24,
    LEN_custom_client_data          = 8,
    LEN_map_data                    = 103,//手册有矛盾 有103和105
    LEN_custom_info                 = 34
} cmd_len_e;

//各数据结构体定义
typedef __PACKED_STRUCT
{
	/*bit 0-3：比赛类型 
	• 1：RoboMaster 机甲大师超级对抗赛 
	• 2：RoboMaster 机甲大师高校单项赛 
	• 3：ICRA RoboMaster高校人工智能挑战赛 
	• 4：RoboMaster机甲大师高校联盟赛3V3对抗  
	• 5：RoboMaster 机甲大师高校联盟赛步兵对抗 
	*/
    uint8_t game_type : 4;
	/*bit 4-7：当前比赛阶段 
	• 0：未开始比赛 
	• 1：准备阶段 
	• 2：十五秒裁判系统自检阶段 
	• 3：五秒倒计时 
	• 4：比赛中 
	• 5：比赛结算中
	*/
    uint8_t game_progress : 4;
    uint16_t stage_remain_time;//当前阶段剩余时间，单位：秒 
    uint64_t SyncTimeStamp;	   //UNIX时间，当机器人正确连接到裁判系统的NTP服务器后生效 
} game_status_t;

typedef __PACKED_STRUCT
{
	/*
	 0：平局 
	 1：红方胜利 
	 2：蓝方胜利
	*/
    uint8_t winner;
} game_result_t;

typedef __PACKED_STRUCT
{
    uint16_t ally_1_robot_HP;//己方1号英雄机器人血量，若该机器人未上场或者被罚下，则血量为0，下文同理
	uint16_t ally_2_robot_HP;//己方2号工程机器人血量
	uint16_t ally_3_robot_HP;//己方3号步兵机器人血量
	uint16_t ally_4_robot_HP;//己方4号步兵机器人血量
	uint16_t reserved;		 //保留位
	uint16_t ally_7_robot_HP;//己方7号哨兵机器人血量
	uint16_t ally_outpost_HP;//己方前哨站血量 
	uint16_t ally_base_HP;	 //己方基地血量
} game_robot_HP_t;

typedef __PACKED_STRUCT
{
    uint32_t supply_zone_occupied          : 1; // bit 0: 己方补给区占领状态，1为已占领
    uint32_t reserved_bit_1                : 1; // bit 1: 保留
    uint32_t supply_zone_occupied_rmul     : 1; // bit 2: RMUL附加补给区占领状态，1为已占领
    uint32_t small_power_rune_status       : 2; // bit 3-4: 己方小能量机关，0未激活，1已激活，2正在激活
    uint32_t large_power_rune_status       : 2; // bit 5-6: 己方大能量机关，0未激活，1已激活，2正在激活
    uint32_t central_highland_occupied     : 2; // bit 7-8: 己方中央高地占领状态
    uint32_t trapezoid_highland_occupied   : 2; // bit 9-10: 己方梯形高地占领状态
    uint32_t opponent_dart_hit_time        : 9; // bit 11-19: 对方飞镖最后一次击中己方前哨站或基地的时间，范围0-420
    uint32_t opponent_dart_hit_target      : 3; // bit 20-22: 对方飞镖最后一次命中的具体目标
    uint32_t center_gain_point_occupied    : 2; // bit 23-24: 中心增益点占领状态
    uint32_t fort_gain_point_occupied      : 2; // bit 25-26: 己方堡垒增益点占领状态
    uint32_t outpost_gain_point_occupied   : 2; // bit 27-28: 己方前哨站增益点占领状态
    uint32_t base_gain_point_occupied      : 1; // bit 29: 己方基地增益点占领状态，1为已占领
    uint32_t reserved_bit_30_31            : 2; // bit 30-31: 保留
} event_data_t;

typedef __PACKED_STRUCT
{
	/*己方最后一次受到判罚的等级： 
	 1：双方黄牌 
	 2：黄牌 
	 3：红牌 
	 4：判负
	*/
    uint8_t level;
	/* 己方最后一次受到判罚的违规机器人ID。（如红1机器人ID为1，蓝1机器人ID为101） 
		 判负和双方黄牌时，该值为0 */
    uint8_t offending_robot_id;
	/*己方最后一次受到判罚的违规机器人对应判罚等级的违规次数。（开局默认为0。） */
    uint8_t count;
} referee_warning_t;

typedef __PACKED_STRUCT
{
	/*己方飞镖发射剩余时间，单位：秒 */
    uint8_t dart_remaining_time;
	/* 
	bit 0-2： 
		最近一次己方飞镖击中的目标，开局默认为 0，1 为击中前哨站，2 为击
		中基地固定目标，3 为击中基地随机固定目标，4 为击中基地随机移动目
		标，5为击中基地末端移动目标 
 bit 3-5: 
		对方最近被击中的目标累计被击中计次数，开局默认为0，至多为4 
 bit 6-8： 
		飞镖此时选定的击打目标，开局默认或未选定/选定前哨站时为 0，选中基
		地固定目标为 1，选中基地随机固定目标为 2，选中基地随机移动目标为 
		3，选中基地末端移动目标为4 
 bit 9-15：保留
	*/
    uint16_t dart_info;
} dart_info_t;

typedef __PACKED_STRUCT
{
	/*本机器人ID*/
    uint8_t robot_id;
	/*机器人等级*/
    uint8_t robot_level;
	/*机器人当前血量*/
    uint16_t current_HP;
	/*机器人血量上限*/
    uint16_t maximum_HP;
	/*机器人射击热量每秒冷却值*/
    uint16_t shooter_barrel_cooling_value;
	/*机器人射击热量上限*/
    uint16_t shooter_barrel_heat_limit;
	/*机器人底盘功率上限*/
    uint16_t chassis_power_limit;
	/* 电源管理模块的输出情况： 
	 bit 0：gimbal口输出，0为无输出，1为 24V输出 
	 bit 1：chassis口输出，0为无输出，1为24V输出 
	 bit 2：shooter口输出，0为无输出，1为24V输出*/
    uint8_t power_management_gimbal_output : 1;
    uint8_t power_management_chassis_output : 1;
    uint8_t power_management_shooter_output : 1;
} robot_status_t;

typedef __PACKED_STRUCT
{
    uint16_t reserved_1; 
	uint16_t reserved_2; 
    float reserved_3;
	/*缓冲能量（单位：J）*/
    uint16_t buffer_energy;
	/*17mm发射机构的射击热量 */
    uint16_t shooter_17mm_barrel_heat;
	/*42mm发射机构的射击热量 */
    uint16_t shooter_42mm_barrel_heat;
} power_heat_data_t;

typedef __PACKED_STRUCT
{
	/*本机器人位置x坐标，单位：m*/
    float x;
	/*本机器人位置y坐标，单位：m */
    float y;
	/*本机器人测速模块的朝向，单位：度。正北为0度*/
    float angle;
} robot_pos_t;

typedef __PACKED_STRUCT
{
	/*机器人回血增益（百分比，值为10表示每秒恢复血量上限的10%）*/
    uint8_t recovery_buff;
	/*机器人射击热量冷却增益具体值（直接值，值为x表示热量冷却增加x/s）*/
    uint16_t cooling_buff;
	/*机器人防御增益（百分比，值为50表示50%防御增益）*/
    uint8_t defence_buff;
	/*机器人负防御增益（百分比，值为30表示-30%防御增益）*/
    uint8_t vulnerability_buff;
	/*机器人攻击增益（百分比，值为50表示50%攻击增益）*/
    uint16_t attack_buff;
	/*
	bit 0-6：机器人剩余能量值反馈，以 16 进制标识机器人剩余能量值比例，
		仅在机器人剩余能量小于 50%时反馈，其余默认反馈 0x80。机器人初始能量视为100% 
 bit 0：在剩余能量≥125%时为1，其余情况为0 
 bit 1：在剩余能量≥100%时为1，其余情况为0 
 bit 2：在剩余能量≥50%时为1，其余情况为0 
 bit 3：在剩余能量≥30%时为1，其余情况为0 
 bit 4：在剩余能量≥15%时为1，其余情况为0 
 bit 5：在剩余能量≥5%时为1，其余情况为0 
 bit 6：在剩余能量≥1%时为1，其余情况为0*/
	uint8_t remaining_energy; 
} buff_t;

typedef __PACKED_STRUCT
{
	/*bit 0-3：当扣血原因为装甲模块被弹丸攻击、受撞击或离线时，该4 bit组
		成的数值为装甲模块或测速模块的ID编号；当其他原因导致扣血时，该数
		值为0*/
    uint8_t armor_id : 4;
	/*bit 4-7：血量变化类型 
 0：装甲模块被弹丸攻击导致扣血 
 1：装甲模块或超级电容管理模块离线导致扣血 
 5：装甲模块受到撞击导致扣血 */
    uint8_t HP_deduction_reason : 4;
} hurt_data_t;

typedef __PACKED_STRUCT
{
	/*弹丸类型： 
	 bit 1：17mm弹丸 
	 bit 2：42mm弹丸 */
    uint8_t bullet_type;
	/*发射机构ID： 
	 1： 17mm发射机构  
	 2：保留位 
	 3：42mm发射机构*/
    uint8_t shooter_number;
	/*弹丸射速（单位：Hz）*/
    uint8_t launching_frequency;
	/*弹丸初速度（单位：m/s）*/
    float initial_speed;
} shoot_data_t;

typedef __PACKED_STRUCT
{
	/*机器人自身拥有的17mm弹丸允许发弹量*/
    uint16_t projectile_allowance_17mm;
	/*42mm弹丸允许发弹量*/
    uint16_t projectile_allowance_42mm;
	/*剩余金币数量*/
    uint16_t remaining_gold_coin;
	/*堡垒增益点提供的储备17mm弹丸允许发弹量；该值与机器人是否实际占领堡垒无关*/
	uint16_t projectile_allowance_fortress; 
} projectile_allowance_t;

typedef __PACKED_STRUCT /*：所有RFID卡仅在赛内生效。在赛外，即使检测到对应的RFID卡，对应值也为0。 */
{
    uint32_t rfid_status;
	uint8_t rfid_status_2; 
} rfid_status_t;

typedef __PACKED_STRUCT
{
	/*当前飞镖发射站的状态： 
	 1：关闭 
	 2：正在开启或者关闭中 
	 0：已经开启*/
    uint8_t dart_launch_opening_status;
    uint8_t reserved;
	/*切换击打目标时的比赛剩余时间，单位：秒，无/未切换动作，默认为0。 */
    uint16_t target_change_time;
	/*最后一次操作手确定发射指令时的比赛剩余时间，单位：秒，初始值为0。*/
    uint16_t latest_launch_cmd_time;
} dart_client_cmd_t;

typedef __PACKED_STRUCT /*场地围挡在红方补给站附近的交点为坐标原点,沿场地长边向蓝方为X轴正方向，沿场地短边向红方停机坪为Y轴正方向。*/
{
	/*己方英雄机器人位置x轴坐标，单位：m*/
    float hero_x;
	/*己方英雄机器人位置y轴坐标，单位：m*/
    float hero_y;
	/*己方工程机器人位置x轴坐标，单位：m*/
    float engineer_x;
	/*己方工程机器人位置y轴坐标，单位：m*/
    float enginerr_y;
	/*己方3号步兵机器人位置x轴坐标，单位：m*/
    float standard_3_x;
	/*己方3号步兵机器人位置y轴坐标，单位：m*/
    float standard_3_y;
	/*己方4号步兵机器人位置x轴坐标，单位：m*/
    float standard_4_x;
	/*己方4号步兵机器人位置y轴坐标，单位：m*/
    float standard_4_y;
    float reserved_1;
    float reserved_2;
} ground_robot_position_t;

typedef __PACKED_STRUCT
{
   uint16_t mark_progress;
} radar_mark_data_t;

typedef __PACKED_STRUCT
{
    uint32_t sentry_info;
	uint16_t sentry_info_2; 
} sentry_info_t;

typedef __PACKED_STRUCT
{
    uint8_t radar_info;
} radar_info_t;

typedef __PACKED_STRUCT
{
    uint8_t data[30];
} custom_robot_data_t;

typedef __PACKED_STRUCT 
{ 
	uint8_t data[30]; 
} robot_custom_data_t; 

typedef __PACKED_STRUCT
{
	float target_position_x; 
	float target_position_y; 
	uint8_t cmd_keyboard; 
	uint8_t target_robot_id; 
	uint16_t cmd_source;
} map_command_t;

typedef __PACKED_STRUCT
{
    uint16_t hero_position_x; 
	uint16_t hero_position_y; 
	uint16_t engineer_position_x; 
	uint16_t engineer_position_y; 
	uint16_t infantry_3_position_x; 
	uint16_t infantry_3_position_y; 
	uint16_t infantry_4_position_x; 
	uint16_t infantry_4_position_y; 
	uint16_t reserved; 
	uint16_t reserved_1; 
	uint16_t sentry_position_x; 
	uint16_t sentry_position_y;
} map_robot_data_t;

typedef __PACKED_STRUCT
{
    uint16_t key_value;
    uint16_t x_position : 12;
    uint16_t mouse_left : 4;
    uint16_t y_position : 12;
    uint16_t mouse_right : 4;
    uint16_t reserved;
} custom_client_data_t;

typedef __PACKED_STRUCT
{
    uint8_t intention;
    uint16_t start_position_x;
    uint16_t start_position_y;
    int8_t delta_x[49];
    int8_t delta_y[49];
    uint16_t sender_id;
} map_data_t;

typedef __PACKED_STRUCT
{
    uint16_t sender_id;
    uint16_t receiver_id;
    uint8_t user_data[30];
} custom_info_t;

//机器人交互数据
typedef __PACKED_STRUCT
{
    uint16_t data_cmd_id;
    uint16_t sender_id;
    uint16_t receiver_id;
    uint8_t user_data[112];
} robot_interaction_data_t;

typedef enum
{
    ID_robot_interaction    = 0x0200,
    ID_delete_figure        = 0x0100,
    ID_draw_one_figure      = 0x0101,
    ID_draw_two_fiugre      = 0x0102,
    ID_draw_five_figure     = 0x0103,
    ID_draw_seven_fiugre    = 0x0104,
    ID_draw_character_figure= 0x0110,
    ID_sentry_cmd           = 0x0120,
    ID_radar_cmd            = 0x0121
} robot_interaction_id_e;

typedef enum
{
    LEN_robot_interaction    = 112,
    LEN_delete_figure        = 2,
    LEN_draw_one_figure      = 15,
    LEN_draw_two_fiugre      = 30,
    LEN_draw_five_figure     = 75,
    LEN_draw_seven_fiugre    = 105,
    LEN_draw_character_figure= 45,
    LEN_sentry_cmd           = 4,
    LEN_radar_cmd            = 1
} robot_interaction_len_e;

//0x0200-0x02FF
//0x0100 选手端删除图层
typedef __PACKED_STRUCT
{
    uint8_t delete_type;
    uint8_t layer;
} interaction_layer_delete_t;

//0x0101 选手端绘制一个图形
typedef __PACKED_STRUCT
{
    uint8_t figure_name[3];
    uint32_t operate_type : 3;
    uint32_t figure_type : 3;
    uint32_t layer : 4;
    uint32_t color : 4;
    uint32_t details_a : 9;
    uint32_t details_b : 9;
    uint32_t width : 10;
    uint32_t start_x : 11;
    uint32_t start_y : 11;
    uint32_t details_c : 10;
    uint32_t details_d : 11;
    uint32_t details_e : 11;
} interaction_figure_t;

//0x0102 选手端绘制两个图形
typedef __PACKED_STRUCT
{
    interaction_figure_t interaction_figure[2];
} interaction_figure_2_t;

//0x0103 选手端绘制五个图形
typedef __PACKED_STRUCT
{
    interaction_figure_t interaction_figure[5];
} interaction_figure_3_t;

//0x0104 选手端绘制七个图形
typedef __PACKED_STRUCT
{
    interaction_figure_t interaction_figure[7];
} interaction_figure_4_t;

//0x0110 选手端绘制字符图形
typedef __PACKED_STRUCT
{
    interaction_figure_t interaction_figure;
    uint8_t data[30];
} ext_client_custom_character_t;

//0x0120 哨兵自主决策指令
typedef __PACKED_STRUCT
{
    uint32_t sentry_cmd;
} sentry_cmd_t;

//0x0121 雷达自主决策指令
typedef __PACKED_STRUCT
{
    uint8_t radar_cmd; 
	uint8_t password_cmd; 
	uint8_t password_1; 
	uint8_t password_2; 
	uint8_t password_3; 
	uint8_t password_4; 
	uint8_t password_5; 
	uint8_t password_6;
} radar_cmd_t;

extern frame_header_t frame_header;

extern game_status_t game_status;
extern robot_status_t robot_status;
extern shoot_data_t shoot_data;
extern power_heat_data_t power_heat_data;
extern event_data_t event_data;

void judge_init(UART_HandleTypeDef *huart);
uint8_t judge_get_data(uint8_t *data);
void judge_send_data(uint8_t* message, int length);
uint8_t judge_check_offline(void);

#endif
