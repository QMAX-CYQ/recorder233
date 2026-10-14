//
// Created by RM UI Designer
// Static Edition
//

#include <string.h>

#include "ui_interface.h"

ui_7_frame_t ui_g_1_0;

ui_interface_line_t *ui_g_1_left_big_leg = (ui_interface_line_t*)&(ui_g_1_0.data[0]);
ui_interface_line_t *ui_g_1_left_small_leg = (ui_interface_line_t*)&(ui_g_1_0.data[1]);
ui_interface_line_t *ui_g_1_right_big_leg = (ui_interface_line_t*)&(ui_g_1_0.data[2]);
ui_interface_round_t *ui_g_1_current_high_flag = (ui_interface_round_t*)&(ui_g_1_0.data[3]);
ui_interface_line_t *ui_g_1_right_small_leg = (ui_interface_line_t*)&(ui_g_1_0.data[4]);
ui_interface_rect_t *ui_g_1_supercap_capcity = (ui_interface_rect_t*)&(ui_g_1_0.data[5]);
ui_interface_number_t *ui_g_1_supcap_voltage = (ui_interface_number_t*)&(ui_g_1_0.data[6]);

void _ui_init_g_1_0() {
    for (int i = 0; i < 7; i++) {
        ui_g_1_0.data[i].figure_name[0] = 0;
        ui_g_1_0.data[i].figure_name[1] = 0;
        ui_g_1_0.data[i].figure_name[2] = i + 0;
        ui_g_1_0.data[i].operate_type = 1;
    }
    for (int i = 7; i < 7; i++) {
        ui_g_1_0.data[i].operate_type = 0;
    }

    ui_g_1_left_big_leg->figure_type = 0;
    ui_g_1_left_big_leg->operate_type = 1;
    ui_g_1_left_big_leg->layer = 0;
    ui_g_1_left_big_leg->color = 2;
    ui_g_1_left_big_leg->start_x = 652;
    ui_g_1_left_big_leg->start_y = 440;
    ui_g_1_left_big_leg->width = 5;
    ui_g_1_left_big_leg->end_x = 540;
    ui_g_1_left_big_leg->end_y = 440;

    ui_g_1_left_small_leg->figure_type = 0;
    ui_g_1_left_small_leg->operate_type = 1;
    ui_g_1_left_small_leg->layer = 0;
    ui_g_1_left_small_leg->color = 2;
    ui_g_1_left_small_leg->start_x = 468;
    ui_g_1_left_small_leg->start_y = 363;
    ui_g_1_left_small_leg->width = 5;
    ui_g_1_left_small_leg->end_x = 540;
    ui_g_1_left_small_leg->end_y = 440;

    ui_g_1_right_big_leg->figure_type = 0;
    ui_g_1_right_big_leg->operate_type = 1;
    ui_g_1_right_big_leg->layer = 0;
    ui_g_1_right_big_leg->color = 2;
    ui_g_1_right_big_leg->start_x = 1283;
    ui_g_1_right_big_leg->start_y = 440;
    ui_g_1_right_big_leg->width = 5;
    ui_g_1_right_big_leg->end_x = 1392;
    ui_g_1_right_big_leg->end_y = 440;

    ui_g_1_current_high_flag->figure_type = 2;
    ui_g_1_current_high_flag->operate_type = 1;
    ui_g_1_current_high_flag->layer = 0;
    ui_g_1_current_high_flag->color = 3;
    ui_g_1_current_high_flag->start_x = 96;
    ui_g_1_current_high_flag->start_y = 785;
    ui_g_1_current_high_flag->width = 10;
    ui_g_1_current_high_flag->r = 5;

    ui_g_1_right_small_leg->figure_type = 0;
    ui_g_1_right_small_leg->operate_type = 1;
    ui_g_1_right_small_leg->layer = 0;
    ui_g_1_right_small_leg->color = 2;
    ui_g_1_right_small_leg->start_x = 1450;
    ui_g_1_right_small_leg->start_y = 356;
    ui_g_1_right_small_leg->width = 5;
    ui_g_1_right_small_leg->end_x = 1387;
    ui_g_1_right_small_leg->end_y = 443;

    ui_g_1_supercap_capcity->figure_type = 1;
    ui_g_1_supercap_capcity->operate_type = 1;
    ui_g_1_supercap_capcity->layer = 0;
    ui_g_1_supercap_capcity->color = 2;
    ui_g_1_supercap_capcity->start_x = 666;
    ui_g_1_supercap_capcity->start_y = 282;
    ui_g_1_supercap_capcity->width = 11;
    ui_g_1_supercap_capcity->end_x = 1240;
    ui_g_1_supercap_capcity->end_y = 283;

    ui_g_1_supcap_voltage->figure_type = 5;
    ui_g_1_supcap_voltage->operate_type = 1;
    ui_g_1_supcap_voltage->layer = 0;
    ui_g_1_supcap_voltage->color = 6;
    ui_g_1_supcap_voltage->start_x = 911;
    ui_g_1_supcap_voltage->start_y = 249;
    ui_g_1_supcap_voltage->width = 3;
    ui_g_1_supcap_voltage->font_size = 30;
    ui_g_1_supcap_voltage->number = 30100;


    ui_proc_7_frame(&ui_g_1_0);
    SEND_MESSAGE((uint8_t *) &ui_g_1_0, sizeof(ui_g_1_0));
}

