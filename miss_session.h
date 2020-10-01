/*
 * miss_session.h
 *
 *  Created on: Aug 15, 2020
 *      Author: ning
 */

#ifndef SERVER_MISS_MISS_SESSION_H_
#define SERVER_MISS_MISS_SESSION_H_

/*
 * header
 */
#include <pthread.h>
#include "miss.h"
#include "miss_session_list.h"

/*
 * define
 */
#define MAX_CLIENT_NUMBER   		3
#define MAX_SESSION_NUMBER 			(128)
#define MAX_AUDIO_FRAME_LEN 		(50*1024)
#define MAX_VIDEO_FRAME_LEN 		(500*1024)
/*
 * structure
 */
typedef enum stream_status_t {
	STREAM_NONE = 0,
	STREAM_START,
	STREAM_STOP
} stream_status_t;

typedef struct session_node_t{
    miss_session_t *session;
    int id;/*current session id*/

    pthread_t video_tid;
    pthread_t audio_tid;

    stream_status_t	video_status;
    stream_status_t	audio_status;

    int	video_channel;
    int audio_channel;

    struct list_handle list;
}session_node_t;


/*
 * function
 */
int miss_session_start(void);
int miss_session_exit(void);
int miss_session_add(miss_session_t *session);
int miss_session_del(miss_session_t *session);

int miss_session_video_start(int session_id, miss_session_t *session, char *param);
int miss_session_video_stop(int session_id, miss_session_t *session,char *param);
int miss_session_audio_start(int session_id, miss_session_t *session,char *param);
int miss_session_audio_stop(int session_id, miss_session_t *session,char *param);
int miss_session_speaker_start(miss_session_t *session,char *param);
int miss_session_speaker_stop(miss_session_t *session,char *param);
int miss_session_video_ctrl(miss_session_t *session,char *param);

#endif /* SERVER_MISS_MISS_SESSION_H_ */
