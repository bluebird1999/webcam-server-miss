/*
 * miss_interface.h
 *
 *  Created on: Aug 28, 2020
 *      Author: ning
 */

#ifndef SERVER_MISS_MISS_INTERFACE_H_
#define SERVER_MISS_MISS_INTERFACE_H_

/*
 * header
 */
#include "../../manager/manager_interface.h"

/*
 * define
 */
#define		SERVER_MISS_VERSION_STRING			"alpha-4.1"

#define		MSG_MISS_BASE						(SERVER_MISS<<16)
#define		MSG_MISS_SIGINT						(MSG_MISS_BASE | 0x0000)
#define		MSG_MISS_SIGINT_ACK					(MSG_MISS_BASE | 0x1000)
#define		MSG_MISS_VIDEO_DATA					(MSG_MISS_BASE | 0x0010)
#define		MSG_MISS_AUDIO_DATA					(MSG_MISS_BASE | 0x0011)
#define		MSG_MISS_RPC_SEND					(MSG_MISS_BASE | 0X0020)
#define		MSG_MISS_RPC_SEND_ACK				(MSG_MISS_BASE | 0X1020)
#define		MSG_MISS_CMD						(MSG_MISS_BASE | 0X0021)
#define		MSG_MISS_CMD_ACK					(MSG_MISS_BASE | 0X1021)
#define		MSG_MISS_RDT						(MSG_MISS_BASE | 0X0022)
#define		MSG_MISS_RDT_ACK					(MSG_MISS_BASE | 0X1022)
#define		MSG_MISS_SESSION_STATUS				(MSG_MISS_BASE | 0X0023)
#define		MSG_MISS_SESSION_STATUS_ACK			(MSG_MISS_BASE | 0X1023)
#define		MSG_MISS_SERVER_STATUS				(MSG_MISS_BASE | 0X0024)
#define		MSG_MISS_SERVER_STATUS_ACK			(MSG_MISS_BASE | 0X1024)

#define 	MAX_SESSION_NUMBER   		3

#define		MISS_ASYN_VIDEO_START			0x00
#define		MISS_ASYN_VIDEO_STOP			0x01
#define		MISS_ASYN_VIDEO_CTRL			0x02
#define		MISS_ASYN_AUDIO_START			0x10
#define		MISS_ASYN_AUDIO_STOP			0x11
#define		MISS_ASYN_AUDIO_CTRL			0x12
#define		MISS_ASYN_AUDIO_FORMAT			0x13
#define		MISS_ASYN_SPEAKER_START			0x20
#define		MISS_ASYN_SPEAKER_STOP			0x21
#define		MISS_ASYN_SPEAKER_CTRL			0x22
#define		MISS_ASYN_SPEAKER_FORMAT		0x23
#define		MISS_ASYN_PLAYER_START			0x24
#define		MISS_ASYN_PLAYER_STOP			0x25
#define		MISS_ASYN_MOTOR_CTRL			0x26
#define		MISS_ASYN_PLAYER_SET			0x27
#define		MISS_ASYN_PLAYER_AUDIO_START	0x28
#define		MISS_ASYN_PLAYER_AUDIO_STOP		0x29
#define		MISS_ASYN_PLAYER_REQUEST		0x30
#define		MISS_ASYN_PLAYER_FINISH			0x31

#define		MISS_LOCAL_ERR_NO_DATA					128
#define		MISS_LOCAL_ERR_MISS_GONE				129
#define		MISS_LOCAL_ERR_SESSION_GONE				130
#define		MISS_LOCAL_ERR_AV_NOT_RUN				131
#define		MISS_LOCAL_ERR_PARAM					132
/*
 * structure
 */
enum cmdtype {
	GET_RECORD_FILE = 1,
	GET_RECORD_TIMESTAMP = 2,
	GET_RECORD_DATE = 3,
	GET_RECORD_PICTURE = 5,
	GET_RECORD_MSG = 6
};

typedef struct miss_date_time_t {
    unsigned long    dwYear;
    unsigned long    dwMonth;
    unsigned long    dwDay;
    unsigned long    dwHour;
    unsigned long    dwMinute;
    unsigned long    dwSecond;
} miss_date_time_t;

typedef struct miss_playlist_t {
	uint32_t       		recordType;
	uint32_t	    	channel;
	uint32_t       		deviceId;
	miss_date_time_t	startTime;
	miss_date_time_t	endTime;
	uint32_t        	totalNum;
} miss_playlist_t;
/*
 * function
 */
int server_miss_start(void);
int server_miss_message(message_t *msg);
int server_miss_video_message(message_t *msg);
int server_miss_audio_message(message_t *msg);

#endif /* SERVER_MISS_MISS_INTERFACE_H_ */