void _ui_update_g_1_0() {
    for (int i = 0; i < 7; i++) {
        ui_g_1_0.data[i].operate_type = 2;
    }

    ui_proc_7_frame(&ui_g_1_0);
    SEND_MESSAGE((uint8_t *) &ui_g_1_0, sizeof(ui_g_1_0));
}

void _ui_remove_g_1_0() {
    for (int i = 0; i < 7; i++) {
        ui_g_1_0.data[i].operate_type = 3;
    }

    ui_proc_7_frame(&ui_g_1_0);
    SEND_MESSAGE((uint8_t *) &ui_g_1_0, sizeof(ui_g_1_0));
}


void ui_init_g_1() {
    _ui_init_g_1_0();
}

void ui_update_g_1() {
    _ui_update_g_1_0();
}

void ui_remove_g_1() {
    _ui_remove_g_1_0();
}

ui_7_frame_t ui_g_2_0;

ui_interface_number_t *ui_g_2_current_trigger = (ui_interface_number_t*)&(ui_g_2_0.data[0]);
ui_interface_number_t *ui_g_2_target_trigger = (ui_interface_number_t*)&(ui_g_2_0.data[1]);
ui_interface_arc_t *ui_g_2_head_position = (ui_interface_arc_t*)&(ui_g_2_0.data[2]);
ui_interface_number_t *ui_g_2_vision_trice_id = (ui_interface_number_t*)&(ui_g_2_0.data[3]);
ui_interface_rect_t *ui_g_2_vision_frame = (ui_interface_rect_t*)&(ui_g_2_0.data[4]);
ui_interface_number_t *ui_g_2_vision_order_id = (ui_interface_number_t*)&(ui_g_2_0.data[5]);
ui_interface_round_t *ui_g_2_current_shoot_mode = (ui_interface_round_t*)&(ui_g_2_0.data[6]);

