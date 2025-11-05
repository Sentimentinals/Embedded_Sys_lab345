/*
 * uart.c
 *
 * Created on: Sep 26, 2023
 * Author: HaHuyen
 *
 * --- ĐÃ SỬA LỖI VÀ CẢNH BÁO + CẬP NHẬT CHO FSM ---
 */
#include "uart.h"
#include "stdlib.h" // Thêm thư viện để dùng atoi
#include "string.h" // Thêm thư viện để dùng strlen
#include "usart.h"   // *** THÊM THƯ VIỆN NÀY ĐỂ NHẬN DIỆN 'huart1' ***

uint8_t receive_buffer1 = 0;
uint8_t msg[100];

// Biến cho Ring Buffer
static uint8_t rx_buffer[UART_BUFFER_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
volatile uint8_t uart_rx_flag = 0; // Cờ báo có dữ liệu mới

// Biến cho bộ đệm lệnh (Command Buffer)
#define UART_CMD_BUFFER_SIZE 32
static uint8_t uart_cmd_buffer[UART_CMD_BUFFER_SIZE];
static uint16_t uart_cmd_index = 0;
volatile uint8_t uart_cmd_ready_flag = 0; // Cờ báo có lệnh mới

/**
 * @brief Hàm mypow (được sao chép từ lcd.c) để uart_Rs232SendNum hoạt động.
 */
static uint32_t mypow(uint8_t m,uint8_t n)
{
	uint32_t result=1;
	while(n--)result*=m;
	return result;
}

void uart_init_rs232(){
	HAL_UART_Receive_IT(&huart1, &receive_buffer1, 1);
}

// *** HÀM MỚI ĐỂ SỬA LỖI HAL_BUSY ***
// Hàm này hủy chế độ ngắt nhận, đưa UART về trạng thái rảnh
void uart_stop_listening(void) {
    HAL_UART_AbortReceive_IT(&huart1);
}


void uart_Rs232SendString(uint8_t* str){
	HAL_UART_Transmit(&huart1, str, strlen((char*)str), 100);
}


void uart_Rs232SendBytes(uint8_t* bytes, uint16_t size){
	HAL_UART_Transmit(&huart1, bytes, size, 10);
}

void uart_Rs232SendNum(uint32_t num){
	if(num == 0){
		uart_Rs232SendString((uint8_t*)"0");
		return;
	}
    uint8_t num_flag = 0;
    int i;
	if(num < 0) uart_Rs232SendString((uint8_t*)"-");
    for(i = 10; i > 0; i--)
    {
        if((num / mypow(10, i-1)) != 0)
        {
            num_flag = 1;
            sprintf((void*)msg,"%lu",(unsigned long)(num/mypow(10, i-1)));
            uart_Rs232SendString(msg);
        }
        else
        {
            if(num_flag != 0)
            	uart_Rs232SendString((uint8_t*)"0");
        }
        num %= mypow(10, i-1);
    }
}

void uart_Rs232SendNumPercent(uint32_t num)
{
	sprintf((void*)msg,"%lu",(unsigned long)(num/100));
    uart_Rs232SendString(msg);
    uart_Rs232SendString((uint8_t*)".");
    sprintf((void*)msg,"%lu",(unsigned long)(num%100));
    uart_Rs232SendString(msg);
}


// Hàm đọc từ Ring Buffer
int16_t uart_ReadByte(void) {
    if (rx_head == rx_tail) {
        // Buffer rỗng
        uart_rx_flag = 0; // Xóa cờ
        return -1;
    } else {
        // Lấy dữ liệu từ tail
        uint8_t data = rx_buffer[rx_tail];

        // Di chuyển tail
        rx_tail = (rx_tail + 1) % UART_BUFFER_SIZE;

        // Nếu buffer rỗng sau khi đọc, xóa cờ
        if (rx_head == rx_tail) {
            uart_rx_flag = 0;
        }

        return data;
    }
}

// *** HÀM ĐÃ SỬA LỖI VÒNG LẶP VÔ HẠN (PHIÊN BẢN CHÍNH XÁC) ***
// Các hàm xử lý lệnh UART
void uart_process_incoming_data(void) {
    int16_t byte;

    // Vòng lặp while(1) để lấy TẤT CẢ các byte
    while(1) {
        byte = uart_ReadByte(); // Đọc 1 byte từ ring buffer

        if (byte == -1) {
            // Buffer rỗng, thoát khỏi vòng lặp
            break;
        }

        if (byte == '\r' || byte == '\n') { // Nếu là ký tự kết thúc lệnh
            if (uart_cmd_index > 0) { // Nếu đã có nội dung lệnh
                uart_cmd_buffer[uart_cmd_index] = '\0'; // Kết thúc chuỗi
                uart_cmd_ready_flag = 1; // Bật cờ báo lệnh sẵn sàng
                uart_cmd_index = 0; // Reset chỉ số bộ đệm lệnh

                // Khi đã nhận được lệnh, dừng xử lý các byte còn lại
                break;
            }
            // Nếu là CR/LF nhưng buffer rỗng (index=0), cứ bỏ qua và tiếp tục
        } else if (uart_cmd_index < (UART_CMD_BUFFER_SIZE - 1)) {
            // Thêm byte vào bộ đệm lệnh
            uart_cmd_buffer[uart_cmd_index++] = (uint8_t)byte;
        }
        // (Không làm gì nếu buffer đầy, byte đó bị mất)
    }
}


uint8_t uart_get_command(uint8_t* buffer) {
    if (uart_cmd_ready_flag) {
        strcpy((char*)buffer, (char*)uart_cmd_buffer); // Sao chép lệnh
        uart_cmd_ready_flag = 0; // Xóa cờ
        return 1; // Trả về 1 (có lệnh)
    }
    return 0; // Trả về 0 (không có lệnh)
}

int32_t uart_parse_num_from_string(uint8_t* str) {
    // Dùng hàm 'atoi' (ASCII to Integer) chuẩn của C
    return atoi((char*)str);
}


// Callback ngắt nhận UART
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
	if(huart->Instance == USART1){

		// Tính vị trí head tiếp theo
		uint16_t next_head = (rx_head + 1) % UART_BUFFER_SIZE;

		// Kiểm tra buffer có bị đầy không
		if (next_head != rx_tail) {
			// Thêm dữ liệu vào buffer
			rx_buffer[rx_head] = receive_buffer1;
			rx_head = next_head;

			// Bật cờ báo cho vòng lặp main (cờ này chỉ báo là có byte mới)
			uart_rx_flag = 1;
		} else {
			// Buffer đầy, dữ liệu bị mất (có thể xử lý lỗi ở đây)
		}

		// Kích hoạt lại ngắt nhận UART cho byte tiếp theo
		HAL_UART_Receive_IT(&huart1, &receive_buffer1, 1);
	}
}
