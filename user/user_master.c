/*
 * user_master.c
 *
 *  Created on: 2017��10��17��
 *      Author: lv
 */

#include "user_master.h"
#include "user_timer.h"

/* Local variable */
static SOCKET client_conn[MAX_CONN];
static SOCKET sta_socket = INVALID_SOCKET;
static int client_num = 0;
uint8 ctrlid[9]={0};
uint8 modectl[MODE_NUM];
LOCAL xQueueHandle CliQueueStop = NULL;
LOCAL bool ClientStatus;
xQueueHandle DeviceStateQueue = NULL;
xQueueHandle OrderQueue = NULL;
xQueueHandle OrderMutexQueue = NULL;
static uint8 retrytimes = 0;
static bool offline[100]={false};
xTaskHandle SceneOrderHandle;

void DeliverSendOrderIndex()
{
	bool sendSignal = true;
	portBASE_TYPE xStatus;
	xStatus = xQueueSend(SendOrderReady, &sendSignal, 100/portTICK_RATE_MS);
	if(xStatus == pdPASS)
	{
		MASTER_DBG("deliver send order success\n");
	}
}

void DeliverSendOrderIndexISR()
{
	bool sendSignal = true;
	portBASE_TYPE xStatus;
	portBASE_TYPE pxHighPriority = pdTRUE;
	xStatus = xQueueSendFromISR(SendOrderReady, &sendSignal, &pxHighPriority);
	if(xStatus == pdPASS)
		printf("deliver send order from isr success\n");
}

void WaitForSendOrderReady()
{
	MASTER_DBG("wait for send order ready...\n");
	bool recvSignal = false;
	portBASE_TYPE xStatus;
	xStatus = xQueueReceive(SendOrderReady, &recvSignal, 100/portTICK_RATE_MS);//portMAX_DELAY
	if(xStatus == pdPASS && recvSignal)
	{
		MASTER_DBG("send order ready!\n");
	}
	else
	{
		MASTER_DBG("wait for send order failed!\n");
	}
}

void WaitForSendOrderReadyISR()
{
	printf("wait for send order ready...\n");
	bool recvSignal = false;
	portBASE_TYPE xStatus;
	portBASE_TYPE pxHighPriority = pdTRUE;
	do
	{
		xStatus = xQueueReceiveFromISR(SendOrderReady, &recvSignal, &pxHighPriority);//portMAX_DELAY
		if(xStatus == pdPASS && recvSignal)
			printf("send order ready!\n");
		else
			printf("wait for send order failed!\n");
	}while(xStatus != pdPASS);
}

int CreateClient()
{
	if(CliQueueStop == NULL)
		CliQueueStop = xQueueCreate(1,1);

	if(CliQueueStop != NULL)
		xTaskCreate(TCPClient, "tsk1", 512, NULL, 2, NULL);
}
/*************************************************************************************************
 * function: Create a TCP client connected to remote server to send the eletricstate and get order
 *       ip: 101.201.211.87
 *     port: 8080
 ************************************************************************************************/
void TCPClient(void *pvParameters){
	int ret;
	uint8 recvbytes;
	uint8 dns_retry_counts = 0;
	char *pbuf,*recv_buf,*p;
	struct sockaddr_in remote_ip;
	struct hostent *ahostent = NULL;
	struct ip_addr server_ip;
	xTaskHandle ProDataHandle;

	do
	{
		ahostent = gethostbyname(REMOTE_DOMAIN);
		if(ahostent == NULL){
			vTaskDelay(500/portTICK_RATE_MS);
		}else{
		    printf("Get DNS OK!\n");
		    break;
		}
	}while(dns_retry_counts < 20);

	if(ahostent == NULL)
	{
		printf("ERROR: failed to get DNS, rebooting...\n");
	}
	else
	{
		if(ahostent->h_length <= 4)
		{
			memcpy(&server_ip, (char*)(ahostent->h_addr_list[0]), ahostent->h_length);
			printf("ESP_DOMAIN IP address: %s\n", inet_ntoa(server_ip));
		}
		else
		{
			os_printf("ERR:arr_overflow,%u,%d\n",__LINE__, ahostent->h_length );
		}
	}

	bzero(&remote_ip, sizeof(remote_ip));
	remote_ip.sin_family = AF_INET; /* Internet address family */
	remote_ip.sin_addr.s_addr = inet_addr(REMOTE_IP); /* Any incoming interface */
	remote_ip.sin_len = sizeof(remote_ip);
	remote_ip.sin_port = htons(REMOTE_PORT); /* Remote server port */

	while(1){
		if(retrytimes++ > 20)
		{
			printf("Restart to connect to the Server\n");
			system_restart();
		}
		/* Create socket*/
		sta_socket = socket(PF_INET, SOCK_STREAM, 0);
		if(sta_socket == INVALID_SOCKET){
			close(sta_socket);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
#ifdef DEBUG
			printf("ESP8266 TCP client task > socket error\n");
#endif
		}
#ifdef DEBUG
		printf("ESP8266 TCP client task > socket %d success!\n",sta_socket);
#endif

		/* Connect to remote server*/
		ret = connect(sta_socket, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr));
		if(0 != ret){
			close(sta_socket);
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
#ifdef DEBUG
			printf("ESP8266 TCP client task > connect fail!\n");
#endif
		}
#ifdef DEBUG
		printf("ESP8266 TCP client task > connect ok!\n");
#endif
		retrytimes = 0;

		//printf("ESP8266 TCP client task > connect ok!\n");

		//Create a task to process the data received from the server
		xTaskCreate(ProcessData, "ProcessData", 512, &sta_socket, 2, &ProDataHandle);

	while(1){
		//send order to get the mastercode
		if(!ctrlid[0])
		{
			printf("<00000000U00000000000FF>");
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}

		vTaskDelay(1000 / portTICK_RATE_MS);
		bool state = false;
		portBASE_TYPE xStatus = xQueueReceive(CliQueueStop, &state, 0);

		//disconnect after receiving signal and try to reconnect
		if(xStatus == pdPASS && state)
		{
			if(sta_socket != INVALID_SOCKET)
			{
				close(sta_socket);
				sta_socket = INVALID_SOCKET;
			}
			printf("WARNING: disconnected to Server, reconnecting...\n");
			break;
		}

		/* send http get request to remote server */
		HttpGetRequest(ctrlid, 9);

		//get free heap size, restart if there is no enough space
		int heap_size = system_get_free_heap_size();
		if(heap_size < 5000)
		{
			printf("WARNING: No enough heap space, rebooting...\n");
			system_restart();
		}
		vTaskDelay(2000 / portTICK_RATE_MS);

#ifdef DEBUG
		printf("TCPClient task stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),heap_size);
		printf("ESP8266 TCP client task > send socket %d success!\n",sta_socket);
#endif

	}//send get order
	//break;
	}
	vTaskDelete(NULL);
}

/***************************************************************************************************************
 *    function: Process the uart data, send to remote server through TCP client, send to user through TCP server
 * description: poll the uart rxbuf every 100 ms to see if there is valid data
 **************************************************************************************************************/
