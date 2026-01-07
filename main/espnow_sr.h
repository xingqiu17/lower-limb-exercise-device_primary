#pragma once

#include <stdint.h>
#include "esp_err.h"

#include "esp_wifi.h"
#include "esp_mac.h"
#include "state_machine.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espnow.h"
#include "espnow_storage.h"
#include "espnow_utils.h"



#ifdef __cplusplus
extern "C" {
#endif


extern uint32_t seq ;


//测试用，实际切换为2 TODO
//待修改
#define MAX_SLAVES  1

extern QueueHandle_t master_evt_queue;    //主设备事件句柄

/*----------------------传输信息类型----------------------*/
typedef enum {
    CONNECTION_REQUEST,          //主设备发送的连接请求
    CONNECTION_SLAVE_CONFIRM,      //从设备接受主设备连接请求后，发回给主设备的连接确认
    CONNECTION_MASTER_CONFIRM,   //主设备接受从设备连接确认后，发回给从设备的连接确认
    STATUS_CHANGE,               //主设备->从设备：状态切换
    STATUS_CONFIRM,              //从设备->主设备：状态确认
    EXERCISE_DATA,               //锻炼数据

    TEST,                        //测试

} msg_type;


typedef struct {
    uint8_t mac[6];
    bool    valid;
    bool    ready;
} slave_mac_info_t;

extern slave_mac_info_t g_slave_macs[MAX_SLAVES];
extern uint8_t g_slave_count;


typedef enum {
    EVT_SLAVE_CONFIRM,         // 接收到从设备确认
    EVT_SLAVE_STATUS,          //接收到从设备状态确认
    EVT_SLAVE_EXERCISE_DATA,   //接收到从设备运动数据
    EVT_PAIR_ERROR,
} master_pair_event_t;


typedef struct {
    msg_type type;
    uint32_t seq;
    uint32_t data;
} esp_now_data;


typedef struct {
    master_pair_event_t event;
    uint8_t slave_mac[6];
    uint32_t data;
} master_evt_msg_t;


/*----------------------MAC地址----------------------*/
typedef struct {
    uint8_t addr[6];
} mac_addr_t;


/*----------------------设备配对任务----------------------*/
void ms_pairing_task(void *arg);

esp_err_t app_uart_write_handle(uint8_t *src_addr, void *data,
                                       size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);


esp_err_t master_receive_handle(uint8_t *src_addr,void *data,size_t size,wifi_pkt_rx_ctrl_t *rx_ctrl);

static int find_slave_mac(const uint8_t *mac);

static bool mark_slave_ready(const uint8_t *mac);

static bool all_slaves_ready(void);

esp_err_t nvs_save_slave_mac(const uint8_t *mac);


#ifdef __cplusplus
}
#endif
