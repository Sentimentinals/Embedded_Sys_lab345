/*
 * clock_fsm.c
 *
 * Created on: Nov 5, 2025
 * Author: Gemini
 * Implementation of the clock logic FSM.
 * (Phiên bản đầy đủ cho Lab 5: UART Update + Timeout/Retry + Reset)
 *
 * --- ĐÃ SỬA LỖI XUNG ĐỘT (HAL_BUSY VÀ NÚT NHẤN RESET) ---
 */

#include "clock_fsm.h"
#include "software_timer.h"
#include "button.h"
#include "lcd.h"
#include "ds3231.h"
#include <stdio.h>
#include "uart.h"
#include "stdlib.h"
#include "string.h"

/* Private typedef -----------------------------------------------------------*/
// System Modes
typedef enum {
    MODE_VIEW_TIME,
    MODE_SET_TIME,
    MODE_SET_ALARM,
    MODE_UPDATE_VIA_UART,
    MODE_MESSAGE_DISPLAY // Chế độ hiển thị thông báo (Lỗi hoặc Thành công)
} ClockMode_t;

// Parameters for SET_TIME mode
typedef enum {
    SET_HOUR,
    SET_MIN,
    SET_SEC,
    SET_DAY,
    SET_DATE,
    SET_MONTH,
    SET_YEAR
} SetTimeParam_t;

// Parameters for SET_ALARM mode
typedef enum {
    SET_ALARM_HOUR,
    SET_ALARM_MIN,
    SET_ALARM_ENABLE
} SetAlarmParam_t;

/* Private define ------------------------------------------------------------*/
// Button mapping
#define BTN_MODE_SWITCH 0 // SW0
#define BTN_UP 3          // SW3 ("Up")
#define BTN_DOWN 7        // SW7 ("Down")
#define BTN_SAVE_NEXT 12   // SW6 ("E")

// Button press duration
#define LONG_PRESS_DURATION 40  // 2000ms
#define AUTO_INCREMENT_PERIOD 4 // 200ms

// Hằng số cho Timeout/Retry/Reset
#define UART_TIMEOUT_PERIOD 200 // 10 giây (200 * 50ms tick)
#define UART_MAX_RETRIES 3      // 1 lần gửi + 2 lần thử lại
#define MESSAGE_DISPLAY_PERIOD 60 // 3 giây (60 * 50ms tick)
#define RESET_LONG_PRESS_DURATION 60 // 3 giây (60 * 50ms tick)


/* Private variables ---------------------------------------------------------*/
// FSM state variables
static ClockMode_t current_mode = MODE_VIEW_TIME;
static SetTimeParam_t set_time_param = SET_HOUR;
static SetAlarmParam_t set_alarm_param = SET_ALARM_HOUR;

// Temp variables for time setting
static uint8_t temp_hour, temp_min, temp_sec, temp_day, temp_date, temp_month, temp_year;

// Alarm variables (đây là giá trị khởi tạo)
static uint8_t alarm_hour = 6;
static uint8_t alarm_min = 0;
static uint8_t alarm_enabled = 0;
static uint8_t alarm_triggered = 0;
static uint16_t alarm_display_counter = 0;

// 2Hz blink logic
static uint16_t blink_counter = 0;
static uint8_t blink_flag = 0;

// Day names lookup table
const char* day_names[8] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// UART Update FSM variables
static SetTimeParam_t uart_update_param = SET_HOUR;
static uint8_t uart_data_requested = 0;
#define UART_RX_BUFFER_SIZE 32
static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];

// Biến cho UART Timeout/Retry và Message Display
static uint16_t uart_timeout_counter = 0;
static uint8_t uart_retry_count = 0;
static uint16_t message_display_counter = 0;
static uint8_t message_buffer[30];
static uint16_t message_color = GREEN; // Biến lưu màu cho thông báo

// Biến cho logic nút MODE (SỬA LỖI RESET)
static uint16_t mode_btn_last_count = 0;


