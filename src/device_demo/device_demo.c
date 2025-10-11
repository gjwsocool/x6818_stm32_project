#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "hw_type.h"
#include "iota_init.h"
#include "iota_cfg.h"
#include "log_util.h"
#include "string_util.h"
#include "iota_login.h"
#include "iota_datatrans.h"
#include "iota_error_type.h"
#include "subscribe.h"

#include "led.h"
#include "beep.h"
#include "ds18b20.h"
#include "servo.h"
#include "msg.h"

/*************TCP��STM32ͨ�����******************/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>

//�������
static int server_sockfd;
static int client_sockfd;
//�����ʼλ��
static int position = 500;
/************************************************/
static char *g_workPath = ".";

static char *g_serverIp = "eaf7e024bc.st1.iotda-device.cn-north-4.myhuaweicloud.com";
static int g_port = 1883;
static char *g_username = "68cd0d82d582f2001850d387_ehome";
static char *g_password = "15882014916";

void TimeSleep(int ms){
    usleep(ms * 1000);
}
/*ͳһһ�����ӿ�*/
//��payload:{ "value","����"}��ȡ����
static int get_json_cmd(const char* str){
	char* tmp=strstr(str,":");
	return strtoul(tmp+2,NULL, 0);
}

/*�����ӿ�,����Ͳ���buf*/
static int beep_gateway(int index, const char* cmd,char* buf) {
    (void)index;
    if(!strcmp(cmd, "{\"value\":\"0\"}")){
        strcpy(buf,"\"x6818_beep_off\"");
    }else if(!strcmp(cmd, "{\"value\":\"1\"}")){
        strcpy(buf,"\"x6818_beep_on\"");
    }
    int val=get_json_cmd(cmd);
    return beep_config(val);
}
static int led_gateway(int index, const char* cmd,char* buf) {
    if(!strcmp(cmd, "{\"value\":\"0\"}")){
        strcpy(buf,"\"x6818_led_off\"");
    }else if(!strcmp(cmd, "{\"value\":\"1\"}")){
        strcpy(buf,"\"x6818_led_on\"");
    }
    int val=get_json_cmd(cmd);
    return led_config(index,val);
}
static int stm32_led_ctrl(int index,const char* cmd,char* buf){
	led_req_t ledReq={0};
    ledReq.msgh.msgid=X6818_TO_STM32_LED_REQ;
    ledReq.index=index;
    ledReq.cmd=get_json_cmd(cmd);
	if(ledReq.cmd==1){
		strcpy(buf,"\"stm32_led_on\"");
	}else if(ledReq.cmd==0){
		strcpy(buf,"\"stm32_led_off\"");
	}
    return write(client_sockfd,&ledReq,sizeof(ledReq));
}

static int stm32_beep_ctrl(int index,const char* cmd,char* buf){
	(void)index;
	beep_req_t beepReq={0};
	beepReq.msgh.msgid=X6818_TO_STM32_BEEP_REQ;
	beepReq.cmd=get_json_cmd(cmd);
	if(beepReq.cmd==1){
		strcpy(buf,"\"stm32_beep_on\"");
	}else if(beepReq.cmd==0){
		strcpy(buf, "\"stm32_beep_off\"");
	}
	return write(client_sockfd,&beepReq,sizeof(beepReq));
}
//���һ��С����stm32����
//���cmd����ת�����ϱ������ַ���
static char* moveCmd[]={
	"x6818_car_front",
	"x6818_car_back",
	"x6818_car_left",
	"x6818_car_right",
	"x6818_car_speedUP",
	"x6818_car_speedDOWN",
	"x6818_car_stop"		
};
static int stm32_car_ctrl(int index,const char* cmd,char* buf){
    (void) index;//ֻ��һ����,�ޱ��
    emotor_req_t emotorReq;
    memset(&emotorReq,0,sizeof(emotorReq));
	//������Ϣreq��ͷ��Ϣ
	emotorReq.msgh.msgid=X6818_TO_STM32_EMOTOR_REQ;
	emotorReq.cmd=get_json_cmd(cmd);
	printf("The emotorReq of cmd is %d\n",emotorReq.cmd);
	snprintf(buf, 200,"\"%s\"", moveCmd[emotorReq.cmd-1]);
	//printf("----buf=%s----\n",buf);
	return write(client_sockfd,&emotorReq,sizeof(emotorReq));
}
//�������
//����ֻ��һ�����,index����,buf��������Ϊ���ϱ���ȡ�ĽǶ�ֵ
static int x6818_servo_ctrl(int index,const char* cmd,char* buf){
	(void) index;
	int servo_cmd=get_json_cmd(cmd);//Ҳ����ֵ1����2
	switch (servo_cmd) {
		case SERVO_FRONT:
			position += 200;
			if(position >= 1000) position = 1000;
			servo_move(1 ,position, 500);
			sprintf(buf, "%d", servo_get_position(1));
			printf("-----the Servo is front-----------\n");
			break;
		case SERVO_BACK:
			if(position <= 0) position = 0;
			position -= 200;
			servo_move(1 ,position, 500);
			sprintf(buf, "%d", servo_get_position(1));
			printf("-----the servo is  back-----------\n");
			break;
	}
	return 0;
}