void _ui_init_g_2_0() {
    for (int i = 0; i < 7; i++) {
        ui_g_2_0.data[i].figure_name[0] = 0;
        ui_g_2_0.data[i].figure_name[1] = 1;
        ui_g_2_0.data[i].figure_name[2] = i + 0;
        ui_g_2_0.data[i].operate_type = 1;
    }
    for (int i = 7; i < 7; i++) {
        ui_g_2_0.data[i].operate_type = 0;
    }

    ui_g_2_current_trigger->figure_type = 5;
    ui_g_2_current_trigger->operate_type = 1;
    ui_g_2_current_trigger->layer = 0;
    ui_g_2_current_trigger->color = 6;
    ui_g_2_current_trigger->start_x = 341;
    ui_g_2_current_trigger->start_y = 808;
    ui_g_2_current_trigger->width = 3;
    ui_g_2_current_trigger->font_size = 30;
    ui_g_2_current_trigger->number = 0;

    ui_g_2_target_trigger->figure_type = 5;
    ui_g_2_target_trigger->operate_type = 1;
    ui_g_2_target_trigger->layer = 0;
    ui_g_2_target_trigger->color = 4;
    ui_g_2_target_trigger->start_x = 343;
    ui_g_2_target_trigger->start_y = 708;
    ui_g_2_target_trigger->width = 3;
    ui_g_2_target_trigger->font_size = 30;
    ui_g_2_target_trigger->number = 0;

    ui_g_2_head_position->figure_type = 4;
    ui_g_2_head_position->operate_type = 1;
    ui_g_2_head_position->layer = 0;
    ui_g_2_head_position->color = 2;
    ui_g_2_head_position->start_x = 960;
    ui_g_2_head_position->start_y = 540;
    ui_g_2_head_position->width = 5;
    ui_g_2_head_position->start_angle = 15;
    ui_g_2_head_position->end_angle = 345;
    ui_g_2_head_position->rx = 50;
    ui_g_2_head_position->ry = 50;

    ui_g_2_vision_trice_id->figure_type = 6;
    ui_g_2_vision_trice_id->operate_type = 1;
    ui_g_2_vision_trice_id->layer = 0;
    ui_g_2_vision_trice_id->color = 4;
    ui_g_2_vision_trice_id->start_x = 945;
    ui_g_2_vision_trice_id->start_y = 680;
    ui_g_2_vision_trice_id->width = 3;
    ui_g_2_vision_trice_id->font_size = 30;
    ui_g_2_vision_trice_id->number = 1;

    ui_g_2_vision_frame->figure_type = 1;
    ui_g_2_vision_frame->operate_type = 1;
    ui_g_2_vision_frame->layer = 0;
    ui_g_2_vision_frame->color = 8;
    ui_g_2_vision_frame->start_x = 690;
    ui_g_2_vision_frame->start_y = 380;
    ui_g_2_vision_frame->width = 3;
    ui_g_2_vision_frame->end_x = 1240;
    ui_g_2_vision_frame->end_y = 700;

    ui_g_2_vision_order_id->figure_type = 6;
    ui_g_2_vision_order_id->operate_type = 1;
    ui_g_2_vision_order_id->layer = 0;
    ui_g_2_vision_order_id->color = 3;
    ui_g_2_vision_order_id->start_x = 945;
    ui_g_2_vision_order_id->start_y = 430;
    ui_g_2_vision_order_id->width = 3;
    ui_g_2_vision_order_id->font_size = 30;
    ui_g_2_vision_order_id->number = 1;

    ui_g_2_current_shoot_mode->figure_type = 2;
    ui_g_2_current_shoot_mode->operate_type = 1;
    ui_g_2_current_shoot_mode->layer = 0;
    ui_g_2_current_shoot_mode->color = 3;
    ui_g_2_current_shoot_mode->start_x = 720;
    ui_g_2_current_shoot_mode->start_y = 673;
    ui_g_2_current_shoot_mode->width = 10;
    ui_g_2_current_shoot_mode->r = 5;


    ui_proc_7_frame(&ui_g_2_0);
    SEND_MESSAGE((uint8_t *) &ui_g_2_0, sizeof(ui_g_2_0));
}

void _ui_update_g_2_0() {
    for (int i = 0; i < 7; i++) {
        ui_g_2_0.data[i].operate_type = 2;
    }

    ui_proc_7_frame(&ui_g_2_0);
    SEND_MESSAGE((uint8_t *) &ui_g_2_0, sizeof(ui_g_2_0));
}

void _ui_remove_g_2_0() {
    for (int i = 0; i < 7; i++) {
        ui_g_2_0.data[i].operate_type = 3;
    }

    ui_proc_7_frame(&ui_g_2_0);
    SEND_MESSAGE((uint8_t *) &ui_g_2_0, sizeof(ui_g_2_0));
}


void ui_init_g_2() {
    _ui_init_g_2_0();
}

void ui_update_g_2() {
    _ui_update_g_2_0();
}

void ui_remove_g_2() {
    _ui_remove_g_2_0();
}

ui_1_frame_t ui_g_3_0;

ui_interface_rect_t *ui_g_3_supercap = (ui_interface_rect_t*)&(ui_g_3_0.data[0]);