/* Private function prototypes -----------------------------------------------*/
static void displayTime(void);
static void handle_mode_switch(void);
static void handle_view_time_mode(void);
static void handle_set_time_mode(void);
static void handle_set_alarm_mode(void);
static void handle_uart_update_mode(void);
static void handle_message_display_mode(void);
static void display_mode_status(void);
static uint8_t get_max_date(uint8_t month, uint8_t year);
static void increment_setting(void);
static void decrement_setting(void);
static void increment_alarm_setting(void);
static void decrement_alarm_setting(void);
static void enter_message_display_mode(const char* lcd_msg, uint16_t color, const char* uart_msg);


/* Function Implementation ---------------------------------------------------*/

/**
 * @brief Chuyển FSM sang trạng thái hiển thị thông báo
 */
static void enter_message_display_mode(const char* lcd_msg, uint16_t color, const char* uart_msg) {
    lcd_Fill(0, 100, 240, 220, BLACK);
    sprintf((char*)message_buffer, "%s", lcd_msg);
    message_color = color; // Lưu màu
    lcd_ShowStr(20, 170, message_buffer, message_color, BLACK, 16, 0);

    if (uart_msg != NULL) {
        uart_Rs232SendString((uint8_t*)uart_msg);
    }

    message_display_counter = MESSAGE_DISPLAY_PERIOD; // Bắt đầu đếm 3 giây
    current_mode = MODE_MESSAGE_DISPLAY; // Chuyển sang mode hiển thị
}

/**
 * @brief Handle logic for MESSAGE_DISPLAY mode
 */
static void handle_message_display_mode(void) {
    // Hiển thị lại thông báo (để chống bị ghi đè)
    lcd_ShowStr(20, 170, message_buffer, message_color, BLACK, 16, 0);

    if (message_display_counter > 0) {
        message_display_counter--;
    } else {
        current_mode = MODE_VIEW_TIME;
        // Xóa màn hình khi thoát
        lcd_Fill(0, 100, 240, 220, BLACK);
    }
}


static uint8_t get_max_date(uint8_t month, uint8_t year) {
    if (month == 2) {
        if (year % 4 == 0) {
            return 29;
        } else {
            return 28;
        }
    } else if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    } else {
        return 31;
    }
}

static void increment_setting(void) {
    uint8_t max_date;
    switch(set_time_param) {
        case SET_HOUR: temp_hour = (temp_hour + 1) % 24; break;
        case SET_MIN:  temp_min = (temp_min + 1) % 60; break;
        case SET_SEC:  temp_sec = (temp_sec + 1) % 60; break;
        case SET_DAY:  temp_day = (temp_day % 7) + 1; break;
        case SET_DATE:
            max_date = get_max_date(temp_month, temp_year);
            temp_date = (temp_date % max_date) + 1;
            break;
        case SET_MONTH: temp_month = (temp_month % 12) + 1; break;
        case SET_YEAR:  temp_year = (temp_year + 1) % 100; break;
    }
}

static void decrement_setting(void) {
    uint8_t max_date;
    switch(set_time_param) {
        case SET_HOUR: temp_hour = (temp_hour + 24 - 1) % 24; break;
        case SET_MIN:  temp_min = (temp_min + 60 - 1) % 60; break;
        case SET_SEC:  temp_sec = (temp_sec + 60 - 1) % 60; break;
        case SET_DAY:
            temp_day = (temp_day - 1);
            if (temp_day < 1) temp_day = 7;
            break;
        case SET_DATE:
            max_date = get_max_date(temp_month, temp_year);
            temp_date = (temp_date - 1);
            if (temp_date < 1) temp_date = max_date;
            break;
        case SET_MONTH:
            temp_month = (temp_month - 1);
            if (temp_month < 1) temp_month = 12;
            break;
        case SET_YEAR:  temp_year = (temp_year + 100 - 1) % 100; break;
    }
}

static void increment_alarm_setting(void) {
    switch(set_alarm_param) {
        case SET_ALARM_HOUR: alarm_hour = (alarm_hour + 1) % 24; break;
        case SET_ALARM_MIN:  alarm_min = (alarm_min + 1) % 60; break;
        case SET_ALARM_ENABLE: alarm_enabled = !alarm_enabled; break;
    }
}