//����ָ��
typedef int(*fun_ptr)(int,const char*,char*);
//����Ӳ��������Ϣ
struct hard_resource{
   	const char* cmd_name;
	fun_ptr ptr;
	int arg1;
};
static struct hard_resource hard_info[]={	
	{
			.cmd_name="x6818_bed_room_led_ctrl",
			.ptr=led_gateway,
			.arg1=1,
	},	
	{
			.cmd_name="x6818_food_room_led_ctrl",
			.ptr=led_gateway,
			.arg1=2,
	},
    {
            .cmd_name="x6818_wc_room_led_ctrl",
            .ptr=led_gateway,
            .arg1=3,
    }, 
    {
            .cmd_name="x6818_kitchen_room_led_ctrl",
            .ptr=led_gateway,
            .arg1=4,
    },
    {
            .cmd_name="x6818_room_beep_ctrl",
            .ptr=beep_gateway,
            .arg1=0,
    },
	{//ֱ�ӷ��͵Ƶĵ�ƽ��stm32,0��1��
            .cmd_name="stm32_first_red_led_ctrl",
            .ptr=stm32_led_ctrl,
            .arg1=1,
    },
    {
            .cmd_name="stm32_second_green_led_ctrl",
            .ptr=stm32_led_ctrl,
            .arg1=2,
    },
	{
			.cmd_name="stm32_beep_ctrl",
		    .ptr=stm32_beep_ctrl,
			.arg1=0
	},//С������
    {
            .cmd_name="emotor_car_control",
            .ptr=stm32_car_ctrl,
            .arg1=0,
    },
	{
			.cmd_name="x6818_servo_ctrl",
			.ptr=x6818_servo_ctrl,
			.arg1=0
	}
};
#define SIZE ( sizeof(hard_info)/sizeof(hard_info[0]) )
/*

   @:Test_CommandResponse():����Ϊ�Ʒ�����������Ĵ�����
	***�ƶ��·�������� requestId�����豸������ȷ��Ӧ������ƽ̨��Ϊʧ��
	***��Ӧ��Ҫ����ƽ̨��ִ�гɹ�����ʧ�ܣ�result_code�������ɸ���������Ϣ��
*/
static void Test_CommandResponse(char *requestId)
{
    //ָ�������ϱ�����Ч�غ���Ϣ�����Բ���ָ��
    char *commandResponse = "{\"device_ctrl\": \"over\"}"; 
    //ָ�������Ľ��:0�ɹ�,1ʧ��
    int result_code = 0;
    //����ָ��һ���������������
    char *response_name = "cmdResponses";
    //����SDK�����ϱ��������
    int messageId = IOTA_CommandResponse(requestId, result_code, 
            response_name, commandResponse, NULL);
    if (messageId != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: Test_CommandResponse() failed, messageId %d\n", messageId);
    }
}

