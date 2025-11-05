/*
 * traffic_fsm.c
 *
 * Created on: 5 Nov 2025
 * Author: Gemini
 *
 * English Comments Update:
 * - Added English comments for all static and public functions.
 * - Logic remains 4 modes, 6 states.
 */

#include "traffic_fsm.h"
#include "main.h" // For button definitions and colors
#include "lcd.h"
#include "button.h"
#include "software_timer.h" // For flag_timer2
#include "led_7seg.h"
#include <stdio.h> // For sprintf

// --- Button Definitions ---
#define BUTTON_MODE     0
#define BUTTON_ADJUST   1
#define BUTTON_CONFIRM  2

// --- Static State Variables ---
static System_Mode_t current_mode;
static Traffic_State_t traffic_state;

// --- Time Periods (in seconds) ---
static int period_green = 5;
static int period_yellow = 2;
static int period_red = 2; // Duration for the "All Red" transition phase

static int temp_period_value = 0; // Temporary value during modification

// --- Countdown Timers (for 2 routes) ---
static int r1_timer = 5; // Countdown timer for Route 1 (LEDs 2&3 - Right)
static int r2_timer = 9; // Countdown timer for Route 2 (LEDs 0&1 - Left)

// --- Internal Timers ---
static int second_counter = 0;    // ms counter to create a 1-second tick
static int blink_counter = 0;     // ms counter to create a 2Hz blink
static int blink_state = 0;       // Current blink state (0 = OFF, 1 = ON)

static char lcd_buffer[50]; // String buffer for LCD display

// --- Static Function Prototypes ---
static int is_button_pressed(int button_index);
static void fsm_normal_mode_run();
static void fsm_modify_mode_run();
static void draw_traffic_lights(int r1_state, int r2_state, int blink_r, int blink_g, int blink_y);
static void update_lcd_display();

/**
 * @brief Initializes the Traffic Light State Machine.
 * Sets the default mode to NORMAL, resets timers to default values, and clears the LCD.
 */
void fsm_traffic_init() {
    current_mode = MODE_NORMAL;
    traffic_state = STATE_R1_GREEN_R2_RED;

    // Initialize timers for the 6-state cycle
    r1_timer = period_green;
    r2_timer = period_green + period_yellow + period_red; // Total red time for R2

    second_counter = 0;
    blink_counter = 0;
    blink_state = 0;
    lcd_Clear(BLACK); // Clear the screen
}

/**
 * @brief Checks if a specific button was just pressed (rising edge detection).
 * @param button_index The index (0-15) of the button to check.
 * @retval 1 if pressed (count == 1), 0 otherwise.
 */
static int is_button_pressed(int button_index) {
    // Relies on button_count[] from button.c
    if (button_count[button_index] == 1) {
        return 1;
    }
    return 0;
}

/**
 * @brief Main run function for the state machine, called every 50ms cycle.
 * Handles timers, button inputs for mode switching, and calls the appropriate sub-FSM.
 */
void fsm_traffic_run() {
    // --- Update internal timers (called every 50ms) ---
    blink_counter += 50;
    second_counter += 50;

    // 2Hz = 500ms period (250ms ON, 250ms OFF)
    if (blink_counter >= 250) {
        blink_counter = 0;
        blink_state = !blink_state; // Toggle blink state
    }

    // --- Handle Mode Switching (Button 1) ---
    if (is_button_pressed(BUTTON_MODE)) {
        current_mode++;
        if (current_mode > MODE_MODIFY_YELLOW) {
            current_mode = MODE_NORMAL;
        }

        blink_counter = 0; // Reset blink timer
        blink_state = 1;   // Always start in ON state when switching mode

        // Load current period value into temp variable for editing
        switch(current_mode) {
            case MODE_NORMAL:
                // Reset to default traffic state and timers
                traffic_state = STATE_R1_GREEN_R2_RED;
                r1_timer = period_green;
                r2_timer = period_green + period_yellow + period_red;
                second_counter = 0; // Reset 1-second tick
                break;
            case MODE_MODIFY_RED:
                temp_period_value = period_red;
                break;
            case MODE_MODIFY_GREEN:
                temp_period_value = period_green;
                break;
            case MODE_MODIFY_YELLOW:
                temp_period_value = period_yellow;
                break;
        }
    }

    // --- Run the FSM logic based on the current mode ---
    if (current_mode == MODE_NORMAL) {
        fsm_normal_mode_run();
    } else {
        fsm_modify_mode_run();
    }

    // --- Update all displays ---
    // (Called every cycle to ensure smooth blinking in modify modes)
    update_lcd_display();
}

/**
 * @brief Handles the logic for MODE_NORMAL.
 * Runs a 1-second countdown timer and transitions the 6 traffic light states.
 */
