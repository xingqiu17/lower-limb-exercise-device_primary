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

extern uint32_t g_current_action_mode;
extern uint32_t g_current_action_count;


#define MAX_SLAVES  1

// Set to 0 to compile out the boot TEST packet task.
#ifndef ESPNOW_SR_TEST_TASK_ENABLE
#define ESPNOW_SR_TEST_TASK_ENABLE 0
#endif

#define ESPNOW_SR_TEST_PACKET_COUNT       100
#define ESPNOW_SR_TEST_PACKET_INTERVAL_MS 100
//TODO 待修改


extern QueueHandle_t master_evt_queue;    //主设备事件句柄

/*----------------------传输信息类型----------------------*/
typedef enum {
    CONNECTION_REQUEST,          //主设备发送的连接请求
    CONNECTION_SLAVE_CONFIRM,      //从设备接受主设备连接请求后，发回给主设备的连接确认
    CONNECTION_MASTER_CONFIRM,   //主设备接受从设备连接确认后，发回给从设备的连接确认
    STATUS_CHANGE,               //主设备->从设备：状态切换
    STATUS_CONFIRM,              //从设备->主设备：状态确认
    EXERCISE_DATA,               //锻炼数据
    KEEP_ALIVE,                 //从设备->主设备  心跳包
    POWER_MANAGE,               //电源管理：主->从 data=1开机/0关机；从->主 data=2确认
    

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
    EVT_SLAVE_POWER_ACK,       //接收到从设备电源管理确认（data=2）
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
void master_keepalive_monitor_task(void *arg);

void app_handle_slave_link_lost(void);

void app_handle_slave_link_restored(void);

void app_mark_pairing_task_done(void);

esp_err_t app_uart_write_handle(uint8_t *src_addr, void *data,
                                       size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);


esp_err_t master_receive_handle(uint8_t *src_addr,void *data,size_t size,wifi_pkt_rx_ctrl_t *rx_ctrl);

esp_err_t nvs_save_slave_mac(const uint8_t *mac);

// Update current action mode (0: none, 1-4: action), and reset action count.
void master_set_current_action_mode(uint32_t action_mode);

// Send STATUS_CHANGE action command to all READY slaves.
esp_err_t master_send_action_to_ready_slaves(uint32_t action_code);

// Send POWER_MANAGE command to all READY slaves. power_data: 1=power on, 0=power off.
esp_err_t master_send_power_manage_to_ready_slaves(uint32_t power_data);

// Wait slave POWER_MANAGE ack (data=2) within timeout.
bool master_wait_slave_power_ack(uint32_t timeout_ms);

// Control whether master is in shutdown flow. When true, non-shutdown events should be ignored.
void master_set_shutdown_in_progress(bool in_progress);

// Query shutdown flow state.
bool master_is_shutdown_in_progress(void);

// Start/stop the boot TEST task. It sends TEST data=0 and counts TEST data=1 replies.
esp_err_t master_start_test_task(void);
void master_stop_test_task(void);
uint32_t master_get_test_reply_count(void);
void master_reset_test_reply_count(void);



#ifdef __cplusplus
}
#endif
