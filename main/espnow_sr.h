#pragma once

#include <stdint.h>
#include "esp_err.h"

#include "espnow.h"
#include "esp_wifi.h"
#include "esp_mac.h"



#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    CONNECTION_REQUEST,        //主设备发送的连接请求
    CONNECTION_SUB_CONFIRM,    //从设备接受主设备连接请求后，发回给主设备的连接确认
    CONNECTION_MAIN_CONFIRM,   //主设备接受从设备连接确认后，发回给从设备的连接确认
    STATUS_CHANGE,             //主设备->从设备：状态切换
    STATUS_CONFIRM,            //从设备->主设备：状态确认
    EXERCISE_DATA,             //锻炼数据

    TEST,                      //测试

} msg_type;


esp_err_t app_uart_write_handle(uint8_t *src_addr, void *data,
                                       size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);



#ifdef __cplusplus
}
#endif