static void fsm_normal_mode_run() {
    // --- Create 1-second tick ---
    if (second_counter < 1000) {
        return; // Not 1 second yet, exit
    }
    second_counter = 0; // Reset ms counter

    // --- Decrement countdown timers (in seconds) ---
    r1_timer--;
    r2_timer--;

    // --- Handle state transitions based on which timer reached 0 ---
    switch (traffic_state) {
        case STATE_R1_GREEN_R2_RED:
            if (r1_timer == 0) { // R1 Green time finished
                traffic_state = STATE_R1_YELLOW_R2_RED;
                r1_timer = period_yellow; // Load R1 Yellow time
            }
            break;

        case STATE_R1_YELLOW_R2_RED:
            if (r1_timer == 0) { // R1 Yellow time finished
                traffic_state = STATE_ALL_RED_1;
                r1_timer = period_red; // Load "All Red" time
            }
            break;

        case STATE_ALL_RED_1:
             if (r1_timer == 0) { // "All Red 1" time finished (R2 timer also 0)
                traffic_state = STATE_R1_RED_R2_GREEN;
                r1_timer = period_green + period_yellow + period_red; // Load R1 total Red time
                r2_timer = period_green;                 // Load R2 Green time
            }
            break;

        case STATE_R1_RED_R2_GREEN:
             if (r2_timer == 0) { // R2 Green time finished
                traffic_state = STATE_R1_RED_R2_YELLOW;
                r2_timer = period_yellow; // Load R2 Yellow time
            }
            break;

        case STATE_R1_RED_R2_YELLOW:
            if (r2_timer == 0) { // R2 Yellow time finished
                traffic_state = STATE_ALL_RED_2;
                r2_timer = period_red; // Load "All Red" time
            }
            break;

        case STATE_ALL_RED_2:
            if (r2_timer == 0) { // "All Red 2" time finished (R1 timer also 0)
                traffic_state = STATE_R1_GREEN_R2_RED;
                r1_timer = period_green;                 // Load R1 Green time
                r2_timer = period_green + period_yellow + period_red; // Load R2 total Red time
            }
            break;
    }
}

/**
 * @brief Handles the logic for all MODIFY modes (MOD_RED, MOD_GREEN, MOD_YELLOW).
 * Listens for BUTTON_ADJUST to increment temp value and BUTTON_CONFIRM to save it.
 */
static void fsm_modify_mode_run() {
    // --- Handle Button 2 (ADJUST) ---
    if (is_button_pressed(BUTTON_ADJUST)) {
        temp_period_value++;
        if (temp_period_value > 99) {
            temp_period_value = 1; // Wrap from 99 back to 1
        }
    }

    // --- Handle Button 3 (CONFIRM) ---
    if (is_button_pressed(BUTTON_CONFIRM)) {
        // Save the temporary value to the corresponding permanent period
        switch(current_mode) {
            case MODE_MODIFY_RED:
                period_red = temp_period_value;
                break;
            case MODE_MODIFY_GREEN:
                period_green = temp_period_value;
                break;
            case MODE_MODIFY_YELLOW:
                period_yellow = temp_period_value;
                break;
            case MODE_NORMAL: // Should not happen
            default:
                break;
        }
    }
}

/**
 * @brief Updates all display outputs (LCD text, 7-Segment LEDs, and traffic light circles).
 */