void UartProcess(void *pvParameters) {
	/* send the data to tcp client if the rxbuf is not empty */
	MASTER_DBG("Welcome to send uart data task!\n");

	uint8 i,orderidx,update;
	uint8 *buff = (uint8*)zalloc(100);
	uint8 *TempRxBuff = (uint8*)zalloc(MAX_RX_LEN);
	uint8 TempRxLen = 0;;
	int32 len;
	SOCKET client_sock;
	while (1) {
		update = 0;
		if (stringlen) {
			memcpy(TempRxBuff, rxbuf, stringlen);
			TempRxLen = stringlen;
			stringlen = 0;

			MASTER_DBG("Uart task > stringlen = %d\n",TempRxLen);
			MASTER_DBG("Uart task > received data:%s\n", TempRxBuff);

			//mastercode, save it
			if(TempRxBuff[0] == '#' && TempRxBuff[9] == 'U'){
				memcpy(ctrlid, TempRxBuff+1, 8);
			}

			//the address of end device, save or update the address table, the newly used long addr is 4 bytes longer than the old short addr
			if(TempRxBuff[0] == '#' && strcspn(TempRxBuff,"Y")==13){
				updateaddr(TempRxBuff);
				update = 4;
			}

			//the state of end devices
			if(TempRxBuff[0] == '<' && strcspn(TempRxBuff,">") > 24){
				GetState(TempRxBuff, TempRxLen);
				update = 4;
				if(TempRxBuff[14] == 'O')
				{
					updateaddr(TempRxBuff);
					memset(TempRxBuff, 0, 100);
					TempRxLen = 0;
					continue;
				}
			}

			//send the message to the local client
			SendToClient(TempRxBuff, TempRxLen);

			//printf("connect to server\n");
			//sta_socket = ConnectToServer();

			//send the message to remote server
			if(TempRxBuff[0] == '<' && sta_socket != INVALID_SOCKET){
				MASTER_DBG("Uart task > send message to server %d\n", sta_socket);

				//the basic upload bytes = 2(<>) + 8(default short device addr) + 2(state)
				uint8 UploadBytes = 12;

				//the variable update is used to distinguish the long addr and the short addr
				if(update)
					UploadBytes += 4;

				//if the device type is NEW_LOCK(e.g."10"), upload 3 more bytes
				//if the device type is AIR_CONDITION(e.g."11"),upload all the data except the last 2 bytes before '>'
				if(TempRxBuff[1] == '1')
				{
					if(TempRxBuff[2] == '0')
					{
						UploadBytes += 3;
					}
					else if(TempRxBuff[2] == '1')
					{
						uint8* end = FindEnd(TempRxBuff, TempRxLen);
						if(end != NULL)
						{
							UploadBytes = end - TempRxBuff - 3;
						}
					}
				}

				memset(buff, 0, 100);
				memcpy(buff, ctrlid, 8);
				memcpy(buff + 8, TempRxBuff+1, UploadBytes);

				//upload the state by http post
				HttpPostRequest(buff,strlen(buff));
			}
#ifdef DEBUG
			printf("Uart task > send complete!\n");
#endif
			//if the message comes from modectrl do the next procedure
			if(TempRxBuff[0] == '<' && TempRxBuff[2] == 'A'){
				if(update)
					convertaddr(TempRxBuff);
				orderidx = atoi(TempRxBuff+10);
				uint8 *modeidx = (uint8*)zalloc(sizeof(uint8));
				*modeidx = orderidx;
				xTaskCreate(SceneOrder, "SceneOrder", 256, modeidx, 2, NULL);
			}

			//if the message comes from sensors do the nect procedure
			if(TempRxBuff[0] == '<' && TempRxBuff[2] == 'D'){
				orderidx = atoi(TempRxBuff + 17);
				ProcessSensor(TempRxBuff, orderidx);
				WaitForSendOrderReady();
			}

			memset(TempRxBuff, 0, MAX_RX_LEN);

#ifdef DEBUG
			printf("uart process stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
			system_print_meminfo();
#endif
		}
		vTaskDelay(100 / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}

/* Create a UDP server for application to search the ip address and MAC address */
void UDPServer(void *pvParameters) {
	LOCAL uint32 sock_fd;
	struct sockaddr_in server_addr, from;
	struct ip_info info;
	int ret, nNetTimeout;
	char *udp_msg = (char *) zalloc(DATA_LEN);
	uint8 *addr = (uint8 *) zalloc(4);
	uint8 opmode;
	socklen_t fromlen;
#ifdef DEBUG
	printf("Hello, welcome to UDPtask!\r\n");
#endif
	//wifi_station_scan(NULL,scan_done);
	//printf(rxbuf);

	/* create socket */
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(UDP_LOCAL_PORT);
	server_addr.sin_len = sizeof(server_addr);

	do {
		sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_fd == -1) {
#ifdef DEBUG
			printf("ESP8266 UDP task > failed to create socket!\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (sock_fd == -1);
#ifdef DEBUG
	printf("ESP8266 UDP task > create socket OK!\n");
#endif

	/* bind socket */
	do {
		ret = bind(sock_fd, (struct sockaddr * )&server_addr,
				sizeof(server_addr));
		if (ret != 0) {
#ifdef DEBUG
			printf("ESP8266 UDP task > captdns_task failed to bind socket\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (ret != 0);
#ifdef DEBUG
	printf("ESP8266 UDP task > bind OK!\n");
#endif

	/* receive and send UDP data */
	while (1) {
		//printf("UDPServer stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());

		memset(udp_msg, 0, DATA_LEN);
		memset(&from, 0, sizeof(from));

		setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char * )&nNetTimeout,
				sizeof(int));
		fromlen = sizeof(struct sockaddr_in);
		ret = recvfrom(sock_fd, (uint8 * )udp_msg, DATA_LEN, 0,
				(struct sockaddr * )&from, (socklen_t* )&fromlen);
		if (ret > 0) {
#ifdef DEBUG
			printf("ESP8266 UDP task > recv %d Bytes from %s ,Port %d\n", ret, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
#endif
			if (!strcmp(udp_msg, UDP_STRING)) {
				opmode = wifi_get_opmode();
				switch (opmode) {
				case SOFTAP_MODE:
					wifi_get_ip_info(0x01, &info);
					break;
				case STATION_MODE:
					if (wifi_station_get_connect_status() == STATION_GOT_IP)
						wifi_get_ip_info(0x00, &info);
					break;
				case STATIONAP_MODE:
					if (wifi_station_get_connect_status() == STATION_GOT_IP)
						wifi_get_ip_info(0x00, &info);
					else
						wifi_get_ip_info(0x01, &info);
					break;
				}
				if (&info != NULL) {
					addr = (uint8*) &(info.ip.addr);
					memset(udp_msg, 0, DATA_LEN);
					//sprintf(udp_msg, "%d.%d.%d.%d,ACCF23635DAC,", addr[0],
					//		addr[1], addr[2], addr[3]);
					sprintf(udp_msg, "%d.%d.%d.%d,%s,", addr[0],
							addr[1], addr[2], addr[3],ctrlid);
#ifdef DEBUG
					printf("got ip addr!\n");
					printf("ip:%s\n",(uint8*)udp_msg);
					printf("stringlen=%d\n",stringlen);
#endif
					sendto(sock_fd, (uint8* )udp_msg, strlen(udp_msg), 0,
							(struct sockaddr * )&from, fromlen);
				}
			}
		}
	}

	if (udp_msg) {
		free(udp_msg);
		udp_msg = NULL;
	}
	close(sock_fd);

	vTaskDelete(NULL);
}

/* Create a TCP server for application to connect for communication */
void TCPServer(void *pvParameters) {
	int32 listenfd;
	int32 ret;
	//int32 client_sock;
	struct sockaddr_in server_addr;
	struct sockaddr_in remote_addr;
	int recbytes, stack_counter = 0;

	/* Construct local address structure */
	memset(&server_addr, 0, sizeof(server_addr)); /* Zero out structure */
	server_addr.sin_family = AF_INET; /* Internet address family */
	server_addr.sin_addr.s_addr = INADDR_ANY; /* Any incoming interface */
	server_addr.sin_len = sizeof(server_addr);
	server_addr.sin_port = htons(SERVER_PORT); /* Local port */

	/* Create socket for incoming connections */
	do {
		listenfd = socket(AF_INET, SOCK_STREAM, 0);
		if (listenfd == -1) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > socket error\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (listenfd == -1);

#ifdef DEBUG
	printf("ESP8266 TCP server task > create socket: %d\n", listenfd);
#endif
	/* Bind to the local port */
	do {
		ret = bind(listenfd, (struct sockaddr * )&server_addr,
				sizeof(server_addr));
		if (ret != 0) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > bind fail\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (ret != 0);
#ifdef DEBUG
	printf("ESP8266 TCP server task > port:%d\n",ntohs(server_addr.sin_port));
#endif

	do {
		/* Listen to the local connection */
		ret = listen(listenfd, MAX_CONN);
		if (ret != 0) {
#ifdef DEBUG
			printf("ESP8266 TCP server task > failed to set listen queue!\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
		}
	} while (ret != 0);
#ifdef DEBUG
	printf("ESP8266 TCP server task > listen ok:%d\n", listenfd);
#endif

	int32 len = sizeof(struct sockaddr_in);
	while (1) {
#ifdef DEBUG
		printf("ESP8266 TCP server task > wait client\n");
#endif

		/*block here waiting local connect request*/
		SOCKET client_sock = accept(listenfd, (struct sockaddr * )&remote_addr,(socklen_t * )&len);
		if(client_sock < 0)
		{
#ifdef DEBUG
			printf("ESP8266 TCP server task > accept fail\n");
#endif
			vTaskDelay(1000 / portTICK_RATE_MS);
			continue;
		}
#ifdef DEBUG
		printf("ESP8266 TCP server task > client num:%d\n",client_num);
		printf("ESP8266 TCP server task > Client from %s:%d client_sock:%d\n",inet_ntoa(remote_addr.sin_addr), htons(remote_addr.sin_port), client_sock);
#endif
		if(client_num < MAX_CONN + 2){
			client_conn[client_num++] = client_sock;
			//printf("TCPServer stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
		}
		else
		{
			close(client_sock);
			/*SOCKET first_sock = client_conn[0];
			uint8 i;
			for(i = 0; i < MAX_CONN - 1; i++)
			{
				client_conn[i] = client_conn[i+1];
			}
			client_conn[MAX_CONN - 1] = client_sock;
			close(first_sock);*/
		}
	}
	vTaskDelete(NULL);
}

/* Create a task to accept client, but the job finally done in TCPServer task */
void WaitClient(void *pvParameters){
	int ret;
	int32 len = sizeof(struct sockaddr_in);
	SOCKET cliconn,listenfd;
	struct sockaddr_in remote_addr;
#ifdef DEBUG
	printf("waiting for client...\nlistenfd:%d\n", listenfd);
#endif
	while(1){
		if(client_num < MAX_CONN){
#ifdef DEBUG
			printf("accepting...\n");
#endif
			cliconn = accept(listenfd, (struct sockaddr *)&remote_addr, (socklen_t *)&len );
			if(cliconn < 0){
				printf("accept failed!\n");
			}
			else{
				printf("accept ok!!!cliconn:%d,ip:%s,port:%d\n",cliconn, inet_ntoa(remote_addr.sin_addr),htons(remote_addr.sin_port));
				client_conn[client_num++] = cliconn;
				xTaskCreate(RecvData, "RecvData", 256, &cliconn, 2, NULL);
			}
		}
		else{
			printf("connection full!\n");
		}
	}
	vTaskDelete(NULL);
}

void RecvData(void *pvParameters){
#ifdef DEBUG
	printf("ESP8266 RecvData task > reading data...\n");
#endif
	int ret,i,recvbytes;
	uint8 orderidx;
	SOCKET cliconn = *(SOCKET*)pvParameters;
	free(pvParameters);

	while(1){
#ifdef DEBUG
		if(client_num)
			printf("ESP8266 RecvData task > client num:%d\n", client_num);
		printf("ESP8266 RecvData task > cliconn:%d\n",cliconn);
#endif
			if(cliconn){
				char *recv_buf = (char *)zalloc(DATA_LEN);
				recvbytes = read(cliconn, recv_buf, DATA_LEN);
				if(recvbytes > 0){
					if(recvbytes > 20 && recv_buf[0]=='<')
					{
						recv_buf[recvbytes] = 0;
						if(recv_buf[1]=='<'){
							spi_flash_erase_sector(ADDR_TABLE_START + 3);
							spi_flash_write((ADDR_TABLE_START + 3) * SPI_FLASH_SEC_SIZE, (uint32*)recv_buf, 64);
							sprintf(recv_buf, "set wifi ok!\n");
							printf(recv_buf);
							send(cliconn, recv_buf, strlen(recv_buf), 0);
							system_restart();
						}
						else if(recv_buf[9] == 'T'){
							orderidx = atoi(recv_buf+13);//mode control order index is 13
							if(recv_buf[10] == 'H'){
								if(orderidx >= 0 && orderidx < MODE_NUM && !modectl[orderidx]){
									uint8* modeidx = (uint8*)zalloc(sizeof(uint8));
									*modeidx = orderidx;
									xTaskCreate(SceneOrder, "SceneOrder", 256, modeidx, 3, &SceneOrderHandle);
								}
							}
							if(recv_buf[10] == 'G'){//disable mode
								modectl[orderidx]=1;
							}
							if(recv_buf[10] == 'S'){//enable mode
								modectl[orderidx]=0;
							}
							if(recv_buf[10] == 'R'){
								if(orderidx >= 0 && orderidx < MODE_NUM )
									spi_flash_erase_sector(SPI_FLASH_START - orderidx);
							}
							if(recv_buf[10] == 'T'){//just for debug
								spi_flash_read((SPI_FLASH_START - orderidx) * SPI_FLASH_SEC_SIZE,(uint32*)recv_buf,128);
								recv_buf[127]=0;
								for(i=0;i<128;i++)
									printf("%c",recv_buf[i]);
							}
							if(recv_buf[10] == 'U'){//just for debug
								spi_flash_read((ADDR_TABLE_START + orderidx) * SPI_FLASH_SEC_SIZE,(uint32*)recv_buf,128);
								recv_buf[127]=0;
								for(i=0;i<128;i++)
									printf("%c",recv_buf[i]);
							}
						}
						else{
							if(strcspn(recv_buf,"X") == 13)
								convertaddr(recv_buf);
							printf(recv_buf);
						}

						MASTER_DBG("RecvData task stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
						//system_print_meminfo();
					}
#ifdef DEBUG
					printf("ESP8266 RecvData task > read %d bytes success:%s\n", recvbytes, recv_buf);
					//send(cliconn, recv_buf, strlen(recv_buf), 0);
#endif
				}
				else if(recvbytes == 0){
					MASTER_DBG("ESP8266 RecvData task > end of file socket %d\n", cliconn);
					for(i=0; i < client_num; i++)
						if(cliconn == client_conn[i])
							break;
					if(i == client_num)
					{
						MASTER_DBG("ESP8266 RecvData task > error:connection not found!\n");
					}
					else if(i < client_num-1)
						for( ; i < client_num; i++)
							client_conn[i] = client_conn[i+1];
					closesocket(cliconn);
					client_num--;
					break;
				}
				else{
					MASTER_DBG("ESP8266 RecvData task > socket disconnected!\n");
					for(i=0; i < client_num; i++)
						if(cliconn == client_conn[i])
							break;
					if(i == client_num)
					{
						MASTER_DBG("ESP8266 RecvData task > error:connection not found!\n");
					}
					else if(i < client_num-1)
						for( ; i < client_num; i++)
							client_conn[i] = client_conn[i+1];
					closesocket(cliconn);
					client_num--;
					break;
				}
				free(recv_buf);
			}
			else{
				MASTER_DBG("ESP8266 RecvData task > connection error!\n");
				closesocket(cliconn);
				client_num--;
				for( ; i < client_num; i++)
					client_conn[i] = client_conn[i+1];
				break;
			}
	}
#ifdef DEBUG
	printf("ESP8266 RecvData task > end of reading\n");
#endif
	vTaskDelete(NULL);
}

/* Create a task to process the data from remote server */
void ProcessData(void *pvParameters){
#ifdef DEBUG
	printf("processing data...\n");
#endif
	int recvbytes;
	uint8 orderidx;
	uint8 *recv_buf,*p,orderlen,bodylen;
	uint8 *head;
	uint8 *end;
	SOCKET cliconn = *(SOCKET*)pvParameters;
	while(1)
	{
		recvbytes = 0;
		recv_buf = (char*)zalloc(400);
		memset(recv_buf, 0, 400);
		if((recvbytes = read(sta_socket, recv_buf, 400)) > 0)
		{
#ifdef DEBUG
			recv_buf[recvbytes] = 0;
			printf("ESP8266 TCP client task > recv data %d bytes!\nESP8266 TCP client task > %s\n", recvbytes, recv_buf);
#endif
			p = strstr(recv_buf, "\r\n\r\n");
			if(p != NULL && p[4] == '{')
			{
				end = strchr(p+4, '}');
				if(end == NULL)
				{
					printf("failed to find body end\n");
					free(recv_buf);
					continue;
				}
				bodylen = end - p - 4;

				//here, end is used to mark the end of the processed order, yet no order is processed, let end equals to p + 4,the beginning of the orders
				end = p + 4;

				while(bodylen > 18)
				{
					//connect to server and receive data success,reset retry times
					retrytimes = 0;

					//recv_buf[recvbytes] = 0;
					//printf(recv_buf);

					head = strchr(end + 1, '<');
					if(head != NULL)
					{
						end = strchr(head, '>');

						if(end == NULL)
							break;

						orderlen = end - head + 1;
					}
					else
					{
						printf("failed to find order end\n");
						break;
					}
#ifdef DEBUG
					printf("orderlen=%d\n",orderlen);
#endif
					if(ProcessOrder(head, orderlen) == -1)
					{
						break;
					}
					bodylen -= orderlen;
				}
			}
			else
			{
				//printf("failded to find body head\n");
			}
		}
		free(recv_buf);

		if(recvbytes <= 0){
#ifdef DEBUG
			printf("ESP8266 TCP client task > read data fail!\n");
			printf("recvbytes=%d\n",recvbytes);
#endif
			ClientStatus = true;
			xQueueSend(CliQueueStop, &ClientStatus, 0);
			close(sta_socket);
			sta_socket = INVALID_SOCKET;
			break;
		}

		//printf("ProcessData task stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
	}
	vTaskDelete(NULL);
}

uint8* FindHead(uint8* data, uint8 len)
{
	if(data == NULL)
		return NULL;

	uint8 i;
	for(i = 0; i < len; i++)
	{
		if(data[i] == ORDER_HEAD)
			return data + i;
	}
	return NULL;
}

uint8* FindEnd(uint8* data, uint8 len)
{
	if(data == NULL)
		return NULL;

	uint8 i;
	for(i = 0; i < len; i++)
	{
		if(data[i] == ORDER_END)
			return data + i;
	}
	return NULL;
}

/* Create a task to send mode order */
void SceneOrder(void *pvParameters){
	uint8 orderidx = *(uint8*)pvParameters;
	free(pvParameters);
	portBASE_TYPE xStatus;
	bool value = true;
	xStatus = xQueueSend(SceneOrderAmountLimit, &value, 0);

	const portTickType WaitStateTimeout = 500 / portTICK_RATE_MS;
	uint8 *qStateBuffer = (uint8*)zalloc(DATA_LEN);

	if(xStatus != pdPASS)
	{
		//printf("delete scene order task\n");
		free(qStateBuffer);
		vTaskDelete(NULL);
		return;
	}
	else
	{
		//printf("enter critical section\n");
	}

	if(orderidx >= MODE_NUM)
	{
		xQueueReceive(SceneOrderAmountLimit, &value, 0);
		//printf("order index overflow, delete scene order task\n");
		free(qStateBuffer);
		vTaskDelete(NULL);
		return;
	}

	uint8 i;
	const uint8 BufferLen = DATA_LEN;
	uint8 *orderbuffer = (uint8*)zalloc(BufferLen * sizeof(uint8));
	uint32 StartAddr = (SPI_FLASH_START - orderidx) * SPI_FLASH_SEC_SIZE;
	uint32 ReadAddr = StartAddr;

	spi_flash_read(ReadAddr, (uint32*)orderbuffer, BufferLen);

	uint8 addrLen = 0;
	uint8 *head = NULL;

	uint8 stateFlag[ORDER_NUM] = { 0 };
	uint8 ordersqe = 0;

	//step1
	while((head = FindHead(orderbuffer, DATA_LEN)) != NULL)
	{
		addrLen = strlen(head);
		convertaddr(head);
		head[addrLen - 4] = '\0';

		//wait here till OrderQueue is empty, prevent sending the clogged orders quickly
//		while(!IsQueueEmpty(OrderQueue))
//			vTaskDelay(500 / portTICK_RATE_MS);

		vTaskDelay(500 / portTICK_RATE_MS);
		//SendOrder(head, addrLen - 4);
		printf(head);

		//clear the DeviceStateQueue in case there is data already exist(tough the device manually)
		ClearQueue(DeviceStateQueue);

		uint8 ret = ReadQueue(DeviceStateQueue, qStateBuffer, WaitStateTimeout);
		if(ret == 0)
		{
			stateFlag[ordersqe] = 1;
		}

		MASTER_DBG("SendOrderTask > device state %s\n", qStateBuffer);

		//the order has been converted to short address, convert the device state if long address
		if(qStateBuffer[ORDER_OPTS_POS_NEW] == 'Z')
		{
			convertaddr(qStateBuffer);
			if(memcmp(qStateBuffer, head, ORDER_OPTS_POS_OLD))
			{
				stateFlag[ordersqe] = 1;
			}
		}
		else
		{
			stateFlag[ordersqe] = 1;
		}

		ordersqe++;

		addrLen = head - orderbuffer + addrLen + 1;
		ReadAddr = (ReadAddr + addrLen) / 4 * 4;
		spi_flash_read(ReadAddr, (uint32*)orderbuffer, BufferLen);
	}

	//step2: resend the order without feedback
	ordersqe = 0;
	ReadAddr = StartAddr;
	spi_flash_read(ReadAddr, (uint32*)orderbuffer, BufferLen);
	while((head = FindHead(orderbuffer, DATA_LEN)) != NULL)
	{
		addrLen = strlen(head);
		if(stateFlag[ordersqe])
		{
		convertaddr(head);
		head[addrLen - 4] = '\0';

		vTaskDelay(500 / portTICK_RATE_MS);
		printf(head);

		//clear the DeviceStateQueue in case there is data already exist(tough the device manually)
		ClearQueue(DeviceStateQueue);

		uint8 ret = ReadQueue(DeviceStateQueue, qStateBuffer, WaitStateTimeout);
		if(ret == 0)
		{
			stateFlag[ordersqe] = 1;
		}

		MASTER_DBG("SendOrderTask > device state %s\n", qStateBuffer);

		//the order has been converted to short address, convert the device state if long address
		if(qStateBuffer[ORDER_OPTS_POS_NEW] == 'Z')
		{
			convertaddr(qStateBuffer);
			if(memcmp(qStateBuffer, head, ORDER_OPTS_POS_OLD))
			{
				stateFlag[ordersqe] = 1;
			}
			else
			{
				stateFlag[ordersqe] = 0;
			}
		}
		else
		{
			stateFlag[ordersqe] = 1;
		}
		}

		ordersqe++;

		addrLen = head - orderbuffer + addrLen + 1;
		ReadAddr = (ReadAddr + addrLen) / 4 * 4;
		spi_flash_read(ReadAddr, (uint32*)orderbuffer, BufferLen);
	}

	//step3: resend for the second time
	ordersqe = 0;
	ReadAddr = StartAddr;
	spi_flash_read(ReadAddr, (uint32*)orderbuffer, BufferLen);
	while((head = FindHead(orderbuffer, DATA_LEN)) != NULL)
	{
		addrLen = strlen(head);
		if(stateFlag[ordersqe])
		{
		convertaddr(head);
		head[addrLen - 4] = '\0';

		vTaskDelay(500 / portTICK_RATE_MS);
		printf(head);

		//clear the DeviceStateQueue in case there is data already exist(tough the device manually)
		ClearQueue(DeviceStateQueue);

		uint8 ret = ReadQueue(DeviceStateQueue, qStateBuffer, WaitStateTimeout);
		if(ret == 0)
		{
			stateFlag[ordersqe] = 1;
		}

		MASTER_DBG("SendOrderTask > device state %s\n", qStateBuffer);

		//the order has been converted to short address, convert the device state if long address
		if(qStateBuffer[ORDER_OPTS_POS_NEW] == 'Z')
		{
			convertaddr(qStateBuffer);
			if(memcmp(qStateBuffer, head, ORDER_OPTS_POS_OLD))
			{
				stateFlag[ordersqe] = 1;
			}
			else
			{
				stateFlag[ordersqe] = 0;
			}
		}
		else
		{
			stateFlag[ordersqe] = 1;
		}
		}

		ordersqe++;

		addrLen = head - orderbuffer + addrLen + 1;
		ReadAddr = (ReadAddr + addrLen) / 4 * 4;
		spi_flash_read(ReadAddr, (uint32*)orderbuffer, BufferLen);
	}

	free(qStateBuffer);
	free(orderbuffer);
	xStatus = xQueueReceive(SceneOrderAmountLimit, &value, 0);
	if(xStatus == pdPASS)
	{
		//printf("leave critical section\n");
	}
	vTaskDelete(NULL);
}

/* Create a task to check whether the devices are online */
void CheckOnline(void *pvParameters){
	uint32 addr,info;
	uint8 addrnum,rpage,i,j;
	uint8 order[25];
	sprintf(order,"<0*00****XO**********FF>");
	while(1){
		spi_flash_read((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, &info, 4);
		addrnum = info & 0xff;
		if(addrnum > 100)
			addrnum = 0;
		//printf("addrnum:%d\n", addrnum);
		rpage = ((info >> 8) & 0xff) % 2;
		for(i=0; i<addrnum; i++){
			spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * i + 8, &addr, 4);
			memcpy(order+5, (uint8*)&addr, 4);
			if(!offline[i]){
				offline[i] = true;
				for(j=0; j<3; j++){
					if(offline[i]){
						printf(order);
						vTaskDelay(1000 / portTICK_RATE_MS);
					}
					else
						break;
				}
			}
		}
		vTaskDelay(15000 / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}

/* wifi event handle function */
void wifi_handle_event_cb(System_Event_t *evt) {
	//printf("event %x\n", evt->event_id);
	switch (evt->event_id) {
	case EVENT_STAMODE_CONNECTED:
#ifdef DEBUG
		printf("connect to ssid %s, channel %d\n",
				evt->event_info.connected.ssid,
				evt->event_info.connected.channel);
#endif
		break;
	case EVENT_STAMODE_DISCONNECTED:
#ifdef DEBUG
		printf("disconnect from ssid %s, reason %d\n",
				evt->event_info.disconnected.ssid,
				evt->event_info.disconnected.reason);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 1);		//outout 1,turn off led
		break;
	case EVENT_STAMODE_AUTHMODE_CHANGE:
#ifdef DEBUG
		printf("mode: %d -> %d\n", evt->event_info.auth_change.old_mode,
				evt->event_info.auth_change.new_mode);
#endif
		break;
	case EVENT_STAMODE_GOT_IP:
#ifdef DEBUG
		printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
				IP2STR(&evt->event_info.got_ip.ip),
				IP2STR(&evt->event_info.got_ip.mask),
				IP2STR(&evt->event_info.got_ip.gw));
		printf("\n");
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 0);		//output 0,turn on led
#if ESP_PLATFORM
		user_esp_platform_init();
#endif
		CreateClient();
#ifdef WEBSOCKET
		//websocket_start(NULL);
#endif
		break;
	case EVENT_SOFTAPMODE_STACONNECTED:
#ifdef DEBUG
		printf("station: " MACSTR "join, AID = %d\n",
				MAC2STR(evt->event_info.sta_connected.mac),
				evt->event_info.sta_connected.aid);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 0);		//outout 0,turn on led
		break;
	case EVENT_SOFTAPMODE_STADISCONNECTED:
#ifdef DEBUG
		printf("station: " MACSTR "leave, AID = %d\n",
				MAC2STR(evt->event_info.sta_disconnected.mac),
				evt->event_info.sta_disconnected.aid);
#endif
		GPIO_OUTPUT(GPIO_Pin_4, 1);		//outout 1,turn off led
		break;
	default:
		break;
	}
}

void updateaddr(uint8 *buff){
	uint32 info[2],addr[3];//Extaddr+shortaddr
	uint8 addrnum,rpage,wpage,i,j,updated=0;
	spi_flash_read((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, info, 8);
	if(info[0] == 0xffffffff)//which page to read and the total number of address
		info[0] = 0;
	if(info[1] == 0xffffffff)//times the address have changed
		info[1] = 0;
	addrnum = info[0] & 0xff;
	if(addrnum > 200 && addrnum != 255)
		SendToClient("Device full\n", strlen("Device full\n"));
	if(addrnum > 200)
		addrnum = 0;
	rpage = ((info[0] >> 8) & 0xff) % 2;
	wpage = (rpage + 1)%2;
	for(i=0; i<addrnum; i++){
		spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * i, addr, 12);
		if( !strncmp((uint8*)addr, buff+5,8) ){
			offline[i] = false;
			if( strncmp((uint8*)&addr[2], buff+15, 4) )//extaddr is the same while short addr is not,update it
				updated = 1;
			break;
		}
	}
	if( (i<addrnum && updated) ||  i == addrnum){
		if(i==addrnum)
			addrnum++;
		spi_flash_erase_sector(ADDR_TABLE_START + wpage);
		for(j=0; j< addrnum; j++){
			spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * j, addr, 12);
			if(j == i){
				strncpy((uint8*)&addr[0], buff+5, 8);
				strncpy((uint8*)&addr[2], buff+15, 4);
			}
			spi_flash_write((ADDR_TABLE_START + wpage) * SPI_FLASH_SEC_SIZE + 12 * j, addr, 12);
		}
		info[1]++;
		info[0] = (uint16)wpage<<8 | addrnum;
		spi_flash_erase_sector(ADDR_TABLE_START + 2);
		spi_flash_write((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, info, 8);
	}
}

void convertaddr(uint8 *buff){
	uint32 info,addr[3];
	uint8 addrnum,rpage,i;
	uint8 recvbytes = strcspn(buff,">");
	spi_flash_read((ADDR_TABLE_START + 2) * SPI_FLASH_SEC_SIZE, &info, 4);
	if(info == 0xffffffff)//which page to read and the total number of address
		info = 0;
	addrnum = info & 0xff;
	rpage = ((info >> 8) && 0xff) % 2;
	for(i=0; i<addrnum; i++){
		spi_flash_read((ADDR_TABLE_START + rpage) * SPI_FLASH_SEC_SIZE + 12 * i, addr, 12);
		if( !strncmp((uint8*)addr, buff+5,8) ){
			strncpy(buff+5, (uint8*)&addr[2], 4);
			strncpy(buff+9, buff+13, recvbytes-12);
			buff[recvbytes-3]=0;
			break;
		}
	}
}

void StrToHex(uint8* Str, uint8* Hex, uint8 len)
{
  uint8 h1,h2,s1,s2,i;
  for(i=0; i < len; i++)
  {
    h1 = Str[2*i];
    h2 = Str[2*i+1];
    s1 = h1-0x30;
    if(s1 > 9)
      s1-=7;
    s2 = h2-0x30;
    if(s2 > 9)
      s2-=7;
    Hex[i]=s1*16+s2;
  }
}

void HexToStr(uint8* Hex, uint8* Str, uint8 len)
{
  char	ddl,ddh;
  int i;

  for (i=0; i<len; i++)
  {
    ddh = 48 + Hex[i] / 16;
    ddl = 48 + Hex[i] % 16;
    if (ddh > 57)
      ddh = ddh + 7;
    if (ddl > 57)
      ddl = ddl + 7;
    Str[i*2] = ddh;
    Str[i*2+1] = ddl;
  }

  Str[len*2] = '\0';
}

void led_init()
{
	GPIO_AS_OUTPUT(GPIO_Pin_4);
	GPIO_OUTPUT(GPIO_Pin_4, 1);		        //outout 1 link turn off

	GPIO_AS_OUTPUT(GPIO_Pin_5);
	GPIO_OUTPUT(GPIO_Pin_5, 0);		        //outout 2 ready turn on
}

void wifi_config()
{
	uint8 *buff=(uint8*)zalloc(64);
	uint8 len1,len2;
	spi_flash_read((ADDR_TABLE_START + 3) * SPI_FLASH_SEC_SIZE, (uint32*)buff, 64);
#ifdef DEBUG
	printf(buff);
#endif

	if(buff[0]=='<'){
		// Set the device to be STA mode
		wifi_set_opmode(STATION_MODE);
		struct station_config *config1 = (struct station_config *) zalloc(
				sizeof(struct station_config));;
		len1=strcspn(buff+2,",");
		memcpy(config1->ssid, buff+2,len1);
		len2=strcspn(buff+len1+3,">");
		memcpy(config1->password, buff+len1+3, len2);
		wifi_station_set_config(config1);
		free(config1);
		wifi_station_connect();
#ifdef DEBUG
		printf("ssid:%s\n",config1->ssid);
		printf("password:%s\n", config1->password);
#endif
	}
	else{
		// Set the device to be AP mode
		 printf("\nAP mode\n");
		 wifi_set_opmode(SOFTAP_MODE);
		 struct softap_config *config2 = (struct softap_config *)zalloc(sizeof(struct softap_config)); // initialization
		 wifi_softap_get_config(config2);           // Get soft-AP config first.
		 sprintf(config2->ssid, "%s%x", AP_SSID,system_get_chip_id() & 0xffff);
		 sprintf(config2->password, AP_PASSWORD);
		 config2->authmode = AUTH_WPA_WPA2_PSK;
		 config2->ssid_len = 0;                     // or its actual SSID length
		 config2->max_connection = 4;
		 wifi_softap_set_config(config2);           // Set ESP8266 soft-AP config
		 free(config2);
	}
	free(buff);
}

void user_info()
{
	printf("SDK version:%s%d.%d.%dt%d(%s)\nserverip:%s\n", VERSION_TYPE,IOT_VERSION_MAJOR,\
	    IOT_VERSION_MINOR,IOT_VERSION_REVISION,device_type,UPGRADE_FALG, REMOTE_IP);
}

int UpdateModeOrder(uint8 *order, uint8 len)
{
	uint8 modeidx;
//	if( modeidx >= MODE_NUM )
//		return -1;

	uint8 buff[ORDER_LEN+1] = { 0 };
	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;
	uint8 *modeidxending = NULL;
	uint8 addrLen;
	uint8 hwcodeLen;
	int usedBufferLen;
	int remainingBufferLen;

	if(order[2] == 'A')
	{
		if(order[4] == '1')
		{
			convertaddr(order);
			convertaddr(order + 12);
			memcpy(buff, order, len - 8);
		}
		else
		{
			convertaddr(order);
			memcpy(buff, order, ORDER_LEN - 4);
		}
		printf(buff);
		free(orderbuffer);
		return 1;
	}

	if(order[2] == 'D')
	{
		spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'S');
		if(ctrl == NULL)
		{
			free(orderbuffer);
			return 0;
		}
		addrLen = ctrl - order;
		while(*head == '<')
		{
			if(!memcmp(head, order, addrLen) && (order[14] - 'G') == (head[16] - '0'))
				break;
			else
				head += strlen(head) + 1;
		}
		usedBufferLen = head - orderbuffer;
		remainingBufferLen = totalBufferLen - usedBufferLen;
		if(remainingBufferLen > len)
		{
			order[addrLen] = 'X';//13
			order[addrLen + 3] = order[addrLen + 1] - 'G' + '0';//16 14
			order[addrLen + 1] = 'H';//14
			memcpy(head, order, len);
			head[len] = '\0';
		}
		else
			printf("\r\norder full\r\n");
		spi_flash_erase_sector(SPI_FLASH_START - MODE_NUM);
		spi_flash_write((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		free(orderbuffer);
		return 1;
	}

	if(order[2] == '9')
	{
		ctrl = strchr(order, 'S');
		if(ctrl == NULL)
		{
			free(orderbuffer);
			return 0;
		}

		StrToHex(ctrl + 2, &hwcodeLen, 1);
		printf("hwcodeLen:%d\n", hwcodeLen);
		if(ctrl - order + hwcodeLen + 3 > len)
		{
			free(orderbuffer);
			return -1;
		}

		modeidx = atoi(ctrl + hwcodeLen + 4);
		printf("modeidx:%d\n", modeidx);
		if(modeidx >= MODE_NUM)
		{
			free(orderbuffer);
			return -1;
		}

		spi_flash_read((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		addrLen = ctrl - order;
		order[addrLen] = 'X';
		while(*head == '<')
		{
			if(!memcmp(order, head, addrLen + hwcodeLen + 4))
				break;
			else
				head += strlen(head) + 1;
		}
		usedBufferLen = head - orderbuffer;
		remainingBufferLen = totalBufferLen - usedBufferLen;
		if(remainingBufferLen > len)
		{
			memcpy(head, order, len);
			head[len] = '\0';
		}
		else
			printf("\r\norder full\r\n");
		spi_flash_erase_sector(SPI_FLASH_START - modeidx);
		spi_flash_write((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		free(orderbuffer);
		return 1;
	}

	modeidx = atoi(order + 17);
	if(modeidx >= MODE_NUM)
	{
		free(orderbuffer);
		return -1;
	}
	spi_flash_read((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	head = orderbuffer;
	ctrl = strchr(order, 'S');
	if(head == NULL || ctrl == NULL)
	{
		free(orderbuffer);
		return 0;
	}
	addrLen = ctrl - order;

	while(*head == '<')
	{
		if(!memcmp(head, order, addrLen) && !memcmp(head + addrLen + 2, order + addrLen + 2, 2))
		{
			break;
		}
		else
		{
			head += strlen(head) + 1;
		}
	}

	usedBufferLen = head - orderbuffer;
	remainingBufferLen = totalBufferLen - usedBufferLen;
	int i;
	if(remainingBufferLen > len)
	{
		order[addrLen] = 'X';
		head[len] = 0;
		memcpy(head, order, len);
	}
	else
	{
		printf("\r\norder full\r\n");
		free(orderbuffer);
		return -1;
	}

	spi_flash_erase_sector(SPI_FLASH_START - modeidx);
	spi_flash_write((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	free(orderbuffer);
	return 1;
}

int DeleteModeOrder(uint8 *order, uint8 len)
{
	uint8 modeidx;
	uint8 hwcodeLen;
	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;

	uint8 buff[ORDER_LEN+1];
	if(order[2] == 'A')
	{
		if(order[4] == '1')
		{
			convertaddr(order);
			convertaddr(order + 12);
			memcpy(buff, order, len - 8);
		}
		else
		{
			memcpy(buff, order, len);
			buff[len] = '\0';
		}
		printf(buff);
		free(orderbuffer);
		return 1;
	}

	ctrl = strchr(order, 'S');
	if(ctrl == NULL)
	{
		free(orderbuffer);
		return 0;
	}

	if(order[2] == '9')
	{
		StrToHex(ctrl + 2, &hwcodeLen, 1);
		if(ctrl - order + hwcodeLen + 3 > len)
		{
			free(orderbuffer);
			return -1;
		}
		modeidx = atoi(ctrl + hwcodeLen + 4);
	}
	else
		modeidx = atoi(order + 17);

	if(modeidx >= MODE_NUM)
	{
		free(orderbuffer);
		return -1;
	}

	spi_flash_read((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	head = orderbuffer;

	uint8 addrLen = ctrl - order;
	uint8 *find = NULL;
	uint8 deletelen;
	uint8 copylen;

	while(*head == '<')
	{
		if(find != NULL)
		{
			copylen = strlen(head) + 1;
			memmove(find, head, copylen);
			memset(find + copylen, 0xFF, head - find);
			find += copylen;
			head += copylen;
		}
		else if(!memcmp(head, order, addrLen) && !memcmp(head + addrLen + 2, order + addrLen + 2, 2))
		{
			find = head;
			deletelen = strlen(head) + 1;
			memset(head, 0xFF, deletelen);
			head += deletelen;
		}
		else
			head += strlen(head) + 1;
	}
	spi_flash_erase_sector(SPI_FLASH_START - modeidx);
	spi_flash_write((SPI_FLASH_START - modeidx) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	free(orderbuffer);
	return 1;
}

int DropMode(uint8 modeidx)
{
	if( modeidx >= MODE_NUM)
			return -1;
	spi_flash_erase_sector(SPI_FLASH_START - modeidx);
	return 1;
}

int EnableMode(uint8 modeidx, uint8 *order)
{
	if( modeidx >= MODE_NUM)
		return -1;

	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;
	int addrLen;

	if(order[2] == 'D')
	{
		spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'T');
		addrLen = ctrl - order;
		while(*head == '<')
		{

			if(!memcmp(order, head, addrLen))
			{
				//order[10] = 'H';
				//memcpy(head, order, addrLen + 2);
				head[14] = 'H';
				printf("enable mode OK!\n");
				break;
			}
			else
				head += strlen(head) + 1;
		}
		spi_flash_erase_sector(SPI_FLASH_START - MODE_NUM);
		spi_flash_write((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	}
	else
	{
		modectl[modeidx] = 0;

	}

	free(orderbuffer);
	return 1;
}

int DisableMode(uint8 modeidx, uint8 *order)
{
	if( modeidx >= MODE_NUM)
		return -1;

	int totalBufferLen = ORDER_NUM * ORDER_LEN;
	uint8 *orderbuffer = (uint8 *)zalloc(totalBufferLen * sizeof(uint8));
	uint8 *head = NULL;
	uint8 *ctrl = NULL;
	int addrLen;

	if(order[2] == 'D')
	{
		spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
		head = orderbuffer;
		ctrl = strchr(order, 'T');
		addrLen = ctrl - order;
		while(*head == '<')
		{

			if(!memcmp(order, head, addrLen))
			{
				//order[10] = 'G';
				//memcpy(head, order, addrLen + 2);
				head[14] = 'G';
				printf("disable mode OK!\n");
				break;
			}
			else
				head += strlen(head) + 1;
		}
		spi_flash_erase_sector(SPI_FLASH_START - MODE_NUM);
		spi_flash_write((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	}
	else
	{
		modectl[modeidx] = 1;
	}

	free(orderbuffer);
	return 1;
}

int ProcessSensor(uint8 *orderIn, uint8 modeidx)
{
	int totalBufferLen = 240;//ORDER_NUM * ORDER_LEN;
	uint8 *order = (uint8*)zalloc(DATA_LEN);
	uint8 *orderbuffer = (uint8*)zalloc(totalBufferLen * sizeof(uint8));
	memcpy(order, orderIn, DATA_LEN);
	DeliverSendOrderIndex();

	if(orderbuffer == NULL)
	{
		MASTER_DBG("allocate memory failed\n");
		free(orderbuffer);
		free(order);
		return -1;
	}
	else
	{
		MASTER_DBG("allocate memory success\n");
	}

	spi_flash_read((SPI_FLASH_START - MODE_NUM) * SPI_FLASH_SEC_SIZE, (uint32*)orderbuffer, totalBufferLen);
	uint8 *head = orderbuffer;
	while(*head == '<')
	{
		if(!memcmp(head, order, 13) && ((order[16] - '0') & 1) == (head[16] - '0') && head[14] == 'H')
		{
#ifdef DEBUG
		printf(head);
		printf("\nreceived flag:%d, saved flag:%d\n",((order[16]-'0')&1), (head[16]-'0'));
#endif
			modeidx = atoi(head + 17);
			uint8 *orderidx = (uint8*)zalloc(sizeof(uint8));
			*orderidx = modeidx;
			//printf("ProcessSensor orderidx:%d\n", modeidx);
			xTaskCreate(SceneOrder, "SceneOrder", 256, orderidx, 2, NULL);
			//WaitForSendOrderReady();
			//delay_ms(500);
			break;
		}
		else
		{
			head += strlen(head) + 1;
		}
	}
	free(orderbuffer);
	free(order);
	return 1;

}

int ProcessOrder(uint8 *order, uint8 len)
{
	uint8 buff[128];
	uint8 orderidx;
	uint8 opts;
	if(order == NULL || order[0] != ORDER_HEAD)
		return -1;
	uint8* orderend = strchr(order, ORDER_END);
	if(orderend == NULL)
		return -1;

	if(strcspn(order, "XT") == ORDER_OPTS_POS_NEW)
	{
//#ifdef DEBUG
		//printf("long address, converting...\n");
//#endif
		memcpy(buff, order, len);
		convertaddr(order);
		len -= 4;
	}

	if(order[ORDER_OPTS_POS_NEW] == ORDER_SET)
	{
		if(order[ORDER_OPTS_POS_NEW + 1] == ORDER_DELETE)
			DeleteModeOrder(order, len);
		else
			UpdateModeOrder(order, len);
	}
	else if(order[ORDER_OPTS_POS_OLD] == ORDER_CONTROL)
	{
		memcpy(buff, order, len);
		buff[len] = 0;
		SendOrder(buff, len);
	}
	else if(order[ORDER_OPTS_POS_OLD] == ORDER_SEND)
	{
		opts = order[ORDER_OPTS_POS_OLD + 1];
		orderidx = atoi(order + ORDER_OPTS_POS_OLD + 4);
		uint8* modeidx;
		switch(opts)
		{
			case ORDER_OPEN:
				modeidx = (uint8*)zalloc(sizeof(uint8));
				*modeidx = orderidx;
				xTaskCreate(SceneOrder, "SceneOrder", 256, modeidx, 2, NULL);
				//WaitForSendOrderReady();
			break;

			case ORDER_CLOSE://disable mode
				DisableMode(0, buff);
			break;

			case ORDER_SET://enable mode
				EnableMode(0, buff);
			break;

			case ORDER_DELETE:
				DropMode(orderidx);
			break;

			default:
			break;
		}
	}
	else if(order[ORDER_OPTS_POS_OLD] == ORDER_ALARM)
	{
		uint8* findmark = strchr(order + ORDER_OPTS_POS_OLD, '*');
		uint8* TimerMode = findmark + 1;

		uint8 *type;
		uint8 period[8] = { 0 };
		uint8 weekday[16] = { 0 };
		uint8 time[16] = { 0 };
		uint8 action[8] = { 0 };
		uint8 actions_time[16] = { 0 };
		uint8 actions_action[8] = { 0 };

		opts = order[ORDER_OPTS_POS_OLD + 1];
		orderidx = atoi(order + ORDER_OPTS_POS_OLD + 2);
		if(orderidx > MODE_NUM)
			return -1;

		switch(*TimerMode)
		{
			case TIMER_LOOP_IN_WEEK:
				type = "LOOP_IN_WEEK";
				findmark = strchr(order + ORDER_OPTS_POS_OLD, '-');
				memcpy(weekday, TimerMode + 1, findmark - TimerMode - 1);
				memcpy(actions_time, findmark + 1, orderend - findmark - 1);
				sprintf(actions_action, "mode%d", orderidx);
				break;
			case TIMER_LOOP_PERIOD:
				type = "LOOP_PERIOD";
				findmark = strchr(order + ORDER_OPTS_POS_OLD, '-');
				memcpy(period, TimerMode + 1, findmark - TimerMode - 1);
				memcpy(weekday, "\"\"", 3);
				memcpy(time, findmark + 1, orderend - findmark - 1);
				sprintf(action, "mode%d", orderidx);
				break;
			case TIMER_FIXED:
				type = "FIXEDTIME";
				memcpy(weekday, "\"\"", 3);
				memcpy(actions_time, TimerMode + 1, orderend - TimerMode - 1);
				sprintf(actions_action, "mode%d", orderidx);
				break;
			default:
				break;
		}

		int id;
		switch(opts)
		{
			case ORDER_OPEN:
				AddTimer(mclient_param, type, period, weekday, time, action, actions_time, actions_action, orderidx);
				break;
			case ORDER_CLOSE:
				UpdateTimer(mclient_param, type, period, weekday, time, action, actions_time, actions_action);
				break;
			case ORDER_DELETE:
				id = GetTimerId(muser_timer, orderidx);
				if(id > 0)
					DeleteTimer(mclient_param, id);
				break;
			default:
				break;
		}
	}

	return 1;
}

SOCKET ConnectToServer(void)
{
	int ret;
	struct sockaddr_in remote_ip;
	SOCKET sta_socket = INVALID_SOCKET;

	bzero(&remote_ip, sizeof(remote_ip));
	remote_ip.sin_family = AF_INET; /* Internet address family */
	remote_ip.sin_addr.s_addr = inet_addr(REMOTE_IP); /* Any incoming interface */
	remote_ip.sin_len = sizeof(remote_ip);
	remote_ip.sin_port = htons(REMOTE_PORT); /* Remote server port */

	while(1){
		if(retrytimes++ > 20)
			system_restart();
		/* Create socket*/
		sta_socket = socket(PF_INET, SOCK_STREAM, 0);
		if(sta_socket == INVALID_SOCKET){
#ifdef DEBUG
			printf("ESP8266 TCP client task > socket error\n");
#endif
			close(sta_socket);
			vTaskDelay(100 / portTICK_RATE_MS);
			continue;
		}
#ifdef DEBUG
		printf("ESP8266 TCP client task > socket %d success!\n",sta_socket);
#endif

		/* Connect to remote server*/
		ret = connect(sta_socket, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr));
		if(0 != ret){
#ifdef DEBUG
			printf("ESP8266 TCP client task > connect fail return %d\n", ret);
#endif
			close(sta_socket);
			vTaskDelay(100 / portTICK_RATE_MS);
			continue;
		}
#ifdef DEBUG
		printf("ESP8266 TCP client task > connect ok!\n");
#endif
		retrytimes = 0;
		break;
	}
	xTaskCreate(ReceiveHttpResponse, "ReceiveHttpResponse", 176, NULL, 2, NULL);

	return sta_socket;

}

void ReceiveHttpResponse(void *parameter)
{
	uint8 *recvbuff = (uint8*)zalloc(128);
	uint8 recvbytes;
	while(1)
	{
		if(sta_socket == INVALID_SOCKET)
			break;
		if((recvbytes = read(sta_socket, recvbuff, 128)) <= 0)
		{
			close(sta_socket);
			sta_socket = INVALID_SOCKET;
			break;
		}
		else
		{
			MASTER_DBG("receive http response %d bytes\n", recvbytes);
		}
	}
	free(recvbuff);
	vTaskDelete(NULL);
}

void UploadVersion()
{
	uint8 *buff = (uint8*)zalloc(100);
	sprintf(buff,"POST /zfzn02/servlet/MasterMessageServlet?masterCode=%s&masterVersion=v%d.%d.%d HTTP/1.1\r\n", ctrlid, IOT_VERSION_MAJOR, IOT_VERSION_MINOR, IOT_VERSION_REVISION);
	write(sta_socket, buff, strlen(buff));
	write(sta_socket, "Connection:keep-alive\r\n", strlen("Connection:keep-alive\r\n"));
	write(sta_socket, "User-Agent:lwip1.3.2\r\n", strlen("User-Agent:lwip1.3.2\r\n"));
	write(sta_socket, "Host:101.201.211.87:8080\r\n", strlen("Host:101.201.211.87:8080\r\n"));
	write(sta_socket, "\r\n", 2);
	free(buff);
}

int HttpPostRequest(uint8* data, uint8 len)
{
	if(sta_socket == INVALID_SOCKET)
		return -1;

	fd_set write_set;
	struct timeval timeout;
	uint8 *buff = (uint8*)zalloc(200);
	
	FD_ZERO(&write_set);
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	FD_SET(sta_socket, &write_set);
	
	int ret = select(sta_socket + 1, NULL, &write_set, NULL, &timeout);
	MASTER_DBG("HttpPostRequest task > select ret: %d\n", ret);
	
	if(ret > 0)
	{
		if(FD_ISSET(sta_socket, &write_set))
		{
			sprintf(buff, "POST /zfzn02/servlet/ElectricStateServlet?electricState=<%s> HTTP/1.1\r\n",data);
			if(write(sta_socket, buff, strlen(buff)) < 0)
			{
				printf("post failed\n");
				free(buff);
				return -1;
			}
			write(sta_socket, "Connection:keep-alive\r\n", strlen("Connection:keep-alive\r\n"));
			write(sta_socket, "User-Agent:lwip1.3.2\r\n", strlen("User-Agent:lwip1.3.2\r\n"));
			write(sta_socket, "Host:101.201.211.87:8080\r\n", strlen("Host:101.201.211.87:8080\r\n"));
			write(sta_socket, "\r\n", 2);
		}
	}
	free(buff);
	return 0;
}

int HttpGetRequest(uint8* data, uint8 len)
{
	if(sta_socket == INVALID_SOCKET)
		return -1;

	fd_set write_set;
	struct timeval timeout;
	uint8 *buff = (uint8*)zalloc(100);

	FD_ZERO(&write_set);
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	FD_SET(sta_socket, &write_set);

	int ret = select(sta_socket + 1, NULL, &write_set, NULL, &timeout);
	//printf("HttpGetRequest task > select ret: %d\n", ret);

	if(ret > 0)
	{
		if(FD_ISSET(sta_socket, &write_set))
		{
			sprintf(buff, "GET /zfzn02/servlet/ElectricOrderServlet?masterCode=%s HTTP/1.1\r\n", data);
			if(write(sta_socket, buff, strlen(buff)) < 0)
			{
				printf("write failed\n");
				free(buff);
				return -1;
			}
			write(sta_socket, "Connection:keep-alive\r\n", strlen("Connection:keep-alive\r\n"));
			write(sta_socket, "User-Agent:lwip1.3.2\r\n", strlen("User-Agent:lwip1.3.2\r\n"));
			write(sta_socket, "Host:101.201.211.87:8080\r\n", strlen("Host:101.201.211.87:8080\r\n"));
			write(sta_socket, "\r\n", 2);
		}
	}
	free(buff);
	return 0;
}

//int HttpGetRequest(uint8* data, uint8 len)
//{
//	if(sta_socket == INVALID_SOCKET)
//		return -1;
//
//	uint8 *buff = (uint8*)zalloc(100);
//
//	sprintf(buff, "GET /zfzn02/servlet/ElectricOrderServlet?masterCode=%s HTTP/1.1\r\n", data);
//	if(write(sta_socket, buff, strlen(buff)) < 0)
//	{
//		printf("get failed\n");
//		//close(sta_socket);
//		free(buff);
//		return -1;
//	}
//	write(sta_socket, "Connection:keep-alive\r\n", strlen("Connection:keep-alive\r\n"));
//	write(sta_socket, "User-Agent:lwip1.3.2\r\n", strlen("User-Agent:lwip1.3.2\r\n"));
//	write(sta_socket, "Host:101.201.211.87:8080\r\n", strlen("Host:101.201.211.87:8080\r\n"));
//	write(sta_socket, "\r\n", 2);
//	free(buff);
//	return 0;
//}
//#undef MASTER_DBG
//#define MASTER_DBG printf
void SendToClient(uint8 *data, uint8 len)
{
	uint8 i;
	SOCKET client_sock;
	fd_set write_set;
	struct timeval timeout;
//#ifdef DEBUG
	MASTER_DBG("SendToClient task > send to %d clients\n", client_num);
//#endif
	for(i=0; i < client_num; i++){
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;
		client_sock = client_conn[i];

		FD_ZERO(&write_set);
		FD_SET(client_sock, &write_set);

		int ret = select(client_sock + 1, NULL, &write_set, NULL, &timeout);
		MASTER_DBG("SendToClient task > select ret: %d\n", ret);

		if(ret > 0)
		{
			MASTER_DBG("SendToClient task > send to client %d\n", client_sock);
			if (FD_ISSET(client_sock, &write_set) && (data != NULL))
			{
				if((ret = send(client_sock, data, len, 0)) <= 0)
				{
					CloseClient(client_sock);
					MASTER_DBG("SendToClient task > send to %d failed!\n", client_sock);
				}
				else
				{
					MASTER_DBG("SendToClient task > send %d bytes to %d success\n", ret, client_sock);
				}
			}
		}
		else
		{
			CloseClient(client_sock);
		}
	}
}
//#undef MASTER_DBG
//#define MASTER_DBG /\
///printf

//void SendToClient(uint8 *data, uint8 len)
//{
//	uint8 i;
//	SOCKET client_sock;
//	fd_set write_set;
//	struct timeval timeout;
//	MASTER_DBG("SendToClient task > send to %d clients\n", client_num);
//	for(i=0; i < client_num; i++){
//		client_sock = client_conn[i];
//		MASTER_DBG("SendToClient task > send to client %d\n", client_sock);
//		if(send(client_sock, data, len, 0) <= 0)
//		{
//			CloseClient(client_sock);
//			MASTER_DBG("SendToClient task > send to %d failed!\n", client_sock);
//		}
//		else
//		{
//			MASTER_DBG("SendToClient task > send to %d success\n", client_sock);
//		}
//	}
//}

void CloseClient(SOCKET client)
{
	uint8 i;

	for(i = 0; i < client_num; i++)
	{
		if(client == client_conn[i])
			break;
	}

	if(i == client_num)
	{
		MASTER_DBG("ESP8266 TCPClientProcess task > error:connection not found!\n");
		return;
	}
	else if(i < client_num-1)
	{
		for( ; i < client_num; i++)
			client_conn[i] = client_conn[i+1];
	}

	close(client);
	client_num--;

	MASTER_DBG("remaining %d connection: ", client_num);
	for(i = 0; i < client_num; i++)
	{
		MASTER_DBG("%d ", client_conn[i]);
	}
	MASTER_DBG("\n");
}

void TCPClientProcess(void *pvParameter)
{
	uint8 i;
	uint8 client_idx;
	SOCKET cliconn;
	SOCKET max_sock;
	fd_set read_set;
	struct timeval timeout;
	MASTER_DBG("ESP8266 TCPClientProcess task > begin\n");
	while(1)
	{
		timeout.tv_sec =  1;
		timeout.tv_usec = 0;
		max_sock = -1;

		FD_ZERO(&read_set);
		
		//close extra connection and move the array
		if(client_num > MAX_CONN)
		{
			for(i = 0; i < MAX_CONN; i++)
			{
				if(i < client_num - MAX_CONN)
					close(client_conn[i]);
				client_conn[i] = client_conn[i+client_num-MAX_CONN];
			}
			client_num = MAX_CONN;
		}

		for(i = 0; i < client_num; i++)
		{
			//MASTER_DBG("add %dth fd %d into fd_set\n", i, client_conn[i]);
			if(client_conn[i] > max_sock)
				max_sock = client_conn[i];
			FD_SET(client_conn[i], &read_set);
		}

		//MASTER_DBG("ESP8266 TCPClientProcess task > read_set: %x\n", read_set);
		//MASTER_DBG("ESP8266 TCPClientProcess task > max_sock: %d\n", max_sock);

//		if(max_sock < 0)
//		{
//			vTaskDelay(100 / portTICK_RATE_MS);
//			continue;
//		}

		int ret = select(max_sock + 1, &read_set, NULL, NULL, &timeout);

		MASTER_DBG("ESP8266 TCPClientProcess task > ret: %d, read_set: %x\n", ret, read_set);
		if(ret < 0 && errno != 0)
		{
			MASTER_DBG("select error reason: %d\n", errno);
			MASTER_DBG("EBADF:%d\n", EBADF);
			MASTER_DBG("EINTR:%d\n", EINTR);
			MASTER_DBG("EINVAL:%d\n", EINVAL);
			MASTER_DBG("ENOMEM:%d\n", ENOMEM);
			continue;
		}

		//the select may return -1 repeatedly, but errno is 0, it seems it select right? ignore it, mmp!
		if(ret != 0)
		{
			MASTER_DBG("ESP8266 TCPClientProcess task > ret: %d\n", ret);
			for(client_idx = 0; client_idx < client_num; client_idx++)
			{
				cliconn = client_conn[client_idx];
				MASTER_DBG("client conn: %d\n", cliconn);
				if(FD_ISSET(cliconn, &read_set))
				{
					int recvbytes = 0;
					uint8 orderidx;
					char *recv_buf = (char *)zalloc(DATA_LEN);
					recvbytes = read(cliconn, recv_buf, DATA_LEN);
					if(recvbytes > 0)
					{
						if(recvbytes > 20 && recv_buf[0]=='<')
						{
							recv_buf[recvbytes] = 0;
							if(recv_buf[1]=='<'){
								spi_flash_erase_sector(ADDR_TABLE_START + 3);
								spi_flash_write((ADDR_TABLE_START + 3) * SPI_FLASH_SEC_SIZE, (uint32*)recv_buf, 64);
								sprintf(recv_buf, "set wifi ok!\n");
								printf(recv_buf);
								send(cliconn, recv_buf, strlen(recv_buf), 0);
								system_restart();
							}
							//ProcessOrder(recv_buf, recvbytes);
							else if(recv_buf[9] == 'T'){
								orderidx = atoi(recv_buf+13);
								if(recv_buf[10] == 'H'){
									//orderidx = atoi(recv_buf+13);//mode control order index is 13
									if(orderidx >= 0 && orderidx < MODE_NUM && !modectl[orderidx]){
										uint8* modeidx = (uint8*)zalloc(sizeof(uint8));
										*modeidx = orderidx;
										portBASE_TYPE xStatus;
										bool value = true;
										xStatus = xQueueSend(SceneOrderAmountLimit, &value, 0);
										
										if(xStatus == pdTRUE)
										{
											xQueueReceive(SceneOrderAmountLimit, &value, 0);
											xStatus = xTaskCreate(SceneOrder, "SceneOrder", 256, modeidx, 2, NULL);
										}
										//WaitForSendOrderReady();
									}
								}
								if(recv_buf[10] == 'G'){//disable mode
									modectl[orderidx]=1;
								}
								if(recv_buf[10] == 'S'){//enable mode
									modectl[orderidx]=0;
								}
								if(recv_buf[10] == 'R'){
									if(orderidx >= 0 && orderidx < MODE_NUM )
										spi_flash_erase_sector(SPI_FLASH_START - orderidx);
								}
								if(recv_buf[10] == 'T'){//just for debug
									uint8 mypage = atoi(recv_buf + 16);
									spi_flash_read((SPI_FLASH_START - orderidx) * SPI_FLASH_SEC_SIZE + 128*mypage,(uint32*)recv_buf,128);
									recv_buf[127]=0;
									for(i=0;i<128;i++)
										printf("%c",recv_buf[i]);
								}
								if(recv_buf[10] == 'U'){//just for debug
									spi_flash_read((ADDR_TABLE_START + orderidx) * SPI_FLASH_SEC_SIZE,(uint32*)recv_buf,128);
									recv_buf[127]=0;
									for(i=0;i<128;i++)
										printf("%c",recv_buf[i]);
								}
							}
							else{
								if(recv_buf[ORDER_OPTS_POS_NEW] == 'X' || recv_buf[ORDER_OPTS_POS_OLD] == 'X')
								{
									convertaddr(recv_buf);
									SendOrder(recv_buf, strlen(recv_buf));
								}
								else
								{
									printf(recv_buf);
								}
							}//*/

							MASTER_DBG("TCPClientProcess task stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
							//system_print_meminfo();
							//printf("TCPClientProcess stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());
						}
#ifdef DEBUG
						printf("ESP8266 TCPClientProcess task > read %d bytes success:%s\n", recvbytes, recv_buf);
						//send(cliconn, recv_buf, strlen(recv_buf), 0);
#endif
					}
					else if(recvbytes == 0){
						MASTER_DBG("ESP8266 TCPClientProcess task > end of file socket %d\n", cliconn);
						CloseClient(cliconn);
						continue;
					}
					else{
						MASTER_DBG("ESP8266 TCPClientProcess task > socket disconnected!\n");
						CloseClient(cliconn);
						continue;
					}
					free(recv_buf);
				}
			}
		}
	}
}

void SendOrderTask(void *pvParameter)
{
	if(OrderQueue == NULL)
		OrderQueue = xQueueCreate(DATA_LEN, 1);
	if(DeviceStateQueue == NULL)
		DeviceStateQueue = xQueueCreate(DATA_LEN, 1);
	if(OrderMutexQueue == NULL)
		OrderMutexQueue = xQueueCreate(1, 1);

	uint8 i;
	uint8 *qStateBuffer = (uint8*)zalloc(DATA_LEN);
	uint8 *qOrderBuffer = (uint8*)zalloc(DATA_LEN);

	const portTickType WaitOrderTimeout = 100 / portTICK_RATE_MS;
	const portTickType WaitStateTimeout = 500 / portTICK_RATE_MS;
	const uint8 ResendTimes = 3;

	while(1)
	{
		//read one order from the OrderQueue
		uint8 ret = ReadQueue(OrderQueue, qOrderBuffer, WaitOrderTimeout);

		//clear the DeviceStateQueue in case there is data already exist(tough the device manually)
		ClearQueue(DeviceStateQueue);

		if(ret == 0)
			continue;

		MASTER_DBG("SendOrderTask > order %s\n", qOrderBuffer);

		//resend the order at most 3 times if no device state is received
		for(i = 0; i < ResendTimes; i++)
		{
			MASTER_DBG("\nSendOrderTask > SENDING order %d:", i);
			printf(qOrderBuffer);

			ret = ReadQueue(DeviceStateQueue, qStateBuffer, WaitStateTimeout);
			if(ret == 0)
				continue;

			MASTER_DBG("SendOrderTask > device state %s\n", qStateBuffer);

			//the order has been converted to short address, convert the device state if long address
			if(qStateBuffer[ORDER_OPTS_POS_NEW] == 'Z')
			{
				convertaddr(qStateBuffer);
				break;
			}

			MASTER_DBG("SendOrderTask > device order %s\n", qOrderBuffer);
			MASTER_DBG("SendOrderTask > device state %s\n", qStateBuffer);

			//compare the short address to see if it's the actual device
			if(!memcmp(qStateBuffer, qOrderBuffer, ORDER_OPTS_POS_OLD))
			{
				MASTER_DBG("SendOrderTask > received device state\n");
				break;
			}
		}
		MASTER_DBG("SendOrderTask > send order complete\n");

		//printf("SendOrderTask stack:%d heap:%d\n",uxTaskGetStackHighWaterMark(NULL),system_get_free_heap_size());

	}
	vTaskDelete(NULL);
}

uint8 ReadQueue(xQueueHandle queue, uint8 *buffer, portTickType timeout)
{
	portBASE_TYPE xStatus;
	uint8 *head = buffer;

	memset(buffer, 0, DATA_LEN);

	//drop the data before order head '<'
	while(1)
	{
		xStatus = xQueueReceive(queue, buffer, timeout);
		if(xStatus == errQUEUE_FULL || buffer - head >= DATA_LEN - 2)
		{
			return 0;
		}
		if(*buffer == ORDER_HEAD)
		{
			buffer++;
			break;
		}
	}

	//read data until the order end '>'
	while(1)
	{
		xStatus = xQueueReceive(queue, buffer, timeout);
		if(xStatus == errQUEUE_FULL || *buffer == ORDER_END || buffer - head >= DATA_LEN - 2)
			break;
		buffer++;
	}

	//check if data read is valid
	if(buffer == head)
		return 0;
	if(*buffer != '>')
	{
		printf("invalid order read:%s, reason %d", head, 1);
		return 0;
	}
	if(buffer - head >= DATA_LEN - 2)
	{
		printf("invalid order read, reason %d", 2);
		return 0;
	}
	if(buffer == head || *(buffer) != '>' || buffer - head >= DATA_LEN - 2)
	{
		printf("invalid order read, reason %d", 1);
		return 0;
	}

	return 1;
}

uint8 WriteQueue(xQueueHandle queue, uint8 *data, uint8 len)
{
	if(queue == NULL || data == NULL)
		return 0;

	const portTickType xTicksToWait = 150 / portTICK_RATE_MS;
	uint8 i;
	for(i = 0; i < len; i++)
	{
		xQueueSend(queue, data, portMAX_DELAY);//0);//
		data++;
	}
	return 1;
}

uint8 ClearQueue(xQueueHandle queue)
{
	if(queue == NULL)
		return 0;

	portBASE_TYPE xStatus;
	uint8 tempbuff;

	do
	{
		xStatus = xQueueReceive(queue, &tempbuff, 0);
	}while(xStatus == pdPASS);

	return 1;
}

uint8 IsQueueEmpty(xQueueHandle queue)
{
	if(queue == NULL)
		return 1;

	portBASE_TYPE xStatus;
	uint8 tempbuff;

	xStatus = xQueuePeek(queue, &tempbuff, 0);

	if(xStatus == errQUEUE_FULL)
		return 1;
	else
		return 0;
}

void SendOrder(uint8 *data, uint8 len)
{
	bool value = true;

	//wait until the OrderMutexQueue is writable ensuring no other thread is writing the OrderQueue
	xQueueSend(OrderMutexQueue, &value, portMAX_DELAY);
	WriteQueue(OrderQueue, data, len);

	//clear the OrderMutexQueue so other thread can write the OrderQueue
	xQueueReceive(OrderMutexQueue, &value, 0);
}

void GetState(uint8 *data, uint8 len)
{
	//there is only one thread --UartProcess that write the DeviceStateQueue, so it's not necessary to sync
	WriteQueue(DeviceStateQueue, data, len);
}
