/*
һ�� report_device_data() �ķ�װ������
    @:�ϱ���������Ϣ����Ϊ�Ʒ�����
    - payload = ��ʽ��ƴ��(property_name, value)������payload = {"������": ����ֵ}
    - services[i] ���(service_id, properties, event_time)
    - �ٰ� payload ��װ�� services[] �ṹ�壬���� IOTA_PropertiesReport �ϱ���
*/
static void report_device_data(char *property_name, char *value)
{
    /*1�������ϱ��ṹ������*/
    const int serviceNum = 1; 
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    // --------------- the data of service1 -------------------------------
    //ƴ��json��Ч�غ�
    char payload[200] = {0};//��Ч�غ�
    //ע��:x6818_temp���������б��е��������ƣ�����Ϊ�Ʋ鿴�����Ҹ����Լ��������޸�
    sprintf(payload, "{\"%s\":%s}", property_name, value);//�ַ���ƴ�ӣ� ������Ҫ""��������Ҫת��\"\"��
    printf("%s: payload: %s\n", __func__, payload);

    /*2������ϱ��ṹ��,ָ�����ϱ�ʱ�䣬��Ʒ���Լ���Ч�غ�*/
    services[0].event_time = GetEventTimesStamp(); 
    /*������Ա���ĳ��Լ��Ĳ�Ʒ��*/
    services[0].service_id = "ehome"; 
    services[0].properties = payload; 

    /*3������ SDK�ϱ�:ͨ������IOTA_PropertiesReport()�ϱ��ļ�����Ϊ��*/
    int messageId = IOTA_PropertiesReport(services, serviceNum, 0, NULL);
    if (messageId != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: Test_PropertiesReport() failed, messageId %d\n", messageId);
    }
    /*4��SDKҪ���ͷ� event_time�ڴ�*/
    MemFree(&services[0].event_time);
}

//�Զ����ַ�����������ȥ��\n\t�������ַ����Ƚ�
static void mystrtok(char *destination, char *source) {
    int i, j = 0;
    for(i = 0; source[i] != '\0'; i++) {
        if((source[i] != ' ') && (source[i] != '\n') && (source[i] != '\t')) { // ���ַ����ǿո�ʱ
            destination[j++] = source[i]; // �����ַ���destination������j
        }
    }
    destination[j] = '\0'; // ȷ�����ַ�����ȷ��ֹ
}

//�����ƶ��·��ġ������Ϣ��EN_IOTA_COMMAND���������ײ�Ӳ��
//��Ӳ��������ɺ��ϱ����Ա仯��report_device_data������ƽ̨һ������ִ�з�����Test_CommandResponse��
/*
    		HandleCommandRequest��
                        @:�����������ݣ�command_name��paras �ȣ�
			@������Ӳ�������ơ��صơ��ϱ����ݣ�
			@�����ƶ˷���ִ�н����Test_CommandResponse��
 */
static void HandleCommandRequest(EN_IOTA_COMMAND *command)
{
    if (command == NULL) {
        return;
    }
    /*���Դ�ӡ������Ϣ�еĸ����ֶ���Ϣ
    @:���軪Ϊ�Ʒ����������Ч�غ����£�
    @:"{"paras":{"value":"1"/"0"},"service_id":"ehome","command_name":"x6818_bed_room_led_ctrl"}"
    	--��ӡ��Ʒ����+��ӡ��Ч�غ��е���������command_name
    	--��ӡ�����ֵ+��ӡ��Ϣ�ı��,ÿ���·�����Ϣ����һ��Ψһ�Ĵ˱��
    */
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), service_id: %s\n", command->service_id);
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), command_name: %s\n", command->command_name);
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), paras: %s\n", command->paras);
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), request_id %s\n", command->request_id);
    
    
 
    ///�������������x6818һ���յ�����Ӳ��������������������������Ӳ������
    //���ڻ�Ϊ�Ʒ��͵���Ч�غ����ݸ�ʽΪ��{������\n��TAB��\t "value":TAB��\t "1" ����\n }
    //{ 
    //        "value":        "1"
    //}
    // ---------- ��ԭʼ paras ��һ���� payload ----------
    char payload[200] = {0}; //�ݴ洦������Ч�غ�
    mystrtok(payload, command->paras);
    printf("******payload: %s *********\n", payload);
    
    
   			 /*����Ӳ�����ϱ�����*/
    // ---------- ������������ payload ������ҵ���߼�������Ӳ���� ----------
    int i;
    char ctrl_info[200]={0};
    for(i=0;i<SIZE;i++){
        if(!strcmp(command->command_name,hard_info[i].cmd_name)) {
                hard_info[i].ptr(hard_info[i].arg1,payload,ctrl_info);
				//����������ƺ��������Ʋ�ƥ��,��һ���ж�
                if(!strcmp(hard_info[i].cmd_name,"emotor_car_control")){
                    report_device_data("emotor_state",ctrl_info);
                    break;
                } 
				if(!strcmp(hard_info[i].cmd_name, "x6818_servo_ctrl")){
					report_device_data("x6818_servo",ctrl_info);
					break;
				}
                //�ϱ���Ϊ�ƣ���Ҫ�ص����
                report_device_data(command->command_name, ctrl_info);
                break;
        }
    }
    
    //���������� ����-��Ӧģʽ,������Ӳ��֮�����Ϊ��һ��ִ��������
    //������ʾ���ڼ���������Ϣ�������
    Test_CommandResponse(command->request_id); // response command
}


