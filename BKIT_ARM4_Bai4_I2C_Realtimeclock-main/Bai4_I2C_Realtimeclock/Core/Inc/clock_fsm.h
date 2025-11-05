/*
 * clock_fsm.h
 *
 * Created on: Nov 5, 2025
 * Author: Gemini
 * Handles the main clock logic FSM.
 */

#ifndef INC_CLOCK_FSM_H_
#define INC_CLOCK_FSM_H_

#include "main.h"

/**
 * @brief Runs the main clock FSM.
 * This function should be called repeatedly in the main loop (e.g., every 50ms tick - 500ms).
 */
void clock_fsm_run(void);

#endif /* INC_CLOCK_FSM_H_ */
