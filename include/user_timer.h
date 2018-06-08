/*
 * user_timer.h
 *
 *  Created on: 2017Äê12ÔÂ28ÈÕ
 *      Author: lv
 */

#ifndef APP_INCLUDE_USER_TIMER_H_
#define APP_INCLUDE_USER_TIMER_H_

#include "user_esp_platform.h"

#define GET_TIMER_FRAME "{\"body\": {}, \"get\":{\"is_humanize_format_simple\":\"%s\"},\"meta\": {\"Authorization\": \"Token %s\"},\
\"path\": \"/v1/device/timers/\",\"post\":{},\"method\": \"GET\"}\n"

#define CREATE_TIMER_FRAME "{\"body\": {\"timers\":[{\"type\":\"%s\",\"period\":\"%s\",\"weekdays\":%s,\"time\":\"%s\",\"action\":\"%s\",\
\"time_actions\":[{\"time\":\"%s\",\"action\":\"%s\"}]}]}, \"get\":{\"is_humanize_format_simple\":\"true\"},\"meta\": {\"Authorization\": \"Token %s\"},\
\"path\": \"/v1/device/timers/\",\"post\":{},\"method\": \"POST\"}\n"

#define UPDATE_TIMER_FRAME "{\"body\": {\"timers\":[{\"type\":\"%s\",\"period\":\"%s\",\"weekday\":[\"%s\"],\"time\":%s,\"action\":\"%s\",\
\"time_actions\":[{\"time\":\"%s\",\"action\":\"%s\"}]}]}, \"get\":{\"is_humanize_format_simple\":\"true\"},\"meta\": {\"Authorization\": \"Token %s\"},\
\"path\": \"/v1/device/timers/\",\"post\":{},\"method\": \"PUT\"}\n"

#define DELETE_TIMER_FRAME "{\"body\": {\"timers\":[{\"id\":%d}]}, \"get\":{},\"meta\": {\"Authorization\": \"Token %s\"},\
\"path\": \"/v1/device/timers/\",\"post\":{\"is_humanize_format_simple\":\"true\"},\"method\": \"DELETE\"}\n"

struct user_timer
{
	int id;
	int modeidx;
	struct user_timer *next;
};

extern struct user_timer *muser_timer;
extern struct client_conn_param *mclient_param;
extern struct esp_platform_saved_param esp_param;

void AddTimer(struct client_conn_param *pclient_param, const char *type, uint8 *period, uint8 *weekday, uint8 *time, uint8 *action, uint8 *actions_time, uint8 *actions_action, uint8 modeidx);
void UpdateTimer(struct client_conn_param *pclient_param, const char *type, uint8 *period, uint8 *weekday, uint8 *time, uint8 *action, uint8 *actions_time, uint8 *actions_action);
void DeleteTimer(struct client_conn_param *pclient_param, int id);

void AddTimerIndex(struct user_timer *puser_timer, int id, int modeidx);
void UpdateTimerIndex(struct user_timer *puser_timer, int id);
void DeleteTimerIndex(struct user_timer *puser_timer, int id);
int GetTimerId(struct user_timer *puser_timer, int modeidx);

void user_master_timer_get(struct client_conn_param *pclient_param, bool is_humanize_format_simple);
void user_master_timer_index_update(char *buff);

void user_master_timer_get_task(void *param);

#endif /* APP_INCLUDE_USER_TIMER_H_ */
