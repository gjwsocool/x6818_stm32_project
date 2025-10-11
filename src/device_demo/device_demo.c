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

/*************TCP和STM32通信相关******************/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>

//两个句柄
static int server_sockfd;
static int client_sockfd;
//舵机起始位置
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
/*统一一函数接口*/
//从payload:{ "value","数子"}获取数字
static int get_json_cmd(const char* str){
	char* tmp=strstr(str,":");
	return strtoul(tmp+2,NULL, 0);
}

/*函数接口,输出型参数buf*/
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
//针对一个小车的stm32控制
//针对cmd数字转换成上报参数字符串
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
    (void) index;//只有一个车,无编号
    emotor_req_t emotorReq;
    memset(&emotorReq,0,sizeof(emotorReq));
	//命令消息req的头信息
	emotorReq.msgh.msgid=X6818_TO_STM32_EMOTOR_REQ;
	emotorReq.cmd=get_json_cmd(cmd);
	printf("The emotorReq of cmd is %d\n",emotorReq.cmd);
	snprintf(buf, 200,"\"%s\"", moveCmd[emotorReq.cmd-1]);
	//printf("----buf=%s----\n",buf);
	return write(client_sockfd,&emotorReq,sizeof(emotorReq));
}
//舵机控制
//这里只有一个舵机,index不传,buf用来给华为云上报获取的角度值
static int x6818_servo_ctrl(int index,const char* cmd,char* buf){
	(void) index;
	int servo_cmd=get_json_cmd(cmd);//也就是值1或者2
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

//函数指针
typedef int(*fun_ptr)(int,const char*,char*);
//定义硬件类型信息
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
	{//直接发送灯的电平给stm32,0亮1灭
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
	},//小车控制
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

   @:Test_CommandResponse():给华为云反馈操作结果的处理函数
	***云端下发命令（带有 requestId），设备必须明确回应，否则平台认为失败
	***响应需要告诉平台：执行成功还是失败（result_code），并可附带额外信息。
*/
static void Test_CommandResponse(char *requestId)
{
    //指定额外上报的有效载荷信息，可以不用指定
    char *commandResponse = "{\"device_ctrl\": \"over\"}"; 
    //指定操作的结果:0成功,1失败
    int result_code = 0;
    //随意指定一个反馈结果的名称
    char *response_name = "cmdResponses";
    //调用SDK函数上报操作结果
    int messageId = IOTA_CommandResponse(requestId, result_code, 
            response_name, commandResponse, NULL);
    if (messageId != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: Test_CommandResponse() failed, messageId %d\n", messageId);
    }
}

/*
一个 report_device_data() 的封装函数：
    @:上报灯数据信息给华为云服务器
    - payload = 格式化拼接(property_name, value)，即：payload = {"属性名": 属性值}
    - services[i] 填充(service_id, properties, event_time)
    - 再把 payload 封装进 services[] 结构体，调用 IOTA_PropertiesReport 上报。
*/
static void report_device_data(char *property_name, char *value)
{
    /*1：定义上报结构体数组*/
    const int serviceNum = 1; 
    ST_IOTA_SERVICE_DATA_INFO services[serviceNum];

    // --------------- the data of service1 -------------------------------
    //拼接json有效载荷
    char payload[200] = {0};//有效载荷
    //注意:x6818_temp就是属性列表中的属性名称，到华为云查看，并且根据自己的名称修改
    sprintf(payload, "{\"%s\":%s}", property_name, value);//字符串拼接（ 属性需要""，所以需要转移\"\"）
    printf("%s: payload: %s\n", __func__, payload);

    /*2：填充上报结构体,指定了上报时间，产品名以及有效载荷*/
    services[0].event_time = GetEventTimesStamp(); 
    /*这个属性必须改成自己的产品名*/
    services[0].service_id = "ehome"; 
    services[0].properties = payload; 

    /*3：调用 SDK上报:通过调用IOTA_PropertiesReport()上报文件给华为云*/
    int messageId = IOTA_PropertiesReport(services, serviceNum, 0, NULL);
    if (messageId != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: Test_PropertiesReport() failed, messageId %d\n", messageId);
    }
    /*4：SDK要求释放 event_time内存*/
    MemFree(&services[0].event_time);
}

//自定义字符串处理函数，去掉\n\t，方便字符串比较
static void mystrtok(char *destination, char *source) {
    int i, j = 0;
    for(i = 0; source[i] != '\0'; i++) {
        if((source[i] != ' ') && (source[i] != '\n') && (source[i] != '\t')) { // 当字符不是空格时
            destination[j++] = source[i]; // 复制字符到destination并递增j
        }
    }
    destination[j] = '\0'; // 确保新字符串正确终止
}

//处理云端下发的“命令”消息（EN_IOTA_COMMAND），驱动底层硬件
//在硬件操作完成后，上报属性变化（report_device_data）并给平台一个命令执行反馈（Test_CommandResponse）
/*
    		HandleCommandRequest：
                        @:解析命令内容（command_name、paras 等）
			@：操作硬件（开灯、关灯、上报数据）
			@：给云端返回执行结果（Test_CommandResponse）
 */
static void HandleCommandRequest(EN_IOTA_COMMAND *command)
{
    if (command == NULL) {
        return;
    }
    /*调试打印推送消息中的各个字段信息
    @:假设华为云发送命令的有效载荷如下：
    @:"{"paras":{"value":"1"/"0"},"service_id":"ehome","command_name":"x6818_bed_room_led_ctrl"}"
    	--打印产品名称+打印有效载荷中的命令名称command_name
    	--打印命令的值+打印消息的编号,每个下发的消息都有一个唯一的此编号
    */
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), service_id: %s\n", command->service_id);
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), command_name: %s\n", command->command_name);
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), paras: %s\n", command->paras);
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: HandleCommandRequest(), request_id %s\n", command->request_id);
    
    
 
    ///解析命令参数：x6818一旦收到各种硬件操作命令，接下来解析命令操作硬件即可
    //由于华为云发送的有效载荷数据格式为：{　换行\n　TAB键\t "value":TAB键\t "1" 换行\n }
    //{ 
    //        "value":        "1"
    //}
    // ---------- 将原始 paras 归一化到 payload ----------
    char payload[200] = {0}; //暂存处理后的有效载荷
    mystrtok(payload, command->paras);
    printf("******payload: %s *********\n", payload);
    
    
   			 /*操作硬件、上报数据*/
    // ---------- 根据命令名与 payload 做具体业务逻辑（驱动硬件） ----------
    int i;
    char ctrl_info[200]={0};
    for(i=0;i<SIZE;i++){
        if(!strcmp(command->command_name,hard_info[i].cmd_name)) {
                hard_info[i].ptr(hard_info[i].arg1,payload,ctrl_info);
				//如果命令名称和属性名称不匹配,多一步判断
                if(!strcmp(hard_info[i].cmd_name,"emotor_car_control")){
                    report_device_data("emotor_state",ctrl_info);
                    break;
                } 
				if(!strcmp(hard_info[i].cmd_name, "x6818_servo_ctrl")){
					report_device_data("x6818_servo",ctrl_info);
					break;
				}
                //上报华为云，需要重点分析
                report_device_data(command->command_name, ctrl_info);
                break;
        }
    }
    
    //交互机制是 请求-响应模式,操作完硬件之后给华为云一个执行命令结果
    //参数表示给第几个推送消息反馈结果
    Test_CommandResponse(command->request_id); // response command
}


