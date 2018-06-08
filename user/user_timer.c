/*
 * user_timer.c
 *
 *  Created on: 2017Äê12ÔÂ28ÈÕ
 *      Author: lv
 */
#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/lwip/sys.h"
#include "lwip/lwip/ip_addr.h"
#include "lwip/lwip/netdb.h"
#include "lwip/lwip/sockets.h"
#include "lwip/lwip/err.h"
#include "user_timer.h"
#include "upgrade.h"

struct user_timer *muser_timer = NULL;

void AddTimer(struct client_conn_param *pclient_param, const char *type, uint8 *period, uint8 *weekday, uint8 *time, uint8 *action, uint8 *actions_time, uint8 *actions_action, uint8 modeidx)
{
	uint8 devkey[token_size] = { 0 };
	char *pbuf = (char*)zalloc(packet_size);
	memcpy(devkey, esp_param.devkey, 40);

	AddTimerIndex(muser_timer, -1, modeidx);

	sprintf(pbuf, CREATE_TIMER_FRAME, type, period, weekday, time, action, actions_time, actions_action, devkey);
	ESP_DBG("%s\n", pbuf);
	write(mclient_param->sock_fd, pbuf, strlen(pbuf));
	free(pbuf);
	vTaskDelay(500 / portTICK_RATE_MS);
	user_master_timer_get(mclient_param, true);
}

void UpdateTimer(struct client_conn_param *pclient_param, const char *type, uint8 *period, uint8 *weekday, uint8 *time, uint8 *action, uint8 *actions_time, uint8 *actions_action)
{
	uint8 devkey[token_size] = { 0 };
	char *pbuf = (char*)zalloc(packet_size);
	memcpy(devkey, esp_param.devkey, 40);

	sprintf(pbuf, UPDATE_TIMER_FRAME, type, period, weekday, time, action, actions_time, actions_action, devkey);
	ESP_DBG("%s\n", pbuf);
	write(mclient_param->sock_fd, pbuf, strlen(pbuf));
	free(pbuf);
	vTaskDelay(500 / portTICK_RATE_MS);
	user_master_timer_get(mclient_param, true);
}

void DeleteTimer(struct client_conn_param *pclient_param, int id)
{
	uint8 devkey[token_size] = { 0 };
	char *pbuf = (char*)zalloc(packet_size);
	memcpy(devkey, esp_param.devkey, 40);

	DeleteTimerIndex(muser_timer, id);

	sprintf(pbuf, DELETE_TIMER_FRAME, id, devkey);
	ESP_DBG("%s\n", pbuf);
	write(mclient_param->sock_fd, pbuf, strlen(pbuf));
	free(pbuf);
	vTaskDelay(500 / portTICK_RATE_MS);
	user_master_timer_get(mclient_param, true);
}

void PrintTimerIndex(struct user_timer *puser_timer)
{
	if(puser_timer == NULL)
	{
		return;
	}
	while(puser_timer != NULL)
	{
		ESP_DBG("id=%d,modeidx=%d\n", puser_timer->id, puser_timer->modeidx);
		puser_timer = puser_timer->next;
		ESP_DBG("print next index\n");
	}
}

void AddTimerIndex(struct user_timer *puser_timer, int id, int modeidx)
{
	struct user_timer *tuser_timer = (struct user_timer*)zalloc(sizeof(struct user_timer));
	tuser_timer->id = id;
	tuser_timer->modeidx = modeidx;
	tuser_timer->next = puser_timer;
	muser_timer = tuser_timer;
	PrintTimerIndex(muser_timer);
}

void UpdateTimerIndex(struct user_timer *puser_timer, int id)
{
	if(puser_timer == NULL)
	{
		ESP_DBG("puser_timer is null\n");
		return;
	}

	struct user_timer *tuser_timer = puser_timer;
	while(tuser_timer != NULL)
	{
		if(tuser_timer->id == -1)
		{
			ESP_DBG("updata id\n");
			tuser_timer->id = id;
			break;
		}
		tuser_timer = tuser_timer->next;
		ESP_DBG("next iterator\n");
	}
	PrintTimerIndex(puser_timer);
}

void DeleteTimerIndex(struct user_timer *puser_timer, int id)
{
	if(puser_timer == NULL)
	{
		return;
	}

	struct user_timer *tuser_timer = puser_timer->next;
	struct user_timer *preuser_timer = puser_timer;
	if(preuser_timer->id == id)
	{
		muser_timer = tuser_timer;
		free(preuser_timer);
		return;
	}
	while(tuser_timer != NULL)
	{
		if(tuser_timer->id == id)
		{
			preuser_timer->next = tuser_timer->next;
			free(tuser_timer);
			break;
		}
		preuser_timer = tuser_timer;
		tuser_timer = tuser_timer->next;
	}
	PrintTimerIndex(puser_timer);
}

int GetTimerId(struct user_timer *puser_timer, int modeidx)
{
	if(puser_timer == NULL)
	{
		return -1;
	}
	//struct user_timer *tuser_timer = puser_timer;
	while(puser_timer != NULL)
	{
		if(puser_timer->modeidx == modeidx)
		{
			return puser_timer->id;
		}
		puser_timer = puser_timer->next;
	}
	return -1;
}

void user_master_timer_get(struct client_conn_param *pclient_param, bool is_humanize_format_simple)
{
	uint8 devkey[token_size] = { 0 };
	char *pbuf = (char*)zalloc(packet_size);
	memcpy(devkey, esp_param.devkey, 40);

	char *humanize_format = is_humanize_format_simple ? "true" : "false";
	sprintf(pbuf, GET_TIMER_FRAME, humanize_format, devkey);
	ESP_DBG("%s\n", pbuf);
	write(mclient_param->sock_fd, pbuf, strlen(pbuf));
	free(pbuf);
}

void user_master_timer_index_build(char *buff)
{
	char *pid = NULL;
	char *pidx = NULL;
	ESP_DBG("index building\n");
	if((pidx = strstr(buff, "\"visibly\": 1")) != NULL && (pidx = strstr(buff, "timers")) != NULL)
	{
		while((pid = strstr(pidx, "\"id\"")) != NULL)
		{
			int id = atoi(pid + 6);
			ESP_DBG("index building find id");
			if((pidx = strstr(pid, "mode")) != NULL)
			{
				int idx = atoi(pidx + 4);
				AddTimerIndex(muser_timer, id, idx);
			}
		}
		PrintTimerIndex(muser_timer);
	}
}

void user_master_timer_index_add(char *buff)
{
	char *pstr = NULL;
	ESP_DBG("index adding\n");
	if((pstr = strstr(buff, "\"visibly\": 1")) != NULL && (pstr = strstr(buff, "timers")) != NULL)
	{
		if((pstr = strstr(pstr, "\"id\"")) != NULL)
		{
			ESP_DBG("index adding find id\n");
			int id = atoi(pstr + 6);
			UpdateTimerIndex(muser_timer, id);
		}
	}
}

void user_master_timer_index_update(char *buff)
{
	if(muser_timer == NULL)
	{
		user_master_timer_index_build(buff);
	}
	else
	{
		user_master_timer_index_add(buff);
	}
}

void user_master_timer_get_task(void *param)
{
	bool humanize_format = *(bool*)param;
	printf("%s", __func__);
    vTaskDelay(500 / portTICK_RATE_MS);
    user_master_timer_get(mclient_param, humanize_format);
	free(param);
	vTaskDelete(NULL);
}
