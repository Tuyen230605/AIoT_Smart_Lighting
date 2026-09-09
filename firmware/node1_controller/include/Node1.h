/**
 * Node1.h — Khai báo dùng chung giữa các task của riêng node 1.
 *
 * Hợp đồng liên node nằm ở firmware/common/include/. File này chỉ chứa những
 * thứ không ra khỏi node 1, để các file .cpp không phải rải `extern` cho nhau.
 */
#ifndef NODE1_H
#define NODE1_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Giá trị lux mới nhất, TaskSensors ghi → TaskNetwork đọc.
 *
 * Dài đúng 1 phần tử và ghi bằng xQueueOverwrite: người đọc luôn thấy số đo
 * mới nhất, người ghi không bao giờ bị chặn. Đây là cách thay cho một biến
 * float toàn cục dùng chung — cùng lý do với G1.1, chỉ khác quy mô.
 */
extern QueueHandle_t oiLuxQueue;

void TaskNetwork(void *pvParameters);
void TaskSensors(void *pvParameters);
void TaskLED(void *pvParameters);
void TaskMic(void *pvParameters);
void TaskButton(void *pvParameters);

#endif // NODE1_H