void _ui_init_g_3_0() {
    for (int i = 0; i < 1; i++) {
        ui_g_3_0.data[i].figure_name[0] = 0;
        ui_g_3_0.data[i].figure_name[1] = 2;
        ui_g_3_0.data[i].figure_name[2] = i + 0;
        ui_g_3_0.data[i].operate_type = 1;
    }
    for (int i = 1; i < 1; i++) {
        ui_g_3_0.data[i].operate_type = 0;
    }

    ui_g_3_supercap->figure_type = 1;
    ui_g_3_supercap->operate_type = 1;
    ui_g_3_supercap->layer = 0;
    ui_g_3_supercap->color = 0;
    ui_g_3_supercap->start_x = 658;
    ui_g_3_supercap->start_y = 273;
    ui_g_3_supercap->width = 5;
    ui_g_3_supercap->end_x = 1248;
    ui_g_3_supercap->end_y = 293;


    ui_proc_1_frame(&ui_g_3_0);
    SEND_MESSAGE((uint8_t *) &ui_g_3_0, sizeof(ui_g_3_0));
}

void _ui_update_g_3_0() {
    for (int i = 0; i < 1; i++) {
        ui_g_3_0.data[i].operate_type = 2;
    }

    ui_proc_1_frame(&ui_g_3_0);
    SEND_MESSAGE((uint8_t *) &ui_g_3_0, sizeof(ui_g_3_0));
}

void _ui_remove_g_3_0() {
    for (int i = 0; i < 1; i++) {
        ui_g_3_0.data[i].operate_type = 3;
    }

    ui_proc_1_frame(&ui_g_3_0);
    SEND_MESSAGE((uint8_t *) &ui_g_3_0, sizeof(ui_g_3_0));
}

ui_string_frame_t ui_g_3_1;
ui_interface_string_t* ui_g_3_low_leg_length = &(ui_g_3_1.option);

void _ui_init_g_3_1() {
    ui_g_3_1.option.figure_name[0] = 0;
    ui_g_3_1.option.figure_name[1] = 2;
    ui_g_3_1.option.figure_name[2] = 1;
    ui_g_3_1.option.operate_type = 1;

    ui_g_3_low_leg_length->figure_type = 7;
    ui_g_3_low_leg_length->operate_type = 1;
    ui_g_3_low_leg_length->layer = 0;
    ui_g_3_low_leg_length->color = 1;
    ui_g_3_low_leg_length->start_x = 122;
    ui_g_3_low_leg_length->start_y = 806;
    ui_g_3_low_leg_length->width = 3;
    ui_g_3_low_leg_length->font_size = 30;
    ui_g_3_low_leg_length->str_length = 3;
    strcpy(ui_g_3_low_leg_length->string, "low");


    ui_proc_string_frame(&ui_g_3_1);
    SEND_MESSAGE((uint8_t *) &ui_g_3_1, sizeof(ui_g_3_1));
}

void _ui_update_g_3_1() {
    ui_g_3_1.option.operate_type = 2;

    ui_proc_string_frame(&ui_g_3_1);
    SEND_MESSAGE((uint8_t *) &ui_g_3_1, sizeof(ui_g_3_1));
}

void _ui_remove_g_3_1() {
    ui_g_3_1.option.operate_type = 3;

    ui_proc_string_frame(&ui_g_3_1);
    SEND_MESSAGE((uint8_t *) &ui_g_3_1, sizeof(ui_g_3_1));
}
ui_string_frame_t ui_g_3_2;
ui_interface_string_t* ui_g_3_shoot_mode_aim = &(ui_g_3_2.option);

void _ui_init_g_3_2() {
    ui_g_3_2.option.figure_name[0] = 0;
    ui_g_3_2.option.figure_name[1] = 2;
    ui_g_3_2.option.figure_name[2] = 2;
    ui_g_3_2.option.operate_type = 1;

    ui_g_3_shoot_mode_aim->figure_type = 7;
    ui_g_3_shoot_mode_aim->operate_type = 1;
    ui_g_3_shoot_mode_aim->layer = 0;
    ui_g_3_shoot_mode_aim->color = 8;
    ui_g_3_shoot_mode_aim->start_x = 740;
    ui_g_3_shoot_mode_aim->start_y = 685;
    ui_g_3_shoot_mode_aim->width = 2;
    ui_g_3_shoot_mode_aim->font_size = 15;
    ui_g_3_shoot_mode_aim->str_length = 3;
    strcpy(ui_g_3_shoot_mode_aim->string, "aim");


    ui_proc_string_frame(&ui_g_3_2);
    SEND_MESSAGE((uint8_t *) &ui_g_3_2, sizeof(ui_g_3_2));
}

