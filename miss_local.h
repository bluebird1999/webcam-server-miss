/*
 * miss_local.h
 *
 *  Created on: Aug 15, 2020
 *      Author: ning
 */

#ifndef SERVER_MISS_LOCAL_H_
#define SERVER_MISS_LOCAL_H_

/*
 * header
 */
#include "../../manager/manager_interface.h"
#include "miss_session_list.h"
#include <pthread.h>
#include "miss.h"
/*
 * define
 */
#define MAX_CLIENT_NUMBER   		3
#define MAX_SESSION_NUMBER 			(128)
#define MAX_AUDIO_FRAME_LEN 		(50*1024)
#define MAX_VIDEO_FRAME_LEN 		(500*1024)

#define 	MISS_MSG_MAX_NUM 			10
#define 	MISS_MSG_TIMEOUT 			(5)

#define		THREAD_VIDEO				0
#define		THREAD_AUDIO				1

#define		MISS_ASYN_VIDEO_START		0x00
#define		MISS_ASYN_VIDEO_STOP		0x01
#define		MISS_ASYN_VIDEO_CTRL		0x02
#define		MISS_ASYN_AUDIO_START		0x10
#define		MISS_ASYN_AUDIO_STOP		0x11
#define		MISS_ASYN_AUDIO_CTRL		0x12
#define		MISS_ASYN_AUDIO_FORMAT		0x13
#define		MISS_ASYN_SPEAKER_START		0x20
#define		MISS_ASYN_SPEAKER_STOP		0x21
#define		MISS_ASYN_SPEAKER_CTRL		0x22
#define		MISS_ASYN_SPEAKER_FORMAT	0x23
#define		MISS_ASYN_PLAYER_START		0x24
#define		MISS_ASYN_PLAYER_STOP		0x25
#define		MISS_ASYN_MOTOR_CTRL		0x26
#define		MISS_ASYN_PLAYER_SET		0x27

#define		MISS_INIT_CONDITION_NUM					3
#define		MISS_INIT_CONDITION_CONFIG				0
#define		MISS_INIT_CONDITION_MIIO_CONNECTED		1
#define		MISS_INIT_CONDITION_MIIO_DID			2
/*
 * structure
 */
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

typedef struct client_session_t{
	int use_session_num;
	int miss_server_init;
    struct list_handle head;
}client_session_t;

typedef enum stream_status_t {
	STREAM_NONE = 0,
	STREAM_START,
	STREAM_STOP
} stream_status_t;

typedef enum stream_source_type_t {
	SOURCE_NONE =  0,
	SOURCE_LIVE,
	SOURCE_PLAYER,
} stream_source_type_t;

typedef struct session_node_t{
    miss_session_t *session;
    int id;/*current session id*/
    pthread_t video_tid;
    pthread_t audio_tid;
    stream_status_t	video_status;
    stream_status_t	audio_status;
    int	video_channel;
    int audio_channel;
    struct list_handle 		list;
    stream_source_type_t	source;
    char	lock;
}session_node_t;

typedef struct {
    int msg_num;
    time_t timestamps[MISS_MSG_MAX_NUM];
    void *rpc_id[MISS_MSG_MAX_NUM];
    int msg_id[MISS_MSG_MAX_NUM];
} miss_msg_t;

enum cmdtype {
	GET_RECORD_FILE = 1,
	GET_RECORD_TIMESTAMP = 2,
	GET_RECORD_DATE = 3,
	GET_RECORD_PICTURE = 5,
	GET_RECORD_MSG = 6
};

/*
 * function
 */
int miss_session_start(void);
int miss_session_exit(void);
int miss_sessoin_add(miss_session_t *session);
int miss_session_del(miss_session_t *session);
int miss_cmd_video_start(int session_id, miss_session_t *session, char *param);
int miss_cmd_video_stop(int session_id, miss_session_t *session,char *param);
int miss_cmd_audio_start(int session_id, miss_session_t *session,char *param);
int miss_cmd_audio_stop(int session_id, miss_session_t *session,char *param);
int miss_cmd_speaker_start(int session_id, miss_session_t *session,char *param);
int miss_cmd_speaker_stop(int session_id, miss_session_t *session,char *param);
int miss_cmd_video_ctrl(int session_id, miss_session_t *session,char *param);
int miss_cmd_audio_get_format(int session_id, miss_session_t *session,char *param);
int miss_cmd_player_ctrl(int session_id, miss_session_t *session, char *param);
int miss_cmd_player_set_speed(int session_id, miss_session_t *session, char *param);
int miss_cmd_motor_ctrl(int session_id, miss_session_t *session,char *param);
void* miss_get_context_from_id(int id);

#endif /* SERVER_MISS_LOCAL_H_ */
