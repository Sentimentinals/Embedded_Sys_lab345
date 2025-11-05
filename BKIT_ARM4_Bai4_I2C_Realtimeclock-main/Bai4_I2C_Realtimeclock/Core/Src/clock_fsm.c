/*
 * clock_fsm.c
 *
 * Created on: Nov 5, 2025
 * Author: Gemini
 * Implementation of the clock logic FSM.
 */

#include "clock_fsm.h"
#include "software_timer.h"
#include "button.h"
#include "lcd.h"
#include "ds3231.h"
#include <stdio.h>
#include "uart.h"     // Thêm vào
#include "stdlib.h"   // Thêm vào
#include "string.h"   // Thêm vào

/* Private typedef -----------------------------------------------------------*/
// System Modes
typedef enum {
    MODE_VIEW_TIME,
    MODE_SET_TIME,
    MODE_SET_ALARM,
    MODE_UPDATE_VIA_UART // Thêm vào
} ClockMode_t;

// Parameters for SET_TIME mode (Sẽ được tái sử dụng cho UART update)
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
// Button mapping (index in button_count array)
#define BTN_MODE_SWITCH 0 // SW0
#define BTN_UP 3          // SW3 ("Up")
#define BTN_DOWN 7        // SW7 ("Down")
#define BTN_SAVE_NEXT 12   // SW6 ("E")

// Button press duration (in 50ms ticks)
#define LONG_PRESS_DURATION 40  // 2000ms
#define AUTO_INCREMENT_PERIOD 4 // 200ms (for auto inc/dec)

/* Private variables ---------------------------------------------------------*/
// FSM state variables
static ClockMode_t current_mode = MODE_VIEW_TIME;
static SetTimeParam_t set_time_param = SET_HOUR;
static SetAlarmParam_t set_alarm_param = SET_ALARM_HOUR;

// Temp variables for time setting
static uint8_t temp_hour, temp_min, temp_sec, temp_day, temp_date, temp_month, temp_year;

// Alarm variables
static uint8_t alarm_hour = 6;
static uint8_t alarm_min = 0;
static uint8_t alarm_enabled = 0;
static uint8_t alarm_triggered = 0;
static uint16_t alarm_display_counter = 0; // Alarm duration counter

// 2Hz blink logic
static uint16_t blink_counter = 0;
static uint8_t blink_flag = 0; // 1 = Show, 0 = Hide

// Day names lookup table
const char* day_names[8] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// --- Thêm biến cho UART Update FSM ---
static SetTimeParam_t uart_update_param = SET_HOUR;
static uint8_t uart_data_requested = 0; // Flag to stop spamming requests
#define UART_RX_BUFFER_SIZE 32
static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
// --- Hết phần thêm ---

/* Private function prototypes -----------------------------------------------*/
static void displayTime(void);
static void handle_mode_switch(void);
static void handle_view_time_mode(void);
static void handle_set_time_mode(void);
static void handle_set_alarm_mode(void);
static void handle_uart_update_mode(void); // <-- Thêm vào
static void display_mode_status(void);
static uint8_t get_max_date(uint8_t month, uint8_t year);
static void increment_setting(void);
static void decrement_setting(void);
static void increment_alarm_setting(void);
static void decrement_alarm_setting(void);

/* Function Implementation ---------------------------------------------------*/

/**
 * @brief Get the max date for a given month/year (simple leap year check)
 */
