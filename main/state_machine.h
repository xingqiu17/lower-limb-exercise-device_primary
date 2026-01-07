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
    MASTER_PAIRING,         // 已发送配对请求，等待从设备确
    MASTER_READY,           // 所有从设备握手完成
    MASTER_RUNNING,         // 正式工作
    MASTER_ERROR,

} master_state_t;


typedef enum {
    SLAVE_IDLE,
    SLAVE_WAIT_MAIN_CONFIRM,
    SLAVE_READY,
    SLAVE_RUNNING,
} slave_state_t;




/*----------------------主设备状态机事件----------------------*/
typedef enum {
    EVT_PAIR_START,      // 开始配对（外部触发）
    EVT_PAIR_DONE,       // 配对完成（协议层触发）
    EVT_PAIR_TIMEOUT,    // 配对失败/超时
    EVT_START_WORK,      // 启动工作
    EVT_STOP_WORK,       // 停止工作
    EVT_ERROR,
} master_event_t;

/*----------------------主设备状态机事件----------------------*/








/*----------------------MAC地址----------------------*/
master_state_t master_state_machine(master_state_t cur_state, master_event_t event);



#ifdef __cplusplus
}
#endif