static void decrement_alarm_setting(void) {
    switch(set_alarm_param) {
        case SET_ALARM_HOUR: alarm_hour = (alarm_hour + 24 - 1) % 24; break;
        case SET_ALARM_MIN:  alarm_min = (alarm_min + 60 - 1) % 60; break;
        case SET_ALARM_ENABLE: alarm_enabled = !alarm_enabled; break;
    }
}


/**
 * @brief Handle the mode switch button press
 * (ĐÃ SỬA: Bỏ kiểm tra nút nhấn, chỉ thực hiện logic chuyển)
 */
static void handle_mode_switch(void) {

    // If exiting SET_TIME mode, save changes to RTC
    if (current_mode == MODE_SET_TIME) {
        ds3231_Write(ADDRESS_HOUR, temp_hour);
        ds3231_Write(ADDRESS_MIN, temp_min);
        ds3231_Write(ADDRESS_SEC, temp_sec);
        ds3231_Write(ADDRESS_DAY, temp_day);
        ds3231_Write(ADDRESS_DATE, temp_date);
        ds3231_Write(ADDRESS_MONTH, temp_month);
        ds3231_Write(ADDRESS_YEAR, temp_year);
    }

    // If alarm is ringing, stop it
    if (alarm_triggered) {
        alarm_triggered = 0;
    }

    // Cycle mode (0 -> 1 -> 2 -> 3 -> 0)
    current_mode = (current_mode + 1) % 4; // %4 để bỏ qua MODE_MESSAGE_DISPLAY

    // Init SET_TIME mode
    if (current_mode == MODE_SET_TIME) {
        ds3231_ReadTime();
        temp_hour = ds3231_hours;
        temp_min = ds3231_min;
        temp_sec = ds3231_sec;
        temp_day = ds3231_day;
        temp_date = ds3231_date;
        temp_month = ds3231_month;
        temp_year = ds3231_year;
        set_time_param = SET_HOUR;
    }
    // Init SET_ALARM mode
    else if (current_mode == MODE_SET_ALARM) {
        set_alarm_param = SET_ALARM_HOUR;
    }
    // Init UART UPDATE mode
    else if (current_mode == MODE_UPDATE_VIA_UART) {
        ds3231_ReadTime();
        temp_hour = ds3231_hours; temp_min = ds3231_min; temp_sec = ds3231_sec;
        temp_day = ds3231_day; temp_date = ds3231_date; temp_month = ds3231_month;
        temp_year = ds3231_year;

        uart_update_param = SET_HOUR;
        uart_data_requested = 0;
        uart_retry_count = 0; // Reset số lần thử
        uart_timeout_counter = UART_TIMEOUT_PERIOD; // Đặt 10 giây

        lcd_Fill(0, 100, 240, 220, BLACK);
        uart_Rs232SendString((uint8_t*)"\r\n--- ENTERING UART UPDATE MODE ---\r\n");
    }
    // Clear setting area when returning to VIEW_TIME
    else if (current_mode == MODE_VIEW_TIME) {
         lcd_Fill(0, 160, 240, 220, BLACK);
    }
}


static void handle_view_time_mode(void) {
    // Stop alarm
    if (alarm_triggered && (button_count[BTN_UP] == 1 || button_count[BTN_DOWN] == 1 || button_count[BTN_SAVE_NEXT] == 1)) {
        alarm_triggered = 0;
        lcd_Fill(60, 160, 180, 200, BLACK);
    }

    if (alarm_triggered) {
        // Handle alarm timeout (10s)
        if (alarm_display_counter > 0) {
            alarm_display_counter--;
        } else {
            alarm_triggered = 0;
            lcd_Fill(60, 170, 180, 200, BLACK);
        }

        // Alarm visual effect
        if (blink_flag) {
            lcd_Fill(60, 170, 180, 200, RED);
            lcd_ShowStr(70, 175, (uint8_t*)"ALARM!", BLACK, RED, 24, 0);
        } else {
            lcd_Fill(60, 170, 180, 200, BLACK);
        }
        displayTime();

    } else {
        // Read time from RTC
        ds3231_ReadTime();

        // Check for alarm trigger
        if (alarm_enabled &&
            !alarm_triggered &&
            ds3231_hours == alarm_hour &&
            ds3231_min == alarm_min &&
            ds3231_sec == 0)
        {
            alarm_triggered = 1;
            alarm_display_counter = 200; // 10s
        }

        // Normal time display
        displayTime();
    }
}


