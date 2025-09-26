#ifndef __LED_H
#define __LED_H

#ifdef __cplusplus
extern "C" {
#endif

/*头文件*/
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

/*调试宏*/
#define DEBUG

#ifdef DEBUG
#define pr_debug(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) 
#endif

/*设备文件名*/
#define LED_DEVFILE_NAME	"/dev/myled"

/*LED控制命令宏*/
#define LED_ON	0x100001
#define LED_OFF	0x100002

/*
 	函数功能:打开LED设备
	参数：无
	返回值：打开设备成功，返回设备文件描述符fd;失败返回-1 
*/
extern int led_open(void);

/*
	函数功能:关闭LED设备
	参数：
		@fd:设备文件描述符
	返回值：无
*/
extern void led_close(void);

/*
	函数功能：控制LED亮，灭
	参数：
		@fd:设备文件描述符;
	        @cmd:控制LED开关命令
        返回值：开，关灯结果，操作成功，返回0，否则返回-1
*/
extern int led_config(int index, int cmd);

#ifdef __cplusplus
}
#endif

#endif