//��ʼ�� SDK ����֤����
//���߻�Ϊ�ƣ���������豸��˭����ʲô��ʽ��֤������̨����������ʲô�˿ڡ���
static void SetAuthConfig(void)
{
    IOTA_ConfigSetStr(EN_IOTA_CFG_MQTT_ADDR, g_serverIp);
    IOTA_ConfigSetUint(EN_IOTA_CFG_MQTT_PORT, g_port);
    IOTA_ConfigSetStr(EN_IOTA_CFG_DEVICEID, g_username);
    IOTA_ConfigSetStr(EN_IOTA_CFG_DEVICESECRET, g_password);
    IOTA_ConfigSetUint(EN_IOTA_CFG_AUTH_MODE, EN_IOTA_CFG_AUTH_MODE_SECRET);
    IOTA_ConfigSetUint(EN_IOTA_CFG_CHECK_STAMP_METHOD, EN_IOTA_CFG_CHECK_STAMP_OFF);
#ifdef _SYS_LOG
    IOTA_ConfigSetUint(EN_IOTA_CFG_LOG_LOCAL_NUMBER, LOG_LOCAL7);
    IOTA_ConfigSetUint(EN_IOTA_CFG_LOG_LEVEL, LOG_INFO);
#endif
}
/************thread_stm32********************/
#define MSG_BUF_LEN 1024
static void* thread_stm32(void* arg){
	//�ݴ�stm32��������
	unsigned char buf[MSG_BUF_LEN]={0};
	//������Ϣ����
	msghead_t msghead;
	while(1){
		printf("stm32 connect x6818\n");
		fflush(stdout);
		sleep(2);

		if(read(client_sockfd,buf,MSG_BUF_LEN)<=0){
			printf("read client data fails\n");
			exit(-1);
		}
		//��ȡ��Ϣͷ����Ӳ��
		memcpy(&msghead, buf, sizeof(msghead));
		printf("stm32 send messages:%d\n",msghead.msgid);
		//����msghead��ͷ��Ϣ�ֶ�����Ӳ��
		switch(msghead.msgid){
			case STM32_TO_X6818_TEMP_REQ://��ȡ�¶�
				{
					char value[200]={0};
					temp_req_t temp_req;//������stm32������x6818�ϱ��¶�
					memcpy(&temp_req ,buf, sizeof(temp_req));
					printf("*****stm32 temp DS18B20 is %.2f******\n",temp_req.temp);
					sprintf(value,"%.2f",temp_req.temp);//���
					report_device_data("stm32_ds18b20" ,value);
					break;
				}
		}
	}
	return NULL;
}
/****����TCP���������ҵȴ�STM32����****/
//�����ǵ����ӣ�������
static void create_tcp_server(void){
	server_sockfd = socket(AF_INET,SOCK_STREAM,0);
	if(server_sockfd == -1){
		perror("create x6818 server fails");
		exit(-1);
	}
	//�˿ڸ���
	int yes = 1;
	if(setsockopt(server_sockfd,SOL_SOCKET, SO_REUSEADDR,&yes,sizeof(yes))==-1){
		perror("setsockopt fails!");
		close(server_sockfd);
		exit(-1);
	}
	//�󶨶˿�
	struct sockaddr_in addr;
	addr.sin_family=AF_INET;
	addr.sin_port=htons(8080);
	addr.sin_addr.s_addr=INADDR_ANY;
	if(bind(server_sockfd,(struct sockaddr*)&addr,sizeof(addr))==-1){
		perror("bind failed!");
		exit(-1);
	}
	//���ü���
	if(listen(server_sockfd,20)==-1){
		perror("listen fails");
		exit(-1);
	}
	printf("\n**************x6818 is waiting for the connection of STM32***********\n");
	//�ȴ��ͻ�������
	struct sockaddr_in client_addr;
	socklen_t addrLen =sizeof(client_addr);
	client_sockfd=accept(server_sockfd,(struct sockaddr*)&client_addr,&addrLen);
	if(client_sockfd==-1){
		perror("accept fail");
		exit(-1);
	}
	/*****��ӡ�����Ϣ****/
	printf("\n**************stm32 connects x6818 successful**************\n");
	printf("client ip:%s,client port:%d\n",
			inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));
	//�����̸߳�stm32ͨ��
	pthread_t thread;
	int ret=pthread_create(&thread,NULL,thread_stm32,NULL);
	if(ret!=0){
		fprintf(stderr,"create thread for stm32 failed:%s\n",strerror(ret));
		exit(-1);
	}
}