static uint8_t get_max_date(uint8_t month, uint8_t year) {
    if (month == 2) {
        if (year % 4 == 0) { // Basic leap year check
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

/**
 * @brief Increment the temporary setting value based on current parameter
 */
static void increment_setting(void) {
    uint8_t max_date;
    switch(set_time_param) {
        case SET_HOUR: temp_hour = (temp_hour + 1) % 24; break;
        case SET_MIN:  temp_min = (temp_min + 1) % 60; break;
        case SET_SEC:  temp_sec = (temp_sec + 1) % 60; break;
        case SET_DAY:  temp_day = (temp_day % 7) + 1; break; // 1 (Mon) - 7 (Sun)
        case SET_DATE:
            max_date = get_max_date(temp_month, temp_year);
            temp_date = (temp_date % max_date) + 1;
            break;
        case SET_MONTH: temp_month = (temp_month % 12) + 1; break;
        case SET_YEAR:  temp_year = (temp_year + 1) % 100; break;
    }
}

/**
 * @brief [NEW] Decrement the temporary setting value
 */
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

/**
 * @brief Increment the alarm setting value
 */
static void increment_alarm_setting(void) {
    switch(set_alarm_param) {
        case SET_ALARM_HOUR: alarm_hour = (alarm_hour + 1) % 24; break;
        case SET_ALARM_MIN:  alarm_min = (alarm_min + 1) % 60; break;
        case SET_ALARM_ENABLE: alarm_enabled = !alarm_enabled; break;
    }
}

/**
 * @brief [NEW] Decrement the alarm setting value
 */
static void decrement_alarm_setting(void) {
    switch(set_alarm_param) {
        case SET_ALARM_HOUR: alarm_hour = (alarm_hour + 24 - 1) % 24; break;
        case SET_ALARM_MIN:  alarm_min = (alarm_min + 60 - 1) % 60; break;
        case SET_ALARM_ENABLE: alarm_enabled = !alarm_enabled; break; // Toggle is same
    }
}


/**
 * @brief Handle the mode switch button press
 */
static void handle_mode_switch(void) {
    if (button_count[BTN_MODE_SWITCH] == 1) { // Process on new press

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

        // --- MODIFIED: Cycle mode (0 -> 1 -> 2 -> 3 -> 0) ---
        current_mode = (current_mode + 1) % 4;

        // Init SET_TIME mode
        if (current_mode == MODE_SET_TIME) {
            ds3231_ReadTime(); // Load current time into temp vars
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
        // --- ADDED: Init UART UPDATE mode ---
        else if (current_mode == MODE_UPDATE_VIA_UART) {
            // Load current time into temp vars as default
            ds3231_ReadTime();
            temp_hour = ds3231_hours; temp_min = ds3231_min; temp_sec = ds3231_sec;
            temp_day = ds3231_day; temp_date = ds3231_date; temp_month = ds3231_month;
            temp_year = ds3231_year;

            uart_update_param = SET_HOUR; // Bắt đầu từ Giờ
            uart_data_requested = 0;      // Sẵn sàng gửi yêu cầu đầu tiên

            // Xóa vùng hiển thị thời gian và vùng cài đặt
            lcd_Fill(0, 100, 240, 220, BLACK);
            uart_Rs232SendString((uint8_t*)"\r\n--- ENTERING UART UPDATE MODE ---\r\n");
        }
        // --- END ADDED ---

        // Clear setting area when returning to VIEW_TIME
        else if (current_mode == MODE_VIEW_TIME) {
             lcd_Fill(0, 160, 240, 220, BLACK);
        }
    }
}

/**
 * @brief Handle logic for VIEW_TIME mode
 */
static void handle_view_time_mode(void) {
    // Stop alarm if UP, DOWN, or SAVE button is pressed
    if (alarm_triggered && (button_count[BTN_UP] == 1 || button_count[BTN_DOWN] == 1 || button_count[BTN_SAVE_NEXT] == 1)) {
        alarm_triggered = 0;
        lcd_Fill(60, 160, 180, 200, BLACK); // Clear alarm visual
    }

    if (alarm_triggered) {
        // Handle alarm timeout (10s)
        if (alarm_display_counter > 0) {
            alarm_display_counter--;
        } else {
            alarm_triggered = 0;
            lcd_Fill(60, 170, 180, 200, BLACK); // Clear alarm visual
        }

        // Alarm visual effect (blinking)
        if (blink_flag) {
            lcd_Fill(60, 170, 180, 200, RED);
            lcd_ShowStr(70, 175, (uint8_t*)"ALARM!", BLACK, RED, 24, 0);
        } else {
            lcd_Fill(60, 170, 180, 200, BLACK);
        }
        displayTime(); // Keep showing time

    } else {
        // Read time from RTC
        ds3231_ReadTime();

        // Check for alarm trigger condition
        if (alarm_enabled &&
            !alarm_triggered &&
            ds3231_hours == alarm_hour &&
            ds3231_min == alarm_min &&
            ds3231_sec == 0) // Trigger at second 0
        {
            alarm_triggered = 1;
            alarm_display_counter = 200; // 10 seconds (200 * 50ms)
        }

        // Normal time display
        displayTime();
    }
}

/**
 * @brief Handle logic for SET_TIME mode
 */
static void handle_set_time_mode(void) {
    // Clock is "stopped" (we don't call ds3231_ReadTime())

    // Handle UP button (Increment value)
    if (button_count[BTN_UP] == 1) { // Single press
        increment_setting();
    } else if (button_count[BTN_UP] > LONG_PRESS_DURATION) { // Hold press
        if (button_count[BTN_UP] % AUTO_INCREMENT_PERIOD == 0) {
            increment_setting(); // Auto-increment
        }
    }

    // Handle DOWN button (Decrement value)
    if (button_count[BTN_DOWN] == 1) {
        decrement_setting();
    } else if (button_count[BTN_DOWN] > LONG_PRESS_DURATION) {
        if (button_count[BTN_DOWN] % AUTO_INCREMENT_PERIOD == 0) {
            decrement_setting(); // Auto-decrement
        }
    }

    // Handle SAVE/NEXT button (Switch parameter)
    if (button_count[BTN_SAVE_NEXT] == 1) {
        set_time_param = (set_time_param + 1);
        if (set_time_param > SET_YEAR) { // Wrap around
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

    // Clear alarm area
    lcd_Fill(0, 160, 240, 220, BLACK);
}

/**
 * @brief Handle logic for SET_ALARM mode
 */
static void handle_set_alarm_mode(void) {
    // Keep showing the real time
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
        set_alarm_param = (set_alarm_param + 1) % 3; // 3 params: Hour, Min, Enable
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
 * @brief [ADDED] Handle logic for UART UPDATE mode
 */
static void handle_uart_update_mode(void) {
    char str_buff[50]; // Buffer cho LCD và UART

    // 1. Gửi yêu cầu (request) nếu chưa gửi
    if (uart_data_requested == 0) {
        lcd_Fill(0, 170, 240, 200, BLACK); // Xóa dòng trạng thái cũ
        switch(uart_update_param) {
            case SET_HOUR:
                sprintf(str_buff, "Updating hours...");
                uart_Rs232SendString((uint8_t*)"Hours (0-23): ");
                break;
            case SET_MIN:
                sprintf(str_buff, "Updating minutes...");
                uart_Rs232SendString((uint8_t*)"Minutes (0-59): ");
                break;
            case SET_SEC:
                sprintf(str_buff, "Updating seconds...");
                uart_Rs232SendString((uint8_t*)"Seconds (0-59): ");
                break;
            case SET_DAY:
                sprintf(str_buff, "Updating day (1-7)...");
                uart_Rs232SendString((uint8_t*)"Day (1=Mon - 7=Sun): ");
                break;
            case SET_DATE:
                sprintf(str_buff, "Updating date...");
                uart_Rs232SendString((uint8_t*)"Date (1-31): ");
                break;
            case SET_MONTH:
                sprintf(str_buff, "Updating month...");
                uart_Rs232SendString((uint8_t*)"Month (1-12): ");
                break;
            case SET_YEAR:
                sprintf(str_buff, "Updating year (0-99)...");
                uart_Rs232SendString((uint8_t*)"Year (0-99): ");
                break;
        }
        // Hiển thị trạng thái lên LCD
        lcd_ShowStr(20, 170, (uint8_t*)str_buff, MAGENTA, BLACK, 16, 0);
        uart_data_requested = 1; // Đánh dấu là đã gửi yêu cầu
    }

    // 2. Kiểm tra xem có lệnh nào từ UART không
    if (uart_get_command(uart_rx_buffer)) {
        int32_t val = uart_parse_num_from_string(uart_rx_buffer);

        // 3. Lưu giá trị hợp lệ vào biến tạm
        switch(uart_update_param) {
            case SET_HOUR:  if (val >= 0 && val <= 23) temp_hour = (uint8_t)val; break;
            case SET_MIN:   if (val >= 0 && val <= 59) temp_min = (uint8_t)val;  break;
            case SET_SEC:   if (val >= 0 && val <= 59) temp_sec = (uint8_t)val;  break;
            case SET_DAY:   if (val >= 1 && val <= 7)  temp_day = (uint8_t)val;  break;
            case SET_DATE:  if (val >= 1 && val <= 31) temp_date = (uint8_t)val; break; // (Chưa kiểm tra ngày/tháng)
            case SET_MONTH: if (val >= 1 && val <= 12) temp_month = (uint8_t)val; break;
            case SET_YEAR:  if (val >= 0 && val <= 99) temp_year = (uint8_t)val;  break;
        }

        // Phản hồi lại máy tính
        uart_Rs232SendString((uint8_t*)"Received: ");
        uart_Rs232SendString(uart_rx_buffer);
        uart_Rs232SendString((uint8_t*)"\r\n");

        // 4. Chuyển sang trạng thái tiếp theo
        uart_update_param++;
        uart_data_requested = 0; // Sẵn sàng cho yêu cầu tiếp theo

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

            // Hiển thị thông báo hoàn tất
            lcd_Fill(0, 170, 240, 200, BLACK);
            lcd_ShowStr(20, 170, (uint8_t*)"Update Complete!", GREEN, BLACK, 16, 0);
            uart_Rs232SendString((uint8_t*)"Update Complete! Returning to VIEW_TIME mode.\r\n");

            // Trở về chế độ xem giờ
            current_mode = MODE_VIEW_TIME;
        }
    }
}


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
 */
void clock_fsm_run(void) {
    // --- Update 2Hz blink flag ---
    blink_counter = (blink_counter + 1) % 10; // 10 * 50ms = 500ms (2Hz)
    blink_flag = (blink_counter < 5) ? 1 : 0; // 250ms ON (1), 250ms OFF (0)

    // --- Handle mode switching ---
    handle_mode_switch();

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
    }

    // --- Display current mode status ---
    display_mode_status();
}
