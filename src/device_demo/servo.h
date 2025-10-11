#ifndef __SERVO_H_
#define  __SERVO_H_

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <termios.h>//terminal+io+setting

/*对外暴露接口*/

extern int servo_init(void);
//写接口---让id编号的舵机在time时间内转动position角度
extern int servo_move(int id,int position,int time);
//读接口---获取当前id的舵机角度
extern int servo_get_position(int id);

#endif
