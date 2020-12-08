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
#define 	MISS_ACK_SUC_TEMPLATE 		"{\"id\":%d,\"result\":[\"OK\"]}"
#define 	MISS_ACK_ERR_TEMPLATE 		"{\"id\":%d,\"error\":{\"code\":-33020,\"message\":\"%s\"}}"

#define 	MISS_MSG_MAX_NUM 			10
#define 	MISS_MSG_TIMEOUT 			(5)

#define		THREAD_VIDEO				0
#define		THREAD_AUDIO				4

#define		MISS_INIT_CONDITION_NUM					3
#define		MISS_INIT_CONDITION_CONFIG				0
#define		MISS_INIT_CONDITION_MIIO_CONNECTED		1
#define		MISS_INIT_CONDITION_MIIO_DID			2

#define		MISS_EXIT_CONDITION						0

#define		MISS_LOCAL_MAX_NO_BUFFER_TIMES			5
#define		MISS_TASK_TIMEOUT						100

/*
 * structure
 */
typedef struct client_session_t{
	int use_session_num;
	int miss_server_init;
	int miss_server_ready;
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

typedef struct session_task_t {
	void			*func;
	char			status;
	char			old_status;
	char			msg_lock;
	unsigned int	timeout;
} session_task_t;

typedef struct session_node_t{
    miss_session_t *session;
    int id;/*current session id*/
    pthread_t video_tid;
    pthread_t audio_tid;
    stream_status_t	video_status;
    stream_status_t	audio_status;
    int	video_channel;
    int audio_channel;
    int video_frame;
    int audio_frame;
    struct list_handle 		list;
    stream_source_type_t	source;
    char			lock;
    char			video_switch;
    char			video;
    char			audio_switch;
    char			audio;
    session_task_t	task;
}session_node_t;

typedef void (*SESSION_TASK)(session_node_t *);

typedef struct {
    int msg_num;
    time_t timestamps[MISS_MSG_MAX_NUM];
    void *rpc_id[MISS_MSG_MAX_NUM];
    int msg_id[MISS_MSG_MAX_NUM];
} miss_msg_t;

typedef enum session_status_t {
	SESSION_STATUS_ADD = 0,
	SESSION_STATUS_REMOVE,
	SESSION_STATUS_ERROR,
};
/*
 * function
 */
void* miss_get_context_from_id(int id);

#endif /* SERVER_MISS_LOCAL_H_ */