static void update_lcd_display() {
    int r1_light = -1; // -1=Off, 0=Red, 1=Yellow, 2=Green (Route 1 - Right)
    int r2_light = -1; // (Route 2 - Left)
    int blink_r = 0, blink_g = 0, blink_y = 0; // Flags to indicate which color should blink

    // 1. Determine light states and LCD text based on mode
    switch (current_mode) {
        case MODE_NORMAL:
            // Add spaces to overwrite longer strings (e.g., "MODIFY YELLOW")
            sprintf(lcd_buffer, "MODE: NORMAL (1)     ");
            lcd_ShowStr(10, 10, lcd_buffer, WHITE, BLACK, 16, 0);

            // Display T2 (Left) and T1 (Right) countdowns
            sprintf(lcd_buffer, "T2: %02d s  T1: %02d s  ", r2_timer, r1_timer);
            lcd_ShowStr(10, 30, lcd_buffer, WHITE, BLACK, 24, 0);

            // Set light states based on the current traffic FSM state
            switch (traffic_state) {
                case STATE_R1_GREEN_R2_RED:  r1_light = 2; r2_light = 0; break;
                case STATE_R1_YELLOW_R2_RED: r1_light = 1; r2_light = 0; break;
                case STATE_R1_RED_R2_GREEN:  r1_light = 0; r2_light = 2; break;
                case STATE_R1_RED_R2_YELLOW: r1_light = 0; r2_light = 1; break;
                case STATE_ALL_RED_1:
                case STATE_ALL_RED_2:        r1_light = 0; r2_light = 0; break;
            }

            // Update 7-Segment LEDs (R2 on left, R1 on right)
            led7_SetDigit(r2_timer / 10, 0, 0); // Route 2 (LEDs 0&1)
            led7_SetDigit(r2_timer % 10, 1, 0);
            led7_SetDigit(r1_timer / 10, 2, 0); // Route 1 (LEDs 2&3)
            led7_SetDigit(r1_timer % 10, 3, 0);
            break;

        case MODE_MODIFY_RED:
            sprintf(lcd_buffer, "MODE: MODIFY RED (2) ");
            lcd_ShowStr(10, 10, lcd_buffer, WHITE, BLACK, 16, 0);
            sprintf(lcd_buffer, "Value: %02d         ", temp_period_value);
            lcd_ShowStr(10, 30, lcd_buffer, YELLOW, BLACK, 24, 0);

            r1_light = 0; // Both Red
            r2_light = 0;
            blink_r = 1;  // Enable blinking for RED

            // Update 7-Segment LEDs (Show value on the right side)
            led7_SetDigit(0, 0, 0); // Clear left side
            led7_SetDigit(0, 1, 0);
            led7_SetDigit(temp_period_value / 10, 2, 0);
            led7_SetDigit(temp_period_value % 10, 3, 0);
            break;

        case MODE_MODIFY_GREEN:
            sprintf(lcd_buffer, "MODE: MODIFY GREEN (3)");
            lcd_ShowStr(10, 10, lcd_buffer, WHITE, BLACK, 16, 0);
            sprintf(lcd_buffer, "Value: %02d         ", temp_period_value);
            lcd_ShowStr(10, 30, lcd_buffer, YELLOW, BLACK, 24, 0);

            r1_light = 2; // Both Green
            r2_light = 2;
            blink_g = 1;  // Enable blinking for GREEN

            // Update 7-Segment LEDs
            led7_SetDigit(0, 0, 0);
            led7_SetDigit(0, 1, 0);
            led7_SetDigit(temp_period_value / 10, 2, 0);
            led7_SetDigit(temp_period_value % 10, 3, 0);
            break;

        case MODE_MODIFY_YELLOW:
            sprintf(lcd_buffer, "MODE: MODIFY YELLOW (4)");
            lcd_ShowStr(10, 10, lcd_buffer, WHITE, BLACK, 16, 0);
            sprintf(lcd_buffer, "Value: %02d         ", temp_period_value);
            lcd_ShowStr(10, 30, lcd_buffer, YELLOW, BLACK, 24, 0);

            r1_light = 1; // Both Yellow
            r2_light = 1;
            blink_y = 1;  // Enable blinking for YELLOW

            // Update 7-Segment LEDs
            led7_SetDigit(0, 0, 0);
            led7_SetDigit(0, 1, 0);
            led7_SetDigit(temp_period_value / 10, 2, 0);
            led7_SetDigit(temp_period_value % 10, 3, 0);
            break;
    }

    // 2. Call the drawing function with the determined states
    draw_traffic_lights(r1_light, r2_light, blink_r, blink_g, blink_y);
}

/**
 * @brief Draws the 6 traffic light circles on the LCD.
 * Lights that are OFF are drawn with the BLACK background color.
 *
 * @param r1_state State for Route 1 (Right): 0=Red, 1=Yellow, 2=Green, -1=Off
 * @param r2_state State for Route 2 (Left): 0=Red, 1=Yellow, 2=Green, -1=Off
 * @param blink_r Flag (1/0) if RED lights should blink
 * @param blink_g Flag (1/0) if GREEN lights should blink
 * @param blink_y Flag (1/0) if YELLOW lights should blink
 */
static void draw_traffic_lights(int r1_state, int r2_state, int blink_r, int blink_g, int blink_y) {
    // Determine logical ON/OFF state
    int r1_r_on = (r1_state == 0); // Route 1 (Right)
    int r1_y_on = (r1_state == 1);
    int r1_g_on = (r1_state == 2);

    int r2_r_on = (r2_state == 0); // Route 2 (Left)
    int r2_y_on = (r2_state == 1);
    int r2_g_on = (r2_state == 2);

    // Apply blinking logic (if blink_state is 0 [OFF phase], turn off the blinking lights)
    if (blink_state == 0) {
        if (blink_r) { r1_r_on = 0; r2_r_on = 0; }
        if (blink_g) { r1_g_on = 0; r2_g_on = 0; }
        if (blink_y) { r1_y_on = 0; r2_y_on = 0; }
    }

    // --- Draw Route 2 (Left column, x=80) ---
    // (x, y, color, radius, fill)
    lcd_DrawCircle(80, 80,  (r2_r_on) ? RED : BLACK,    20, 1); // R2 Red
    lcd_DrawCircle(80, 130, (r2_y_on) ? YELLOW : BLACK, 20, 1); // R2 Yellow
    lcd_DrawCircle(80, 180, (r2_g_on) ? GREEN : BLACK,  20, 1); // R2 Green

    // --- Draw Route 1 (Right column, x=160) ---
    lcd_DrawCircle(160, 80,  (r1_r_on) ? RED : BLACK,    20, 1); // R1 Red
    lcd_DrawCircle(160, 130, (r1_y_on) ? YELLOW : BLACK, 20, 1); // R1 Yellow
    lcd_DrawCircle(160, 180, (r1_g_on) ? GREEN : BLACK,  20, 1); // R1 Green
}