//初始化 SDK 的认证配置
//告诉华为云：“我这个设备是谁、用什么方式认证、连哪台服务器、走什么端口。”
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
	//暂存stm32发的数据
	unsigned char buf[MSG_BUF_LEN]={0};
	//解析消息类型
	msghead_t msghead;
	while(1){
		printf("stm32 connect x6818\n");
		fflush(stdout);
		sleep(2);

		if(read(client_sockfd,buf,MSG_BUF_LEN)<=0){
			printf("read client data fails\n");
			exit(-1);
		}
		//获取消息头区分硬件
		memcpy(&msghead, buf, sizeof(msghead));
		printf("stm32 send messages:%d\n",msghead.msgid);
		//依据msghead的头消息字段区分硬件
		switch(msghead.msgid){
			case STM32_TO_X6818_TEMP_REQ://获取温度
				{
					char value[200]={0};
					temp_req_t temp_req;//这里是stm32主动给x6818上报温度
					memcpy(&temp_req ,buf, sizeof(temp_req));
					printf("*****stm32 temp DS18B20 is %.2f******\n",temp_req.temp);
					sprintf(value,"%.2f",temp_req.temp);//输出
					report_device_data("stm32_ds18b20" ,value);
					break;
				}
		}
	}
	return NULL;
}
/****建立TCP服务器并且等待STM32连接****/
//这里是单连接，多请求
static void create_tcp_server(void){
	server_sockfd = socket(AF_INET,SOCK_STREAM,0);
	if(server_sockfd == -1){
		perror("create x6818 server fails");
		exit(-1);
	}
	//端口复用
	int yes = 1;
	if(setsockopt(server_sockfd,SOL_SOCKET, SO_REUSEADDR,&yes,sizeof(yes))==-1){
		perror("setsockopt fails!");
		close(server_sockfd);
		exit(-1);
	}
	//绑定端口
	struct sockaddr_in addr;
	addr.sin_family=AF_INET;
	addr.sin_port=htons(8080);
	addr.sin_addr.s_addr=INADDR_ANY;
	if(bind(server_sockfd,(struct sockaddr*)&addr,sizeof(addr))==-1){
		perror("bind failed!");
		exit(-1);
	}
	//设置监听
	if(listen(server_sockfd,20)==-1){
		perror("listen fails");
		exit(-1);
	}
	printf("\n**************x6818 is waiting for the connection of STM32***********\n");
	//等待客户端连接
	struct sockaddr_in client_addr;
	socklen_t addrLen =sizeof(client_addr);
	client_sockfd=accept(server_sockfd,(struct sockaddr*)&client_addr,&addrLen);
	if(client_sockfd==-1){
		perror("accept fail");
		exit(-1);
	}
	/*****打印输出信息****/
	printf("\n**************stm32 connects x6818 successful**************\n");
	printf("client ip:%s,client port:%d\n",
			inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));
	//创建线程跟stm32通信
	pthread_t thread;
	int ret=pthread_create(&thread,NULL,thread_stm32,NULL);
	if(ret!=0){
		fprintf(stderr,"create thread for stm32 failed:%s\n",strerror(ret));
		exit(-1);
	}
}