void _ui_update_g_3_2() {
    ui_g_3_2.option.operate_type = 2;

    ui_proc_string_frame(&ui_g_3_2);
    SEND_MESSAGE((uint8_t *) &ui_g_3_2, sizeof(ui_g_3_2));
}

void _ui_remove_g_3_2() {
    ui_g_3_2.option.operate_type = 3;

    ui_proc_string_frame(&ui_g_3_2);
    SEND_MESSAGE((uint8_t *) &ui_g_3_2, sizeof(ui_g_3_2));
}
ui_string_frame_t ui_g_3_3;
ui_interface_string_t* ui_g_3_shoot_mode_energy = &(ui_g_3_3.option);

void _ui_init_g_3_3() {
    ui_g_3_3.option.figure_name[0] = 0;
    ui_g_3_3.option.figure_name[1] = 2;
    ui_g_3_3.option.figure_name[2] = 3;
    ui_g_3_3.option.operate_type = 1;

    ui_g_3_shoot_mode_energy->figure_type = 7;
    ui_g_3_shoot_mode_energy->operate_type = 1;
    ui_g_3_shoot_mode_energy->layer = 0;
    ui_g_3_shoot_mode_energy->color = 8;
    ui_g_3_shoot_mode_energy->start_x = 740;
    ui_g_3_shoot_mode_energy->start_y = 650;
    ui_g_3_shoot_mode_energy->width = 2;
    ui_g_3_shoot_mode_energy->font_size = 15;
    ui_g_3_shoot_mode_energy->str_length = 6;
    strcpy(ui_g_3_shoot_mode_energy->string, "energy");


    ui_proc_string_frame(&ui_g_3_3);
    SEND_MESSAGE((uint8_t *) &ui_g_3_3, sizeof(ui_g_3_3));
}

void _ui_update_g_3_3() {
    ui_g_3_3.option.operate_type = 2;

    ui_proc_string_frame(&ui_g_3_3);
    SEND_MESSAGE((uint8_t *) &ui_g_3_3, sizeof(ui_g_3_3));
}

void _ui_remove_g_3_3() {
    ui_g_3_3.option.operate_type = 3;

    ui_proc_string_frame(&ui_g_3_3);
    SEND_MESSAGE((uint8_t *) &ui_g_3_3, sizeof(ui_g_3_3));
}
ui_string_frame_t ui_g_3_4;
ui_interface_string_t* ui_g_3_mid_leg_length = &(ui_g_3_4.option);

void _ui_init_g_3_4() {
    ui_g_3_4.option.figure_name[0] = 0;
    ui_g_3_4.option.figure_name[1] = 2;
    ui_g_3_4.option.figure_name[2] = 4;
    ui_g_3_4.option.operate_type = 1;

    ui_g_3_mid_leg_length->figure_type = 7;
    ui_g_3_mid_leg_length->operate_type = 1;
    ui_g_3_mid_leg_length->layer = 0;
    ui_g_3_mid_leg_length->color = 1;
    ui_g_3_mid_leg_length->start_x = 122;
    ui_g_3_mid_leg_length->start_y = 756;
    ui_g_3_mid_leg_length->width = 3;
    ui_g_3_mid_leg_length->font_size = 30;
    ui_g_3_mid_leg_length->str_length = 3;
    strcpy(ui_g_3_mid_leg_length->string, "mid");


    ui_proc_string_frame(&ui_g_3_4);
    SEND_MESSAGE((uint8_t *) &ui_g_3_4, sizeof(ui_g_3_4));
}

void _ui_update_g_3_4() {
    ui_g_3_4.option.operate_type = 2;

    ui_proc_string_frame(&ui_g_3_4);
    SEND_MESSAGE((uint8_t *) &ui_g_3_4, sizeof(ui_g_3_4));
}

void _ui_remove_g_3_4() {
    ui_g_3_4.option.operate_type = 3;

    ui_proc_string_frame(&ui_g_3_4);
    SEND_MESSAGE((uint8_t *) &ui_g_3_4, sizeof(ui_g_3_4));
}
ui_string_frame_t ui_g_3_5;
ui_interface_string_t* ui_g_3_high_leg_length = &(ui_g_3_5.option);

