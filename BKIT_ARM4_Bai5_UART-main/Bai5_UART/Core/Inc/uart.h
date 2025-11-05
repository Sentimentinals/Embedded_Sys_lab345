/*
 * uart.h
 *
 * Created on: Sep 26, 2023
 * Author: HaHuyen
 *
 * --- ĐÃ CẬP NHẬT CHO FSM ---
 */

#ifndef INC_UART_H_
#define INC_UART_H_

#include "usart.h"
#include "stdint.h"

// Kích thước bộ đệm vòng (ring buffer)
#define UART_BUFFER_SIZE 64

void uart_init_rs232(void);
void uart_Rs232SendString(uint8_t* str);
void uart_Rs232SendBytes(uint8_t* bytes, uint16_t size);
void uart_Rs232SendNum(uint32_t num);
void uart_Rs232SendNumPercent(uint32_t num);

// Hàm xử lý dữ liệu (cho FSM)
void uart_process_incoming_data(void);
uint8_t uart_get_command(uint8_t* buffer);
int32_t uart_parse_num_from_string(uint8_t* str);

// Hàm ngắt (Callback)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

// *** HÀM MỚI ĐỂ SỬA LỖI HAL_BUSY ***
void uart_stop_listening(void);

extern volatile uint8_t uart_rx_flag;

#endif /* INC_UART_H_ */