static void handle_set_time_mode(void) {
    // Handle UP button
    if (button_count[BTN_UP] == 1) {
        increment_setting();
    } else if (button_count[BTN_UP] > LONG_PRESS_DURATION) {
        if (button_count[BTN_UP] % AUTO_INCREMENT_PERIOD == 0) {
            increment_setting();
        }
    }

    // Handle DOWN button
    if (button_count[BTN_DOWN] == 1) {
        decrement_setting();
    } else if (button_count[BTN_DOWN] > LONG_PRESS_DURATION) {
        if (button_count[BTN_DOWN] % AUTO_INCREMENT_PERIOD == 0) {
            decrement_setting();
        }
    }

    // Handle SAVE/NEXT button
    if (button_count[BTN_SAVE_NEXT] == 1) {
        set_time_param = (set_time_param + 1);
        if (set_time_param > SET_YEAR) {
            set_time_param = SET_HOUR;
        }
    }

    // --- Display temp values with blinking effect ---
    char str_buff[5];

    // Hour
    if (set_time_param == SET_HOUR && !blink_flag) {
        lcd_ShowStr(70, 100, (uint8_t*)"  ", GREEN, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(70, 100, temp_hour, 2, GREEN, BLACK, 24);
    }
    lcd_ShowStr(100, 100, (uint8_t*)":", GREEN, BLACK, 24, 0);

    // Minute
    if (set_time_param == SET_MIN && !blink_flag) {
        lcd_ShowStr(110, 100, (uint8_t*)"  ", GREEN, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(110, 100, temp_min, 2, GREEN, BLACK, 24);
    }
    lcd_ShowStr(140, 100, (uint8_t*)":", GREEN, BLACK, 24, 0);

    // Second
    if (set_time_param == SET_SEC && !blink_flag) {
        lcd_ShowStr(150, 100, (uint8_t*)"  ", GREEN, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(150, 100, temp_sec, 2, GREEN, BLACK, 24);
    }

    // Day
    if (set_time_param == SET_DAY && !blink_flag) {
        lcd_ShowStr(20, 130, (uint8_t*)"   ", YELLOW, BLACK, 24, 0);
    } else {
        sprintf(str_buff, "%s", day_names[temp_day]);
        lcd_ShowStr(20, 130, (uint8_t*)str_buff, YELLOW, BLACK, 24, 0);
    }

    // Date
    if (set_time_param == SET_DATE && !blink_flag) {
        lcd_ShowStr(70, 130, (uint8_t*)"  ", YELLOW, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(70, 130, temp_date, 2, YELLOW, BLACK, 24);
    }
    lcd_ShowStr(100, 130, (uint8_t*)"/", YELLOW, BLACK, 24, 0);

    // Month
    if (set_time_param == SET_MONTH && !blink_flag) {
        lcd_ShowStr(110, 130, (uint8_t*)"  ", YELLOW, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(110, 130, temp_month, 2, YELLOW, BLACK, 24);
    }
    lcd_ShowStr(140, 130, (uint8_t*)"/", YELLOW, BLACK, 24, 0);

    // Year
    if (set_time_param == SET_YEAR && !blink_flag) {
        lcd_ShowStr(150, 130, (uint8_t*)"  ", YELLOW, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(150, 130, temp_year, 2, YELLOW, BLACK, 24);
    }

    lcd_Fill(0, 160, 240, 220, BLACK);
}


static void handle_set_alarm_mode(void) {
    ds3231_ReadTime();
    displayTime();

    // Handle UP button
    if (button_count[BTN_UP] == 1) {
        increment_alarm_setting();
    } else if (button_count[BTN_UP] > LONG_PRESS_DURATION) {
        if (button_count[BTN_UP] % AUTO_INCREMENT_PERIOD == 0) {
            increment_alarm_setting();
        }
    }

    // Handle DOWN button
    if (button_count[BTN_DOWN] == 1) {
        decrement_alarm_setting();
    } else if (button_count[BTN_DOWN] > LONG_PRESS_DURATION) {
        if (button_count[BTN_DOWN] % AUTO_INCREMENT_PERIOD == 0) {
            decrement_alarm_setting();
        }
    }

    // Handle SAVE/NEXT button
    if (button_count[BTN_SAVE_NEXT] == 1) {
        set_alarm_param = (set_alarm_param + 1) % 3;
    }

    // Display alarm settings
    lcd_ShowStr(20, 170, (uint8_t*)"ALARM:", CYAN, BLACK, 24, 0);

    // Alarm Hour (XX)
    if (set_alarm_param == SET_ALARM_HOUR && !blink_flag) {
        lcd_ShowStr(110, 170, (uint8_t*)"  ", CYAN, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(110, 170, alarm_hour, 2, CYAN, BLACK, 24);
    }

    lcd_ShowStr(140, 170, (uint8_t*)":", CYAN, BLACK, 24, 0);

    // Alarm Minute (YY)
    if (set_alarm_param == SET_ALARM_MIN && !blink_flag) {
        lcd_ShowStr(150, 170, (uint8_t*)"  ", CYAN, BLACK, 24, 0);
    } else {
        lcd_ShowIntNum(150, 170, alarm_min, 2, CYAN, BLACK, 24);
    }

    // Alarm Status (ON/OFF)
    if (set_alarm_param == SET_ALARM_ENABLE && !blink_flag) {
        lcd_ShowStr(20, 200, (uint8_t*)"   ", CYAN, BLACK, 24, 0);
    } else {
        if (alarm_enabled) {
            lcd_ShowStr(20, 200, (uint8_t*)"ON ", CYAN, BLACK, 24, 0);
        } else {
            lcd_ShowStr(20, 200, (uint8_t*)"OFF", CYAN, BLACK, 24, 0);
        }
    }
}


/**
 * @brief Handle logic for UART UPDATE mode (ĐÃ SỬA LỖI HAL_BUSY)
 */
static void handle_uart_update_mode(void) {
    char str_buff[50];

    // 1. Gửi yêu cầu (request) nếu chưa (uart_data_requested == 0)
    if (uart_data_requested == 0) {

        // KIỂM TRA SỐ LẦN THỬ LẠI
        if (uart_retry_count >= UART_MAX_RETRIES) {
            // ĐÃ THỬ QUÁ 3 LẦN -> LỖI TIMEOUT
            enter_message_display_mode("UART Timeout!", RED, "\r\nERROR: No response after 3 tries. Exiting.\r\n");
            return; // Thoát khỏi hàm này
        }

        // Nếu chưa quá 3 lần, gửi request
        uart_retry_count++; // Tăng số lần thử
        uart_timeout_counter = UART_TIMEOUT_PERIOD; // Đặt lại 10 giây

        lcd_Fill(0, 170, 240, 200, BLACK);
        switch(uart_update_param) {
            case SET_HOUR:
                sprintf(str_buff, "Updating hours... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Hours (0-23): ");
                break;
            case SET_MIN:
                sprintf(str_buff, "Updating minutes... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Minutes (0-59): ");
                break;
            case SET_SEC:
                sprintf(str_buff, "Updating seconds... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Seconds (0-59): ");
                break;
            case SET_DAY:
                sprintf(str_buff, "Updating day... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Enter 3 letters (e.g. Mon, Tue, Wed):\r\n");
                uart_Rs232SendString((uint8_t*)"Day: ");
                break;
            case SET_DATE:
                sprintf(str_buff, "Updating date... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Date (1-31): ");
                break;
            case SET_MONTH:
                sprintf(str_buff, "Updating month... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Month (1-12): ");
                break;
            case SET_YEAR:
                sprintf(str_buff, "Updating year... (Try %d/3)", uart_retry_count);
                uart_Rs232SendString((uint8_t*)"Year (0-99): ");
                break;
        }
        lcd_ShowStr(20, 170, (uint8_t*)str_buff, MAGENTA, BLACK, 16, 0);

        // --- SỬA LỖI HAL_BUSY ---
        // Chỉ bật ngắt nhận (chế độ nghe) SAU KHI đã gửi xong request
        uart_init_rs232();

        uart_data_requested = 1; // Đánh dấu là "Đã gửi, đang chờ"
    }

    // 2. Kiểm tra xem có lệnh nào từ UART không
    if (uart_get_command(uart_rx_buffer)) {

        // --- SỬA LỖI HAL_BUSY ---
        // Dừng chế độ nghe ngay khi có lệnh, để chuẩn bị gửi request tiếp theo
        uart_stop_listening();

        uint8_t data_is_valid = 0; // Cờ kiểm tra dữ liệu hợp lệ

        // 3. Xử lý giá trị nhận được
        if (uart_update_param == SET_DAY) {
            // Xử lý NGÀY (nhận chuỗi)
            for (int i = 1; i <= 7; i++) {
                if (strcmp((char*)uart_rx_buffer, day_names[i]) == 0) {
                    temp_day = (uint8_t)i;
                    data_is_valid = 1;
                    break;
                }
            }
        } else {
            // Xử lý các giá trị khác (nhận SỐ)
            int32_t val = uart_parse_num_from_string(uart_rx_buffer);

            switch(uart_update_param) {
                case SET_HOUR:  if (val >= 0 && val <= 23) { temp_hour = (uint8_t)val; data_is_valid = 1; } break;
                case SET_MIN:   if (val >= 0 && val <= 59) { temp_min = (uint8_t)val;  data_is_valid = 1; } break;
                case SET_SEC:   if (val >= 0 && val <= 59) { temp_sec = (uint8_t)val;  data_is_valid = 1; } break;
                case SET_DAY:   /* Đã xử lý ở trên */ break;
                case SET_DATE:  if (val >= 1 && val <= 31) { temp_date = (uint8_t)val; data_is_valid = 1; } break; // (Chưa check ngày/tháng)
                case SET_MONTH: if (val >= 1 && val <= 12) { temp_month = (uint8_t)val; data_is_valid = 1; } break;
                case SET_YEAR:  if (val >= 0 && val <= 99) { temp_year = (uint8_t)val;  data_is_valid = 1; } break;
            }
        }

        // 4. Phản hồi dựa trên tính hợp lệ của dữ liệu
        if (data_is_valid) {
            // --- DỮ LIỆU HỢP LỆ ---
            uart_Rs232SendString((uint8_t*)"Received: ");
            uart_Rs232SendString(uart_rx_buffer);
            uart_Rs232SendString((uint8_t*)"\r\n");

            // Chuyển sang tham số tiếp theo
            uart_update_param++;
            uart_data_requested = 0; // Sẵn sàng cho yêu cầu MỚI
            uart_retry_count = 0;    // RESET số lần thử
            // (timeout sẽ được đặt lại ở đầu vòng lặp tiếp theo)

            // 5. Kiểm tra nếu đã hoàn tất
            if (uart_update_param > SET_YEAR) {
                // Ghi tất cả giá trị tạm vào DS3231
                ds3231_Write(ADDRESS_HOUR, temp_hour);
                ds3231_Write(ADDRESS_MIN, temp_min);
                ds3231_Write(ADDRESS_SEC, temp_sec);
                ds3231_Write(ADDRESS_DAY, temp_day);
                ds3231_Write(ADDRESS_DATE, temp_date);
                ds3231_Write(ADDRESS_MONTH, temp_month);
                ds3231_Write(ADDRESS_YEAR, temp_year);

                // Dùng mode MESSAGE để hiển thị "Complete" trong 3s rồi thoát
                enter_message_display_mode("Update Complete!", GREEN, "\r\nUpdate Complete! Returning...\r\n");
            }

        } else {
            // --- DỮ LIỆU KHÔNG HỢP LỆ ---
            uart_Rs232SendString((uint8_t*)"\r\nInvalid data. Please try again.\r\n");
            uart_data_requested = 0; // Sẽ kích hoạt gửi LẠI request (cho CÙNG MỘT tham số)
            // Không reset uart_retry_count, lần gửi lại này sẽ được tính là một lần thử
        }

    } else {
        // --- KHÔNG CÓ LỆNH MỚI (CHƯA NHẬN ĐƯỢC) ---
        // Kiểm tra logic timeout
        if (uart_timeout_counter > 0) {
            uart_timeout_counter--; // Giảm bộ đếm 50ms
        } else {
            // TIMEOUT (10 giây đã trôi qua)

            // --- SỬA LỖI HAL_BUSY ---
            // Dừng nghe trước khi thử gửi lại
            uart_stop_listening();

            uart_Rs232SendString((uint8_t*)"\r\nTimeout. Retrying...\r\n");
            uart_data_requested = 0; // Sẽ kích hoạt gửi lại request ở vòng lặp FSM tiếp theo
        }
    }
}
// --- HẾT HÀM CẬP NHẬT ---


/**
 * @brief Display the current mode at the bottom of the screen
 */
static void display_mode_status(void) {
    // Clear status bar
    lcd_Fill(0, 280, 240, 320, BLACK);

    switch(current_mode) {
        case MODE_VIEW_TIME:
            lcd_ShowStr(10, 290, (uint8_t*)"MODE: VIEW", WHITE, BLACK, 24, 0);
            break;
        case MODE_SET_TIME:
            lcd_ShowStr(10, 290, (uint8_t*)"MODE: SET TIME", WHITE, BLACK, 24, 0);
            break;
        case MODE_SET_ALARM:
            lcd_ShowStr(10, 290, (uint8_t*)"MODE: SET ALARM", WHITE, BLACK, 24, 0);
            break;
        case MODE_UPDATE_VIA_UART:
            lcd_ShowStr(10, 290, (uint8_t*)"MODE: UART UPDATE", WHITE, BLACK, 24, 0);
            break;
        case MODE_MESSAGE_DISPLAY:
            lcd_ShowStr(10, 290, (uint8_t*)"MODE: MESSAGE", WHITE, BLACK, 24, 0);
            break;
    }

    // Show (A) icon if alarm is enabled
    if (alarm_enabled) {
         lcd_ShowStr(200, 290, (uint8_t*)"(A)", CYAN, BLACK, 24, 0);
    }
}


/**
 * @brief Display time and date on LCD
 */
static void displayTime(void){
    char str_buff[5];

    // Display Time (HH:MM:SS)
	lcd_ShowIntNum(70, 100, ds3231_hours, 2, GREEN, BLACK, 24);
	lcd_ShowStr(100, 100, (uint8_t*)":", GREEN, BLACK, 24, 0);
	lcd_ShowIntNum(110, 100, ds3231_min, 2, GREEN, BLACK, 24);
	lcd_ShowStr(140, 100, (uint8_t*)":", GREEN, BLACK, 24, 0);
	lcd_ShowIntNum(150, 100, ds3231_sec, 2, GREEN, BLACK, 24);

    // Display Date (Day, DD/MM/YY)
    if (ds3231_day > 0 && ds3231_day <= 7) {
        sprintf(str_buff, "%s", day_names[ds3231_day]);
    } else {
        sprintf(str_buff, "??"); // Error
    }
	lcd_ShowStr(20, 130, (uint8_t*)str_buff, YELLOW, BLACK, 24, 0);

	lcd_ShowIntNum(70, 130, ds3231_date, 2, YELLOW, BLACK, 24);
	lcd_ShowStr(100, 130, (uint8_t*)"/", YELLOW, BLACK, 24, 0);
	lcd_ShowIntNum(110, 130, ds3231_month, 2, YELLOW, BLACK, 24);
	lcd_ShowStr(140, 130, (uint8_t*)"/", YELLOW, BLACK, 24, 0);
	lcd_ShowIntNum(150, 130, ds3231_year, 2, YELLOW, BLACK, 24);

    // Clear settings area if in VIEW mode and alarm is not triggered
    if (current_mode == MODE_VIEW_TIME && !alarm_triggered) {
         lcd_Fill(0, 160, 240, 220, BLACK);
    }
}


/**
 * @brief Main FSM execution function. Call this from the main loop.
 * (ĐÃ SỬA LỖI XUNG ĐỘT NÚT RESET)
 */
void clock_fsm_run(void) {

    uint16_t mode_btn_current_count = button_count[BTN_MODE_SWITCH];

    // --- XỬ LÝ NHẤN GIỮ NÚT MODE (ƯU TIÊN CAO NHẤT) ---

    // 1. KIỂM TRA RESET (Nhấn giữ > 3 giây)
    if (mode_btn_current_count > RESET_LONG_PRESS_DURATION) {
        button_count[BTN_MODE_SWITCH] = 0; // Xóa (để tránh lặp lại reset)
        mode_btn_last_count = 0;         // Reset bộ theo dõi

        // 1. Reset RTC time
        ds3231_Write(ADDRESS_YEAR, 25);
        ds3231_Write(ADDRESS_MONTH, 11);
        ds3231_Write(ADDRESS_DATE, 5);
        ds3231_Write(ADDRESS_DAY, 3);
        ds3231_Write(ADDRESS_HOUR, 10);
        ds3231_Write(ADDRESS_MIN, 21);
        ds3231_Write(ADDRESS_SEC, 0);

        // 2. Reset alarm
        alarm_hour = 6;
        alarm_min = 0;
        alarm_enabled = 0;

        // 3. Hiển thị thông báo và quay về VIEW_TIME
        enter_message_display_mode("System Reset!", GREEN, NULL);

        return; // Đã Reset, kết thúc vòng FSM này
    }

    // 2. KIỂM TRA THOÁT MODE MESSAGE (Nhấn thả ngắn khi đang xem thông báo)
    if (current_mode == MODE_MESSAGE_DISPLAY) {
        // Phát hiện sự kiện "thả nút"
        if (mode_btn_current_count == 0 && mode_btn_last_count > 0) {
            current_mode = MODE_VIEW_TIME;
            lcd_Fill(0, 100, 240, 220, BLACK);
            mode_btn_last_count = 0; // Cập nhật bộ theo dõi
        } else {
            mode_btn_last_count = mode_btn_current_count; // Cập nhật bộ theo dõi
        }

        // Khi ở mode MESSAGE, FSM chỉ chạy logic của mode đó
        handle_message_display_mode();
        return; // Bỏ qua FSM chính và thanh trạng thái
    }

    // 3. KIỂM TRA CHUYỂN MODE (Nhấn thả ngắn khi ở các mode chính)
    // Phát hiện sự kiện "thả nút"
    if (mode_btn_current_count == 0 && mode_btn_last_count > 0) {
        // Nút vừa được thả.
        // mode_btn_last_count giữ giá trị đếm *ngay trước khi thả*

        // Nếu code chạy đến đây, nghĩa là nó KHÔNG PHẢI là long press (vì đã bị check ở trên)
        handle_mode_switch(); // Gọi hàm chuyển chế độ
    }

    // --- Cập nhật bộ đếm cuối cùng cho vòng lặp tiếp theo ---
    mode_btn_last_count = mode_btn_current_count;


    // --- Update 2Hz blink flag ---
    blink_counter = (blink_counter + 1) % 10; // 10 * 50ms = 500ms (2Hz)
    blink_flag = (blink_counter < 5) ? 1 : 0; // 250ms ON (1), 250ms OFF (0)


    // --- FSM logic ---
    switch(current_mode) {
        case MODE_VIEW_TIME:
            handle_view_time_mode();
            break;
        case MODE_SET_TIME:
            handle_set_time_mode();
            break;
        case MODE_SET_ALARM:
            handle_set_alarm_mode();
            break;
        case MODE_UPDATE_VIA_UART:
            handle_uart_update_mode();
            break;
        case MODE_MESSAGE_DISPLAY:
            break;
    }

    // --- Display current mode status ---
    display_mode_status();
}
