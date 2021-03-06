/*
 * user_master.h
 *
 *  Created on: 2017��10��17��
 *      Author: lv
 */

#ifndef APP_INCLUDE_USER_MASTER_H_
#define APP_INCLUDE_USER_MASTER_H_

#include <nopoll/nopoll.h>
#include <nopoll/nopoll_private.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_common.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "pthread.h"
#include "string.h"
#include "uart.h"
#include "gpio.h"
#include "udp.h"
#include "key.h"

//#include "user_config.h"
#include "user_iot_version.h"
#include "user_esp_platform.h"
#include "user_plug.h"

#define AP_SSID     "ZFZN_"
#define AP_PASSWORD "12345678"

#define DST_AP_SSID     "zhaofengkeji"
#define DST_AP_PASSWORD "88888888"

#define UDP_STRING		"HF-A11ASSISTHREAD"

#define REMOTE_IP		"101.201.211.87"//"192.168.0.200"//"10.10.100.104"//
#define REMOTE_DOMAIN  	"www.baidu.com"

#define UDP_LOCAL_PORT  48899
#define SERVER_PORT     8899
#define REMOTE_PORT		8080
#define DATA_LEN        128
#define MAX_CONN		4

#define ORDER_LEN	32
#define ORDER_NUM	30
#define MODE_NUM	20
#define SPI_FLASH_SEC_SIZE  4096
#define SPI_FLASH_START		0x7B
#define ADDR_TABLE_START 	0x101

#define ORDER_HEAD 		'<'
#define ORDER_END	  	'>'
#define ORDER_ALARM		'P'
#define ORDER_SET  		'S'
#define ORDER_SEND 		'T'
#define ORDER_CONTROL	'X'
#define ORDER_GETID		'U'
#define ORDER_ADD		'Y'
#define ORDER_OPEN		'H'
#define ORDER_CLOSE		'G'
#define ORDER_DELETE	'R'

#define TIMER_LOOP_IN_WEEK 	'W'
#define TIMER_LOOP_PERIOD 	'L'
#define TIMER_FIXED 		'F'

#define ORDER_OPTS_POS_OLD	9
#define ORDER_OPTS_POS_NEW	13

#define INVALID_SOCKET -1

//#define DEBUG
#ifdef DEBUG
#define MASTER_DBG printf
#else
#define MASTER_DBG /\
/printf
#endif

typedef int32 SOCKET;
typedef struct __pthread_t {char __dummy;} *pthread_t;


/* Local functions */
//void scan_done(void *arg, STATUS status);
void TCPClient(void *pvParameters);
void UDPServer(void *pvParameters);
void TCPServer(void *pvParameters);
void UartProcess(void *pvParameters);
void WaitClient(void *pvParameters);
void RecvData(void *pvParameters);
void ProcessData(void *pvParameters);
void SceneOrder(void *pvParameters);
void CheckOnline(void *pvParameters);
void TCPClientProcess(void *pvParameters);
void SendOrderTask(void *pvParameter);

void wifi_handle_event_cb(System_Event_t *evt);
void convertaddr(uint8 *buff);
void updateaddr(uint8 *buff);
void StrToHex(uint8* Str, uint8* Hex, uint8 len);
void HexToStr(uint8* Hex, uint8* Str, uint8 len);
void led_init();
void wifi_config();
void user_info();

int UpdateModeOrder(uint8 *order, uint8 len);
int DeleteModeOrder(uint8 *order, uint8 len);
int DropMode(uint8 modeidx);
int DisableMode(uint8 modeidx, uint8* order);
int EnableMode(uint8 modeidx, uint8* order);
int ProcessSensor(uint8 *order, uint8 modeidx);
int ProcessOrder(uint8 *order, uint8 len);

uint8 ReadQueue(xQueueHandle queue, uint8 *buffer, portTickType timeout);
uint8 WriteQueue(xQueueHandle queue, uint8 *data, uint8 len);
uint8 ClearQueue(xQueueHandle queue);
uint8 IsQueueEmpty(xQueueHandle queue);

uint8* FindHead(uint8* data, uint8 len);
uint8* FindEnd(uint8* data, uint8 len);

int CreateClient();
SOCKET ConnectToServer(void);
void DeliverSendOrderIndex();
void DeliverSendOrderIndexISR();
void WaitForSendOrderReady();
void WaitForSendOrderReadyISR();

void UploadVersion();
void ReceiveHttpResponse(void *parameter);
int HttpPostRequest(uint8* data, uint8 len);
int HttpGetRequest(uint8* data, uint8 len);

void SendToClient(uint8 *data, uint8 len);
void CloseClient(SOCKET client);
void SendOrder(uint8 *data, uint8 len);
void GetState(uint8 *data, uint8 len);

///* Local variable */
extern noPollConn *conn;
extern uint8 ctrlid[9];
extern xQueueHandle MasterIdReady;
extern xQueueHandle SendOrderReady;
extern xQueueHandle SceneOrderAmountLimit;
extern xQueueHandle DeviceStateQueue;
extern xQueueHandle OrderQueue;

#endif /* APP_INCLUDE_USER_MASTER_H_ */
