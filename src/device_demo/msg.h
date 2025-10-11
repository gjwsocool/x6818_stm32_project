#ifndef __MSG_H
#define __MSG_H

//请求REQ,回复RSP
#define X6818_TO_STM32_LED_REQ   1
#define STM32_TO_X6818_LED_RSP   2

#define STM32_TO_X6818_TEMP_REQ  3
#define X6818_TO_STM32_TEMP_RSP  4

#define X6818_TO_STM32_BEEP_REQ   5
#define STM32_TO_X6818_BEEP_RSP   6

#define X6818_TO_STM32_EMOTOR_REQ   7
#define STM32_TO_X6818_EMOTOR_RSP   8

//电机操作命令:四个方向,两个速度,停止
#define EMOTOR_FRONT            1    
#define EMOTOR_BACK             2
#define EMOTOR_LEFT             3
#define EMOTOR_RIGHT            4
#define EMOTOR_SPEED_UP         5
#define EMOTOR_SPEED_DOWN       6
#define EMOTOR_STOP             7

//针对一个舵机的两个方向
#define SERVO_FRONT  1
#define SERVO_BACK   2
//消息头:|消息id|消息长度|
typedef struct{
	unsigned int msgid;
	unsigned int msglen;
}msghead_t;

//网关x6818给stm32发送LED开关灯命令
typedef struct{
	msghead_t msgh;
	unsigned int index;//灯的编号
	unsigned int cmd;//灯的命令
}led_req_t;
//stm32给网关x6818的led开关反馈
typedef struct{
	msghead_t msgh;
	unsigned int success;
}led_rsp_t;

//网关x6818给stm32发送蜂鸣器命令
typedef struct{
	msghead_t msgh;
	unsigned int cmd;//灯的命令
}beep_req_t;
//stm32给网关x6818的发送蜂鸣器反馈
typedef struct{
	msghead_t msgh;
	unsigned int success;
}beep_rsp_t;

//stm32给网关x6818声明温度上报信息
typedef struct{
	msghead_t msgh;
	float temp;
}temp_req_t;
//网关x6818给stm32反馈温度上报
typedef struct{
	msghead_t msgh;
	unsigned int success;
}temp_rsp_t;

//网关x6818给stm32发送小车emotor控制命令
typedef struct{
	msghead_t msgh;
	unsigned int cmd;
}emotor_req_t;
//stm32给网关x6818发送小车emotor操作后的反馈信息
typedef struct{
	msghead_t msgh;
	unsigned int success;
}emotor_rsp_t;
#endif