/*main()函数*/
int main(int argc, char **argv)
{
    //硬件初始化:调用LED库函数打开LED 
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
	/*舵机初始化*/
	servo_init();
	servo_move(1,position,1000);//id+位置+时间
    
    PrintfLog(EN_LOG_LEVEL_INFO, "device_demo: start test ===================>\n");

    /*SDK初始化*/
    //1：初始化连接服务器信息:端口，IP地址
    if (IOTA_Init(g_workPath) < 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: IOTA_Init() error, init failed\n");
        return 1;
    }
    //2：这里把初始化和配置分开,先初始化后配置
    SetAuthConfig();

     //X6818连接华为云服务器,让设备与华为云建立 MQTT连接，并阻塞等待直到成功
     //@:第一步 (IOTA_Connect())：确认 SDK 启动了“连接动作”。返回值：只说明“请求有没有成功发出去”
    int ret = IOTA_Connect();
    if (ret != 0) {
        PrintfLog(EN_LOG_LEVEL_ERROR, "device_demo: IOTA_Auth() error, Auth failed, result %d\n", ret);
    }
    //@:第二步 (IOTA_IsConnected())：轮询等待连接结果。返回 true表示 SDK已经完成握手并确认连上云端
    while(!IOTA_IsConnected()) {
        TimeSleep(300);
    }

    //注册专门处理华为云给X6818下发的推送消息处理函数HandleCommandRequest
    //IOTA_SetCmdCallback是华为云 SDK提供的函数，指定“收到云端命令时调用的回调函数处理”
    /*用户只做两件事：
              1:注册回调函数IOTA_SetCmdCallback(HandleCommandRequest);
	      2:实现回调函数 HandleCommandRequest()，处理具体命令、操作硬件、上报数据。
    */
    IOTA_SetCmdCallback(HandleCommandRequest);

    //调用连接:stm32和x6818
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
    
     /*主循环什么也不做，通常是守护进程，永远在线。因为命令处理和数据上报都由 回调或独立线程 完成
       while(1) {   TimeSleep(1000); } */
    return 0;
}
