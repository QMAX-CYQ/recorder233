//
// Created by RM UI Designer
// Static Edition
//

#ifndef UI_g_H
#define UI_g_H

#include "ui_interface.h"

extern ui_interface_line_t *ui_g_1_left_big_leg;
extern ui_interface_line_t *ui_g_1_left_small_leg;
extern ui_interface_line_t *ui_g_1_right_big_leg;
extern ui_interface_round_t *ui_g_1_current_high_flag;
extern ui_interface_line_t *ui_g_1_right_small_leg;
extern ui_interface_rect_t *ui_g_1_supercap_capcity;
extern ui_interface_number_t *ui_g_1_supcap_voltage;

void ui_init_g_1();
void ui_update_g_1();
void ui_remove_g_1();

extern ui_interface_number_t *ui_g_2_current_trigger;
extern ui_interface_number_t *ui_g_2_target_trigger;
extern ui_interface_arc_t *ui_g_2_head_position;
extern ui_interface_number_t *ui_g_2_vision_trice_id;
extern ui_interface_rect_t *ui_g_2_vision_frame;
extern ui_interface_number_t *ui_g_2_vision_order_id;
extern ui_interface_round_t *ui_g_2_current_shoot_mode;

void ui_init_g_2();
void ui_update_g_2();
void ui_remove_g_2();

extern ui_interface_rect_t *ui_g_3_supercap;
extern ui_interface_string_t *ui_g_3_low_leg_length;
extern ui_interface_string_t *ui_g_3_shoot_mode_aim;
extern ui_interface_string_t *ui_g_3_shoot_mode_energy;
extern ui_interface_string_t *ui_g_3_mid_leg_length;
extern ui_interface_string_t *ui_g_3_high_leg_length;

void ui_init_g_3();
void ui_update_g_3();
void ui_remove_g_3();

extern ui_interface_string_t *ui_g_4_fly_flag;

void ui_init_g_4();
void ui_update_g_4();
void ui_remove_g_4();


#endif // UI_g_H
