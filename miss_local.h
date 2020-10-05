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

/*
 * define
 */
#define MISS_MSG_MAX_NUM 			10
#define MISS_MSG_TIMEOUT 			(5)

#define	THREAD_VIDEO				0
#define	THREAD_AUDIO				1

/*
 * structure
 */
typedef struct client_session_t{
	int use_session_num;
	int miss_server_init;
    struct list_handle head;
}client_session_t;

typedef struct {
    int msg_num;
    time_t timestamps[MISS_MSG_MAX_NUM];
    void *rpc_id[MISS_MSG_MAX_NUM];
    int msg_id[MISS_MSG_MAX_NUM];
} miss_msg_t;

enum cmdtype {
		GET_RECORD_FILE = 1,
//      GET_RECORD_TIMESTAMP,
//      GET_FOREVER_TIMESTAMP = 4,
		GET_RECORD_PICTURE = 5,
		GET_RECORD_MSG = 6
};

/*
 * function
 */
client_session_t *server_miss_get_client_info(void);
server_info_t *server_miss_get_server_info(void);
void *session_stream_send_video_func(void *arg);
void *session_stream_send_audio_func(void *arg);
int server_miss_get_info(int SID, miss_session_t *session, char *buf);

#endif /* SERVER_MISS_LOCAL_H_ */
