/*
 * traffic_fsm.h
 *
 * Created on: 5 Nov 2025
 * Author: DucTai
 *
 */

#ifndef INC_TRAFFIC_FSM_H_
#define INC_TRAFFIC_FSM_H_

// 1. Các chế độ hệ thống (4 chế độ)
typedef enum {
    MODE_NORMAL = 1,
    MODE_MODIFY_RED = 2,
    MODE_MODIFY_GREEN = 3,
    MODE_MODIFY_YELLOW = 4
} System_Mode_t;

// 2. Các trạng thái đèn giao thông (6 trạng thái)
typedef enum {
    STATE_R1_GREEN_R2_RED,
    STATE_R1_YELLOW_R2_RED,
    STATE_ALL_RED_1,
    STATE_R1_RED_R2_GREEN,
    STATE_R1_RED_R2_YELLOW,
    STATE_ALL_RED_2
} Traffic_State_t;

/**
 * @brief Khởi tạo máy trạng thái đèn giao thông.
 * Đặt lại các giá trị chu kỳ về mặc định và xóa LCD.
 */
void fsm_traffic_init();

/**
 * @brief Hàm chạy máy trạng thái chính.
 * Hàm này nên được gọi trong vòng lặp 50ms (sau khi gọi button_Scan()).
 * Nó sẽ tự động xử lý logic FSM và cập nhật hiển thị.
 */
void fsm_traffic_run();


#endif /* INC_TRAFFIC_FSM_H_ */
