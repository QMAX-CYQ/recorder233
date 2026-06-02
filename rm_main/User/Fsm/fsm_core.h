#ifndef FSM_CORE_H
#define FSM_CORE_H

#include <stdint.h>
#include <stddef.h>

// 前向声明
typedef struct FsmState_t FsmState_t;
typedef struct FsmMachine_t FsmMachine_t;

/**
 * @brief 状态对象接口 (虚函数表)
 */
struct FsmState_t {
    const char* name;                  // 状态名称 (调试用)
    void (*enter)(void);               // 【生命周期】进入状态时执行一次
    void (*execute)(void);             // 【生命周期】状态持续运行时周期性执行
    void (*exit)(void);                // 【生命周期】退出状态时执行一次
};

/**
 * @brief 状态机管理器
 */
struct FsmMachine_t {
    const FsmState_t* curr_state;      // 当前状态
    const FsmState_t* next_state;      // 缓冲的下一状态
    uint8_t state_change_pending;      // 切换挂起标志
};

/**
 * @brief 初始化状态机
 * @param initial_state 初始状态
 */
void fsm_init(FsmMachine_t* fsm, const FsmState_t* initial_state);

/**
 * @brief 请求切换状态
 * @note 真正的切换会在下一次 fsm_run 时发生 (缓冲切换)
 */
void fsm_change(FsmMachine_t* fsm, const FsmState_t* target_state);

/**
 * @brief 状态机运行 tick
 * @note 需要在主循环或任务中周期性调用
 */
void fsm_run(FsmMachine_t* fsm);

#endif // FSM_CORE_H