#include "fsm_core.h"

void fsm_init(FsmMachine_t* fsm, const FsmState_t* initial_state) {
    if (!fsm || !initial_state) return;
    
    fsm->curr_state = initial_state;
    fsm->next_state = NULL;
    fsm->state_change_pending = 0;

    // 初始化时立即执行一次 Enter，建立初始环境
    if (fsm->curr_state->enter) {
        fsm->curr_state->enter();
    }
}

void fsm_change(FsmMachine_t* fsm, const FsmState_t* target_state) {
    if (!fsm || !target_state) return;
    
    // 如果已经在该状态，或已经有挂起的切换，则忽略
    if (fsm->curr_state == target_state) return;
    
    fsm->next_state = target_state;
    fsm->state_change_pending = 1;
}

void fsm_run(FsmMachine_t* fsm) {
    if (!fsm) return;

    // 1. 处理状态切换 (Exit Old -> Change Ptr -> Enter New)
    if (fsm->state_change_pending && fsm->next_state) {
        // A. 执行旧状态 Exit
        if (fsm->curr_state && fsm->curr_state->exit) {
            fsm->curr_state->exit();
        }

        // B. 切换指针
        fsm->curr_state = fsm->next_state;
        fsm->next_state = NULL;
        fsm->state_change_pending = 0;

        // C. 执行新状态 Enter
        if (fsm->curr_state && fsm->curr_state->enter) {
            fsm->curr_state->enter();
        }
    }

    // 2. 执行当前状态逻辑
    if (fsm->curr_state && fsm->curr_state->execute) {
        fsm->curr_state->execute();
    }
}