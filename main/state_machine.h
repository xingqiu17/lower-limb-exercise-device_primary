#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "espnow.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#ifdef __cplusplus
extern "C" {
#endif



/*----------------------主设备连接状态机----------------------*/
typedef enum {
    MASTER_IDLE,            // 未开始配对
    MASTER_PAIRING,         // 已发送配对请求，等待从设备确认
    MASTER_WAIT_ALL_CONFIRM,// 收到部分从设备确认，等待所有确认
    MASTER_READY,           // 所有从设备握手完成
    MASTER_RUNNING,         // 正式工作
    MASTER_ERROR,
} master_state_t;





/*----------------------从设备连接状态机----------------------*/
typedef enum {
    SLAVE_IDLE,
    SLAVE_WAIT_MAIN_CONFIRM,
    SLAVE_READY,
    SLAVE_RUNNING,
} slave_state_t;


/*----------------------状态机事件----------------------*/
typedef enum {
    EVT_PAIR_START,          // 开始配对
    EVT_SLAVE_RESP,          // 收到从设备确认
    EVT_PAIR_TIMEOUT,        // 配对超时
    EVT_MASTER_RESP,         // 所有从设备确认完成，主设备发送最终确认
    EVT_START_WORK,          // 启动工作
    EVT_STOP_WORK,           // 停止工作
    EVT_ERROR,               // 错误
} master_event_t;


typedef enum {
    EVT_RECEIVE_REQ,         // 从设备收到主设备请求
    EVT_SEND_CONFIRM,        // 从设备发送确认
    EVT_RECEIVE_MASTER_ACK,  // 从设备收到主设备最终确认
    EVT_SLAVE_ERROR,         // 从设备出错
} slave_event_t;




/*----------------------MAC地址----------------------*/
master_state_t master_state_machine(master_state_t cur_state, master_event_t event);
slave_state_t  slave_state_machine(slave_state_t cur_state, slave_event_t event);



#ifdef __cplusplus
}
#endif