/*main()����*/
int main(int argc, char **argv)
{
    //Ӳ����ʼ��:����LED�⺯����LED 
    if(led_open() < 0) {
        printf("open led failed.\n");
        return -1;
    }
    if(beep_open() < 0) {
        printf("open beep failed.\n");
        return -1;
    }
    if(ds18b20_open() <0){
        printf("open ds18b20 failsed.\n");
        return -1;
    }
	/*�����ʼ��*/
	servo_init();
	servo_move(1,position,1000);//id+λ��+ʱ��
    
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: start test ===================>\n");

    /*SDK��ʼ��*/
    //1����ʼ�����ӷ�������Ϣ:�˿ڣ�IP��ַ
    if (IOTA_Init(g_workPath) < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: IOTA_Init() error, init failed\n");
        return 1;
    }
    //2������ѳ�ʼ�������÷ֿ�,�ȳ�ʼ��������
    SetAuthConfig();

     //X6818���ӻ�Ϊ�Ʒ�����,���豸�뻪Ϊ�ƽ��� MQTT���ӣ��������ȴ�ֱ���ɹ�
     //@:��һ�� (IOTA_Connect())��ȷ�� SDK �����ˡ����Ӷ�����������ֵ��ֻ˵����������û�гɹ�����ȥ��
    int ret = IOTA_Connect();
    if (ret != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: IOTA_Auth() error, Auth failed, result %d\n", ret);
    }
    //@:�ڶ��� (IOTA_IsConnected())����ѯ�ȴ����ӽ�������� true��ʾ SDK�Ѿ�������ֲ�ȷ�������ƶ�
    while(!IOTA_IsConnected()) {
        TimeSleep(300);
    }

    //ע��ר�Ŵ���Ϊ�Ƹ�X6818�·���������Ϣ������HandleCommandRequest
    //IOTA_SetCmdCallback�ǻ�Ϊ�� SDK�ṩ�ĺ�����ָ�����յ��ƶ�����ʱ���õĻص���������
    /*�û�ֻ�������£�
              1:ע��ص�����IOTA_SetCmdCallback(HandleCommandRequest);
	      2:ʵ�ֻص����� HandleCommandRequest()����������������Ӳ�����ϱ����ݡ�
    */
    IOTA_SetCmdCallback(HandleCommandRequest);

    //��������:stm32��x6818
	create_tcp_server();


    int count = 0;
    float x6818_temp;
    char value[200] = {0};
    while (count < 10000) {
        x6818_temp = ds18b20_read();  
       // sprintf(value, "%.2f", x6818_temp);
        report_device_data("x6818_temp", value);
        TimeSleep(5000);
        count++;
     }
    
     /*��ѭ��ʲôҲ������ͨ�����ػ����̣���Զ���ߡ���Ϊ�����������ϱ����� �ص�������߳� ���
       while(1) {   TimeSleep(1000); } */
    return 0;
}