void _ui_init_g_3_5() {
    ui_g_3_5.option.figure_name[0] = 0;
    ui_g_3_5.option.figure_name[1] = 2;
    ui_g_3_5.option.figure_name[2] = 5;
    ui_g_3_5.option.operate_type = 1;

    ui_g_3_high_leg_length->figure_type = 7;
    ui_g_3_high_leg_length->operate_type = 1;
    ui_g_3_high_leg_length->layer = 0;
    ui_g_3_high_leg_length->color = 1;
    ui_g_3_high_leg_length->start_x = 122;
    ui_g_3_high_leg_length->start_y = 706;
    ui_g_3_high_leg_length->width = 3;
    ui_g_3_high_leg_length->font_size = 30;
    ui_g_3_high_leg_length->str_length = 4;
    strcpy(ui_g_3_high_leg_length->string, "high");


    ui_proc_string_frame(&ui_g_3_5);
    SEND_MESSAGE((uint8_t *) &ui_g_3_5, sizeof(ui_g_3_5));
}

void _ui_update_g_3_5() {
    ui_g_3_5.option.operate_type = 2;

    ui_proc_string_frame(&ui_g_3_5);
    SEND_MESSAGE((uint8_t *) &ui_g_3_5, sizeof(ui_g_3_5));
}

void _ui_remove_g_3_5() {
    ui_g_3_5.option.operate_type = 3;

    ui_proc_string_frame(&ui_g_3_5);
    SEND_MESSAGE((uint8_t *) &ui_g_3_5, sizeof(ui_g_3_5));
}

void ui_init_g_3() {
    _ui_init_g_3_0();
    _ui_init_g_3_1();
    _ui_init_g_3_2();
    _ui_init_g_3_3();
    _ui_init_g_3_4();
    _ui_init_g_3_5();
}

void ui_update_g_3() {
    _ui_update_g_3_0();
    _ui_update_g_3_1();
    _ui_update_g_3_2();
    _ui_update_g_3_3();
    _ui_update_g_3_4();
    _ui_update_g_3_5();
}

void ui_remove_g_3() {
    _ui_remove_g_3_0();
    _ui_remove_g_3_1();
    _ui_remove_g_3_2();
    _ui_remove_g_3_3();
    _ui_remove_g_3_4();
    _ui_remove_g_3_5();
}


ui_string_frame_t ui_g_4_0;
ui_interface_string_t* ui_g_4_fly_flag = &(ui_g_4_0.option);

void _ui_init_g_4_0() {
    ui_g_4_0.option.figure_name[0] = 0;
    ui_g_4_0.option.figure_name[1] = 3;
    ui_g_4_0.option.figure_name[2] = 0;
    ui_g_4_0.option.operate_type = 1;

    ui_g_4_fly_flag->figure_type = 7;
    ui_g_4_fly_flag->operate_type = 1;
    ui_g_4_fly_flag->layer = 0;
    ui_g_4_fly_flag->color = 6;
    ui_g_4_fly_flag->start_x = 888;
    ui_g_4_fly_flag->start_y = 357;
    ui_g_4_fly_flag->width = 2;
    ui_g_4_fly_flag->font_size = 20;
    ui_g_4_fly_flag->str_length = 8;
    strcpy(ui_g_4_fly_flag->string, "        ");


    ui_proc_string_frame(&ui_g_4_0);
    SEND_MESSAGE((uint8_t *) &ui_g_4_0, sizeof(ui_g_4_0));
}

void _ui_update_g_4_0() {
    ui_g_4_0.option.operate_type = 2;

    ui_proc_string_frame(&ui_g_4_0);
    SEND_MESSAGE((uint8_t *) &ui_g_4_0, sizeof(ui_g_4_0));
}

void _ui_remove_g_4_0() {
    ui_g_4_0.option.operate_type = 3;

    ui_proc_string_frame(&ui_g_4_0);
    SEND_MESSAGE((uint8_t *) &ui_g_4_0, sizeof(ui_g_4_0));
}

void ui_init_g_4() {
    _ui_init_g_4_0();
}

void ui_update_g_4() {
    _ui_update_g_4_0();
}

void ui_remove_g_4() {
    _ui_remove_g_4_0();
}

