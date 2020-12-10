/*
 * miss.c
 *
 *  Created on: Aug 27, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bits/socket.h>
#include <sys/time.h>
#include <sys/prctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
#include <json-c/json.h>
#include <rtsvideo.h>
#include <rtsaudio.h>
#include <malloc.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../server/player/player_interface.h"
#include "../../server/speaker/speaker_interface.h"
#include "../../server/device/device_interface.h"
//server header
#include "miss.h"
#include "miss_interface.h"
#include "miss_session_list.h"
#include "miss_local.h"
#include "config.h"

/*
 * static
 */
//variable
static server_info_t 		info;
static message_buffer_t		message;
static message_buffer_t		video_buff[MAX_SESSION_NUMBER];
static message_buffer_t		audio_buff[MAX_SESSION_NUMBER];
static miss_config_t		config;
static client_session_t		client_session;
static pthread_rwlock_t		ilock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t		mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t		vmutex[MAX_SESSION_NUMBER] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t		vcond[MAX_SESSION_NUMBER] = {PTHREAD_COND_INITIALIZER};
static pthread_mutex_t		amutex[MAX_SESSION_NUMBER] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t		acond[MAX_SESSION_NUMBER] = {PTHREAD_COND_INITIALIZER};
static player_init_t  		player[MAX_SESSION_NUMBER];

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void task_default(void);
static void session_task_none(session_node_t *node);
static void session_task_live(session_node_t *node);
static void session_task_playback(session_node_t *node);
static void task_exit(void);
static void server_release_1(void);
static void server_release_2(void);
static void server_release_3(void);
static int server_set_status(int type, int st, int value);
static void server_thread_termination(void);
//specific
static int miss_server_connect(void);
static int miss_server_disconnect(void);
static int session_send_video_stream(session_node_t* node, av_packet_t *packet);
static int session_send_audio_stream(session_node_t* node, av_packet_t *packet);
static session_node_t *miss_session_check_node(miss_session_t *session);
static miss_session_t *miss_session_get_node_id(int sid);
static void *session_stream_send_audio_func(void *arg);
static void *session_stream_send_video_func(void *arg);
static int miss_message_callback(message_t *arg);
static int miss_clean_stream(session_node_t *psession_node);
static void miss_broadcast_thread_exit(void);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */


/*
 * helper
 */
static session_node_t *miss_session_check_node(miss_session_t *session)
{
	client_session_t* pclient_session = &client_session;
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->session == session) {
        	return psession_node;
        }
    }
    return NULL;
}

static session_node_t *miss_session_check_node_by_id(int sid)
{
	client_session_t* pclient_session = &client_session;
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->id == sid) {
        	return psession_node;
        }
    }
    return NULL;
}

static miss_session_t *miss_session_get_node_id(int sid)
{
	client_session_t* pclient_session = &client_session;
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->id == sid) {
        	return psession_node->session;
        }
    }
    return NULL;
}

static int miss_rdt_send_msg(miss_session_t *session, void *msg, int len)
{
    int ret = 0;
    const int max_payload_len = 2048;
    int slicenum = 0;
    int sliceseq = 1;
    char* slice_buf = NULL;
    int slice_size = 0;
    if((len % max_payload_len) == 0) {
        slicenum = len / max_payload_len;
    }
    else {
        slicenum = len / max_payload_len + 1;
    }

    for(sliceseq = 1; sliceseq <= slicenum; sliceseq++) {
        slice_buf = (char*)(msg + (sliceseq - 1) * max_payload_len);
        slice_size = (sliceseq < slicenum) ? max_payload_len : (len - (sliceseq - 1) * max_payload_len);
        ret = miss_rdt_send(session, (void *)slice_buf, slice_size);
        if(ret == 0) {
            //log_debug("Send file OK!\n");
        	usleep(1000); //pause
        }
        else {
            log_err("send file faild! ret = %d\n", ret);
        }
    }
	return 0;
}

static int miss_get_player_infomation_ack(message_t *msg)
{
	int ret = 0;
	miss_session_t	*session;
	pthread_rwlock_rdlock(&ilock);
	session = msg->arg_pass.handler;
	if( session == NULL ) {
		log_qcy(DEBUG_SERIOUS, "session %p with id %d isn't find!", msg->arg_pass.handler, msg->arg_pass.wolf);
		pthread_rwlock_unlock(&ilock);
		return -1;
	}
	if( !msg->result ) {
		ret = miss_rdt_send_msg(session, msg->arg, msg->arg_size);
	}
	else {
	}
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static int miss_helper_activate_stream(int id, int type)
{
	if(type == 0) {
		pthread_mutex_lock(&vmutex[id]);
		pthread_cond_broadcast(&vcond[id]);
		pthread_mutex_unlock(&vmutex[id]);
	}
	else if(type == 1) {
		pthread_mutex_lock(&amutex[id]);
		pthread_cond_broadcast(&acond[id]);
		pthread_mutex_unlock(&amutex[id]);
	}
}

static int miss_message_callback(message_t *arg)
{
	int ret = 0;
	int code;
	char audio_format[16];
	int temp;
	message_t	msg;
	session_node_t *pnod;
	miss_session_t	*session;
	pthread_rwlock_wrlock(&ilock);
	pnod = miss_session_check_node(arg->arg_pass.handler);
	if( pnod == NULL ) {
		log_qcy(DEBUG_SERIOUS, "session %p with id %d isn't find!", arg->arg_pass.handler, arg->arg_pass.wolf);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	session = pnod->session;
	switch( arg->arg_pass.cat) {
		case MISS_ASYN_VIDEO_START:
			log_qcy(DEBUG_INFO, "========start new video stream thread=========");
			pnod->video_status = STREAM_START;
			pnod->video_channel = arg->arg_pass.duck;
			pnod->source = SOURCE_LIVE;
			pnod->lock = 0;
			pnod->video_frame = 0;
			pthread_create(&pnod->video_tid, NULL, session_stream_send_video_func, (void*)pnod);
			break;
		case MISS_ASYN_VIDEO_STOP:
			log_qcy(DEBUG_INFO, "========stop  video stream thread=========");
			pnod->video_tid = -1;
			pnod->video_status = STREAM_NONE;
			if( pnod->audio_status == STREAM_NONE )
				pnod->source = SOURCE_NONE;
			pnod->lock = 0;
			pnod->video_frame = 0;
			miss_helper_activate_stream(pnod->id, 0);
			break;
		case MISS_ASYN_AUDIO_START:
			log_qcy(DEBUG_INFO, "========start new audio stream thread=========");
			pnod->audio_status = STREAM_START;
			pnod->audio_channel = arg->arg_pass.duck;
			pnod->audio_frame = 0;
			pthread_create(&pnod->audio_tid, NULL, session_stream_send_audio_func, (void*)pnod);
			break;
		case MISS_ASYN_AUDIO_STOP:
			log_qcy(DEBUG_INFO, "========stop audio stream thread=========");
			pnod->audio_tid = -1;
			pnod->audio_status = STREAM_NONE;
			if( pnod->video_status == STREAM_NONE )
				pnod->source = SOURCE_NONE;
			pnod->audio_frame = 0;
			miss_helper_activate_stream(pnod->id, 1);
			break;
		case MISS_ASYN_VIDEO_CTRL:
			if( arg->result == 0) code = MISS_NO_ERROR;
			else code = MISS_ERR_CLIENT_NO_SUPPORT;
			ret = miss_cmd_send(session,MISS_CMD_STREAM_CTRL_RESP, (void*)&code, sizeof(int));
			break;
		case MISS_ASYN_AUDIO_FORMAT:
			temp = *((int*)arg->arg);
			memset(audio_format, 0, sizeof(audio_format));
			if( temp == RTS_A_FMT_ALAW ) strcpy(audio_format, "g711a");
			else if( temp == RTS_A_FMT_ULAW ) strcpy(audio_format, "g711u");
			else if( temp == RTS_A_FMT_AUDIO ) strcpy(audio_format, "pcm");
			else if( temp == RTS_A_FMT_MP3 ) strcpy(audio_format, "mp3");
			else if( temp == RTS_A_FMT_AAC ) strcpy(audio_format, "aac");
			else strcpy(audio_format, "unknown");
			if( arg->result == 0)
				code = MISS_NO_ERROR;
			else
				code = MISS_ERR_CLIENT_NO_SUPPORT;
			ret = miss_cmd_send(session,MISS_CMD_GET_AUDIO_FORMAT_RESP, (void*)&audio_format, strlen(audio_format)+1 );
			break;
		case MISS_ASYN_PLAYER_REQUEST:
			if( arg->result == 0 ) {
				pnod->video_channel = arg->arg_pass.duck;
				pnod->lock = 0;
				pnod->source = SOURCE_PLAYER;
				log_qcy(DEBUG_INFO, "========playback setting success=========");
				code = MISS_NO_ERROR;
				ret = miss_cmd_send(session,MISS_CMD_PLAYBACK_RESP, (void*)&code, sizeof(int));
				if( arg->arg_in.cat == 0 )
					pnod->task.status = TASK_WAIT;
				else
					pnod->task.status = TASK_RUN;
			}
			else {
				pnod->task.status = TASK_FINISH;
				log_qcy(DEBUG_INFO, "========playback setting failed=========");
				code = MISS_ERR_CLIENT_NO_SUPPORT;
				ret = miss_cmd_send(session,MISS_CMD_PLAYBACK_RESP, (void*)&code, sizeof(int));
			}
			break;
		case MISS_ASYN_PLAYER_START:
			if( arg->result == 0 ) {
				log_qcy(DEBUG_INFO, "========start new player video stream thread=========");
				pnod->video_status = STREAM_START;
				pthread_create(&pnod->video_tid, NULL, session_stream_send_video_func, (void*)pnod);
			}
			break;
		case MISS_ASYN_PLAYER_STOP:
			if( arg->result == 0 ) {
				log_qcy(DEBUG_INFO, "========stop player video/audio stream thread=========");
				pnod->video_status = STREAM_NONE;
				pnod->audio_status = STREAM_NONE;
				pnod->source = SOURCE_NONE;
				pnod->lock = 0;
				miss_helper_activate_stream(pnod->id, 0);
				miss_helper_activate_stream(pnod->id, 1);
			}
			ret = miss_cmd_send(session, MISS_CMD_PLAYBACK_RESP, (void*)&arg->result, sizeof(int));
			break;
		case MISS_ASYN_PLAYER_AUDIO_START:
			if( arg->result == 0 ) {
				log_qcy(DEBUG_INFO, "========start new player audio stream thread=========");
				pnod->audio_status = STREAM_START;
				pthread_create(&pnod->audio_tid, NULL, session_stream_send_audio_func, (void*)pnod);
			}
			break;
		case MISS_ASYN_PLAYER_AUDIO_STOP:
			if( arg->result == 0 ) {
				log_qcy(DEBUG_INFO, "========stop player audio stream thread=========");
				pnod->audio_status = STREAM_NONE;
				if(pnod->video_status == STREAM_NONE)
					pnod->source = SOURCE_NONE;
				miss_helper_activate_stream(pnod->id, 1);
			}
			break;
		case MISS_ASYN_SPEAKER_START:
			if( arg->result == 0) code = MISS_NO_ERROR;
			else code = MISS_ERR_CLIENT_NO_SUPPORT;
			ret = miss_cmd_send(session, MISS_CMD_SPEAKER_START_RESP, (void*)&arg->result, sizeof(int));
			break;
		case MISS_ASYN_PLAYER_FINISH:
			pnod->video_status = STREAM_NONE;
			pnod->audio_status = STREAM_NONE;
			pnod->source = SOURCE_NONE;
			miss_helper_activate_stream(pnod->id, 0);
			miss_helper_activate_stream(pnod->id, 1);
			pnod->task.status = TASK_FINISH;
			log_qcy(DEBUG_INFO, "========playback finished!=========");
			break;
		case MISS_ASYN_SPEAKER_STOP:
		case MISS_ASYN_SPEAKER_CTRL:
			break;
		case MISS_ASYN_PLAYER_SET:
			break;
	}
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static void *session_stream_send_video_func(void *arg)
{
	session_node_t *node;
	int sid;
    int ret;
    message_t	msg;
    char		fname[MAX_SYSTEM_STRING_SIZE];
    pthread_rwlock_rdlock(&ilock);
	node =(session_node_t*)arg;
	if(!node->session) {
		pthread_rwlock_unlock(&ilock);
		goto exit;
	}
	sid = node->id;
	sprintf(fname, "misv-%d-%d-%d", sid, node->source, node->video_channel);
	pthread_rwlock_unlock(&ilock);
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name(fname);
    pthread_detach(pthread_self());
	msg_buffer_init2(&video_buff[sid], MSG_BUFFER_OVERFLOW_YES, &vmutex[sid]);
	server_set_status(STATUS_TYPE_THREAD_START, (THREAD_VIDEO + sid), 1);
    while( 1 ) {
    	pthread_rwlock_rdlock(&ilock);
    	if( info.exit || (misc_get_bit(info.thread_exit,  (THREAD_VIDEO + sid))) ) {
    		pthread_rwlock_unlock(&ilock);
    		break;
    	}
    	if( !node->session ||
    			(node->video_status != STREAM_START) ) {
    		log_qcy(DEBUG_INFO, "========session video stream quit: session=%p, video_status=%d=========", node->session, node->video_status);
    		pthread_rwlock_unlock(&ilock);
    		break;
    	}
    	pthread_rwlock_unlock(&ilock);
    	//condition
    	pthread_mutex_lock(&vmutex[sid]);
    	if( video_buff[sid].head == video_buff[sid].tail ) {
			pthread_cond_wait(&vcond[sid], &vmutex[sid]);
    	}
    	msg_init(&msg);
    	ret = msg_buffer_pop(&video_buff[sid], &msg);
    	pthread_mutex_unlock(&vmutex[sid]);
    	if( ret ) continue;
    	ret = session_send_video_stream(node,(av_packet_t*)(msg.arg));
    	msg_free(&msg);
    	if( ret == MISS_LOCAL_ERR_NO_DATA ) {
    		continue;
    	}
    	else if(ret == MISS_ERR_INVALID_ARG ) {
    		break;
    	}
    }
exit:
    msg_buffer_release2(&video_buff[sid], &vmutex[sid]);
    server_set_status(STATUS_TYPE_THREAD_START, (THREAD_VIDEO + sid), 0);
    manager_common_send_dummy(SERVER_MISS);
    log_qcy(DEBUG_INFO, "-----------thread exit: server_miss_vstream %s----------", fname);
    pthread_exit(0);
}

static void *session_stream_send_audio_func(void *arg)
{
	session_node_t *node;
	int sid;
    int ret;
    message_t	msg;
    char		fname[MAX_SYSTEM_STRING_SIZE];
    pthread_rwlock_rdlock(&ilock);
	node =(session_node_t*)arg;
	if(!node->session) {
		pthread_rwlock_unlock(&ilock);
		goto exit;
	}
	sid = node->id;
	sprintf(fname, "misa-%d-%d-%d", sid, node->source, node->audio_channel);
	pthread_rwlock_unlock(&ilock);
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name(fname);
    pthread_detach(pthread_self());
	msg_buffer_init2(&audio_buff[sid], MSG_BUFFER_OVERFLOW_YES,&amutex[sid]);
	server_set_status(STATUS_TYPE_THREAD_START, (THREAD_AUDIO + sid), 1);
    while( 1 ) {
    	pthread_rwlock_rdlock(&ilock);
    	if( info.exit || ( misc_get_bit(info.thread_exit,  (THREAD_AUDIO + sid))) ) {
    		pthread_rwlock_unlock(&ilock);
    		break;
    	}
    	if( !node->session ||
    			(node->audio_status != STREAM_START) ) {
    		pthread_rwlock_unlock(&ilock);
    		break;
    	}
		pthread_rwlock_unlock(&ilock);
    	//condition
    	pthread_mutex_lock(&amutex[sid]);
    	if( audio_buff[sid].head == audio_buff[sid].tail ) {
			pthread_cond_wait(&acond[sid], &amutex[sid]);
    	}
    	msg_init(&msg);
    	ret = msg_buffer_pop(&audio_buff[sid], &msg);
    	pthread_mutex_unlock(&amutex[sid]);
    	if( ret ) continue;
    	ret = session_send_audio_stream(node,(av_packet_t*)(msg.arg) );
    	msg_free(&msg);
    	if( ret == MISS_LOCAL_ERR_NO_DATA )  {
    		continue;
    	}
    	else if(ret == MISS_ERR_INVALID_ARG ) {
    		break;
    	}
    }
exit:
    msg_buffer_release2(&audio_buff[sid], &amutex[sid]);
    server_set_status(STATUS_TYPE_THREAD_START, (THREAD_AUDIO + sid), 0);
    manager_common_send_dummy(SERVER_MISS);
    log_qcy(DEBUG_INFO, "-----------thread exit: server_miss_astream %s----------", fname);
    pthread_exit(0);
}

static int session_send_video_stream(session_node_t* node, av_packet_t *packet)
{
    miss_frame_header_t frame_info = {0};
    int ret;
    static int buffer_block = 0;
    pthread_rwlock_rdlock(packet->lock);
    if( ( *(packet->init) == 0 ) ||
    	( packet->data == NULL ) ) {
    	pthread_rwlock_unlock(packet->lock);
    	return MISS_LOCAL_ERR_NO_DATA;
    }
    frame_info.timestamp = packet->info.timestamp;
    frame_info.timestamp_s = packet->info.timestamp/1000;
    frame_info.sequence = packet->info.frame_index;
    frame_info.length = packet->info.size;
    frame_info.codec_id = MISS_CODEC_VIDEO_H264;
    frame_info.flags = packet->info.flag;
	if( (node->video_frame == 0) && (node->audio_frame==0)) {
		miss_session_query(node->session, MISS_QUERY_CMD_SEND_CLEAR_BUFFER, NULL);
		log_qcy(DEBUG_INFO, "clear buffer before send new stream!");
	}
	ret = miss_video_send(node->session, &frame_info, (unsigned char*)(packet->data));
	if ( ret !=0 ) {
		log_qcy(DEBUG_VERBOSE, "send miss-video data error: %d, size: %d", ret, frame_info.length);
		if( ret == MISS_ERR_NO_BUFFER ) {
			if( buffer_block > MISS_LOCAL_MAX_NO_BUFFER_TIMES ) {
				miss_session_query(node->session, MISS_QUERY_CMD_SEND_CLEAR_BUFFER, NULL);
				log_qcy(DEBUG_INFO, "send buffer cleared!");
				buffer_block = 0;
			}
			else {
				buffer_block ++ ;
			}
		}
	}
	else {
		node->video_frame++;
		buffer_block = 0;
	}
	av_packet_sub(packet);
	pthread_rwlock_unlock(packet->lock);
    return ret;
}

static int session_send_audio_stream(session_node_t *node, av_packet_t *packet)
{
    miss_frame_header_t frame_info = {0};
    static int buffer_block = 0;
    int ret;
    pthread_rwlock_rdlock(packet->lock);
    if( ( *(packet->init) == 0 ) ||
    	( packet->data == NULL ) ) {
    	pthread_rwlock_unlock(packet->lock);
    	return MISS_LOCAL_ERR_NO_DATA;
    }
    frame_info.timestamp = packet->info.timestamp;
    frame_info.timestamp_s = packet->info.timestamp/1000;
    frame_info.sequence = packet->info.frame_index;
    frame_info.length = packet->info.size;
    frame_info.codec_id = MISS_CODEC_AUDIO_G711A;
    frame_info.flags = packet->info.flag;
	//send stream to miss
	if( (node->video_frame == 0) && (node->audio_frame==0)) {
		miss_session_query(node->session, MISS_QUERY_CMD_SEND_CLEAR_BUFFER, NULL);
		log_qcy(DEBUG_INFO, "clear buffer before send new stream!");
	}
	ret = miss_video_send(node->session, &frame_info, (unsigned char*)(packet->data));
	if ( ret !=0 ) {
		log_qcy(DEBUG_VERBOSE, "send miss-audio data error: %d, size: %d", ret, frame_info.length);
		if( ret == MISS_ERR_NO_BUFFER ) {
			if( buffer_block > MISS_LOCAL_MAX_NO_BUFFER_TIMES ) {
				miss_session_query(node->session, MISS_QUERY_CMD_SEND_CLEAR_BUFFER, NULL);
				log_qcy(DEBUG_INFO, "send buffer cleared!");
				buffer_block = 0;
			}
			else {
				buffer_block ++ ;
			}
		}
	}
	else {
		node->audio_frame++;
		buffer_block = 0;
	}
	av_packet_sub(packet);
	pthread_rwlock_unlock(packet->lock);
    return ret;
}

static int miss_rpc_send_proc(message_t *msg)
{
	int ret = 0;
	int msg_id = 0;
	if( msg->arg_in.cat == -1 ) {
		ret = miss_rpc_process(NULL, (char*)msg->arg, msg->arg_size-1);
	}
	else {
	    ret = strstr((char*)msg->arg,"error");
	    if (ret != NULL ) {
	    	if( strstr( (char*)msg->arg, "token mismatch") != NULL ) {
	    		if( client_session.miss_server_ready == 0 ) {
	    			config_miss_update_token(&config);
	    			info.status = STATUS_RESTART;
	    			log_qcy(DEBUG_INFO, "-------token mismatch found, restart miss server");
	    		}
	    	}
	    	else if( strstr( (char*)msg->arg, "offline") != NULL ) {
	    		if( client_session.miss_server_ready == 0 ) {
	    			info.status = STATUS_RESTART;
	    			log_qcy(DEBUG_INFO, "-------offline found, try to start miss server");
	    		}
	    	}
	    	else if( strstr( (char*)msg->arg, "try out") != NULL ) {
	    		if( client_session.miss_server_ready == 0 ) {
	    			info.status = STATUS_RESTART;
	    			log_qcy(DEBUG_INFO, "-------tryout found, try to start miss server");
	    		}
	    	}
	    	else if( strstr( (char*)msg->arg, "timeout") != NULL ) {
	    		if( client_session.miss_server_ready == 0 ) {
	    			info.status = STATUS_RESTART;
	    			log_qcy(DEBUG_INFO, "-------timeout found, try to start miss server");
	    		}
	    	}
    		else {
    			log_qcy(DEBUG_INFO, "-------error message found, error = %s", (char*)msg->arg);
    		}
	    }
	    else {
			msg_id = miss_get_context_from_id(msg->arg_in.cat);
			if (NULL != msg_id) {
				log_qcy(DEBUG_INFO, "miss_rpc_process id:%d", msg->arg_in.cat);
				ret = miss_rpc_process(msg_id, (char*)msg->arg, msg->arg_size-1);
				log_qcy(DEBUG_VERBOSE, "--------------- = %s, len = %d", (char*)msg->arg, msg->arg_size-1);
			}
	    }
	}
	if (ret != MISS_NO_ERROR) {
		log_qcy(DEBUG_SERIOUS, "miss_rpc_process err:%d",ret);
	}
	return ret;
}

static int miss_server_set_status(message_t *msg)
{
    int ret = -1;
	client_session.miss_server_ready = msg->arg_in.cat;
    return ret;
}

static int miss_server_connect(void)
{
	int ret = MISS_NO_ERROR;
	char token[2*MAX_SYSTEM_STRING_SIZE] = {0x00};
	char key[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char did[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char model[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char sdk[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char fname[MAX_SYSTEM_STRING_SIZE] = {0};
	miss_server_config_t server = {0};
	miss_device_info_t dev = {0};
	//init client info
    memset(&client_session, 0, sizeof(client_session_t));
    miss_list_init(&client_session.head);
    if( !config.profile.board_type )
    	strcpy(key, config.profile.key);
    strcpy(did, config.profile.did);
    strcpy(model, config.profile.model);
    strcpy(token, config.profile.token);
    strcpy(sdk, config.profile.sdk_type);
    if(token[strlen((char*)token) - 1] == 0xa)
    	token[strlen((char*)token) - 1] = 0;
	server.max_session_num = MAX_SESSION_NUMBER;
	server.max_video_recv_size = config.profile.max_video_recv_size;
	server.max_audio_recv_size = config.profile.max_audio_recv_size;
	server.max_video_send_size = config.profile.max_video_send_size;
	server.max_audio_send_size = config.profile.max_audio_send_size;
	if( !config.profile.board_type ) {
		server.device_key = key;
		server.device_key_len = strlen((char*)key);
	}
	server.device_token = token;
	server.device_token_len = strlen((char*)token);
	server.length = sizeof(miss_server_config_t);
	dev.model = model;
	dev.device_model_len = strlen(model);
	dev.sdk_type = sdk;
	dev.device_sdk_type_len = strlen(sdk);
	dev.did = did;
	dev.did_len = strlen(did);
	miss_log_set_level(MISS_LOG_DEBUG);
	sprintf(fname,"%slog/miss.log", _config_.qcy_path);
	miss_log_set_path(CONFIG_MISS_LOG_PATH);
	ret = miss_server_init(&dev, &server);
	if (MISS_NO_ERROR != ret) {
        log_qcy(DEBUG_SERIOUS, "miss server init fail ret:%d", ret);
        return -1;
	}
	client_session.miss_server_init = 1;
    return 0;
}

static int miss_server_disconnect(void)
{
	pthread_rwlock_wrlock(&ilock);
	if(client_session.miss_server_init == 0) {
		log_qcy(DEBUG_INFO, "miss server is already disconnected is %d!", client_session.miss_server_init);
		pthread_rwlock_unlock(&ilock);
		return 0;
	}
	client_session.miss_server_init = 0;
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(client_session.head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->session) {
            miss_server_session_close(psession_node->session);
            miss_clean_stream(psession_node);
            miss_list_del(&(psession_node->list));
            free(psession_node);
            client_session.use_session_num --;
        }
    }
	miss_server_finish();
	client_session.miss_server_ready = 0;
	pthread_rwlock_unlock(&ilock);
	log_qcy(DEBUG_INFO, "----miss_server disconnected!");
	return 0;
}

static int miss_clean_stream(session_node_t *psession_node)
{
	int ret = 0;
	message_t msg;
	if( psession_node ) {
		if( psession_node->source == SOURCE_LIVE) {
			if( psession_node->video_status == STREAM_START ) {
				psession_node->video_status = STREAM_NONE;
				miss_helper_activate_stream(psession_node->id, 0);
				/********message body********/
				msg_init(&msg);
				msg.message = MSG_VIDEO_STOP;
				msg.sender = msg.receiver = SERVER_MISS;
				manager_common_send_message(SERVER_VIDEO, &msg);
				/****************************/
			}
			if( psession_node->audio_status == STREAM_START ) {
				psession_node->audio_status = STREAM_NONE;
				miss_helper_activate_stream(psession_node->id, 1);
				/********message body********/
				msg_init(&msg);
				msg.message = MSG_AUDIO_STOP;
				msg.sender = msg.receiver = SERVER_MISS;
				manager_common_send_message(SERVER_AUDIO, &msg);
				/****************************/
			}
			psession_node->source = SOURCE_NONE;
		}
		else if( psession_node->source == SOURCE_PLAYER ) {
			if( psession_node->video_status == STREAM_START ||
				psession_node->audio_status == STREAM_START	) {
				psession_node->video_status = STREAM_NONE;
				psession_node->audio_status = STREAM_NONE;
				miss_helper_activate_stream(psession_node->id, 0);
				miss_helper_activate_stream(psession_node->id, 1);
				/********message body********/
				msg_init(&msg);
				msg.message = MSG_PLAYER_STOP;
				msg.sender = msg.receiver = SERVER_MISS;
				manager_common_send_message(SERVER_PLAYER,    &msg);
				/****************************/
			}
			psession_node->source = SOURCE_NONE;
		}
	}
	return ret;
}

static int server_set_status(int type, int st, int value)
{
	int ret=-1;
	pthread_rwlock_wrlock(&ilock);
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		config.status = st;
	else if(type==STATUS_TYPE_THREAD_START)
		misc_set_bit(&info.thread_start, st, value);
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static void miss_broadcast_thread_exit(void)
{
	for(int i=0;i<MAX_SESSION_NUMBER;i++) {
		miss_helper_activate_stream(i, 0);
		miss_helper_activate_stream(i, 1);
	}
}

static void server_thread_termination(void)
{
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_MISS_SIGINT;
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
	manager_common_send_message(SERVER_MANAGER, &msg);
}

static void server_release_1(void)
{
	miss_broadcast_thread_exit();
	usleep(1000);
}

static void server_release_2(void)
{
	miss_server_disconnect();
	msg_buffer_release2(&message, &mutex);
	memset(&config,0,sizeof(miss_config_t));
	memset(&client_session,0,sizeof(client_session));
	memset(&player, 0, sizeof(player));
}

static void server_release_3(void)
{
	memset(&info, 0, sizeof(server_info_t));
}

static int miss_session_check(miss_session_t *session)
{
	int num = 0, i, j, find;
	int sid[MAX_SESSION_NUMBER];
	struct list_handle *post;
	session_node_t *session_node = NULL;
    list_for_each(post, &(client_session.head)) {
        session_node = list_entry(post, session_node_t, list);
        if(session_node) {
        	if(session_node->session == session) {
        		return SESSION_EXIST;
        	}
        	sid[num] = session_node->id;
        	num++;
        }
    }
    if( num >= MAX_SESSION_NUMBER )
    	return SESSION_FULL;
    for(i=0;i<MAX_SESSION_NUMBER;i++) {
    	find = 0;
    	for(j=0;j<num;j++) {
    		if( sid[j] == i ) {
    			find = 1;
    			break;
    		}
    	}
    	if( !find )
    		return i;
    }
    return SESSION_FULL;
}

static int miss_session_status(message_t *msg)
{
    int ret = -1, sid;
    int error = msg->arg_in.dog;
    miss_session_t *session;
    struct list_handle *post;
    session_node_t *session_node = NULL;
    int *pSession_valu = NULL;
    switch(msg->arg_in.cat) {
    	case SESSION_STATUS_ADD:
    		pthread_rwlock_wrlock(&ilock);
    	    session = (miss_session_t*)msg->arg_in.handler;
    		sid = miss_session_check(session);
    	    if( sid == SESSION_FULL) {
    	    	log_qcy(DEBUG_WARNING, "use_session_num:%d max:%d!", client_session.use_session_num, MAX_SESSION_NUMBER);
    	    	pthread_rwlock_unlock(&ilock);
    	    	break;
    	    }
    	    else if( sid == SESSION_EXIST ){
    	    	log_qcy(DEBUG_WARNING, "use_session: %p exist", session);
    	    	pthread_rwlock_unlock(&ilock);
    	    	break;
    	    }
    	    session_node = malloc(sizeof(session_node_t));
    	    if(!session_node) {
    	        log_qcy(DEBUG_SERIOUS, "session add malloc error");
    	        pthread_rwlock_unlock(&ilock);
    	        break;
    	    }
    	    memset(session_node, 0, sizeof(session_node_t));
    	    session_node->session = session;
    	    session_node->id = sid;
    	    miss_list_add_tail(&(session_node->list), &(client_session.head));
    	    //***initial
    	    session_node->task.func = session_task_none;
    		//user data
    	    pSession_valu = malloc(sizeof(int));
    	    *pSession_valu = sid;
    	    *((void**)msg->arg) = pSession_valu;
    	    client_session.use_session_num ++;
    		log_qcy(DEBUG_INFO, "[miss_session_add]miss:%d session_node->session:%d",session,session_node->session);
    	    pthread_rwlock_unlock(&ilock);
    		break;
    	case SESSION_STATUS_REMOVE:
    	    if( error != MISS_ERR_CLOSE_BY_LOCAL) {
    	    	pthread_rwlock_wrlock(&ilock);
    	    	session = (miss_session_t*)msg->arg_in.handler;
    	        list_for_each(post, &(client_session.head)) {
    	            session_node = list_entry(post, session_node_t, list);
    	            if(session_node && session_node->session == session) {
    	            	miss_clean_stream(session_node);
    	                miss_list_del(&(session_node->list));
    	                free(session_node);
    	                client_session.use_session_num --;
    	                ret = 0;
    	                break;
    	            }
    	        }
    	        pthread_rwlock_unlock(&ilock);
    	    }
    	    if(msg->arg) {
    		    log_qcy(DEBUG_INFO, "miss disconnect user_data:%p\n", msg->arg);
    		    free(msg->arg);
    		}
    		break;
    	case SESSION_STATUS_ERROR:
    		log_qcy(DEBUG_INFO, "miss session %p and error =%d\n", msg->arg_in.handler, msg->arg_in.dog);
    		break;
    	default:
    		log_qcy(DEBUG_WARNING, "unprocessed session status = %d, error=%d", msg->arg_in.cat, msg->arg_in.dog);
    		break;
    }
    return ret;
}

static int miss_cmd_get_devinfo(int SID, miss_session_t *session, char *buf)
{
    char str_resp[1024] = { 0 };
	char str_did[64] = { 0 };
	char str_mac[18] = { 0 };
	char str_version[64] = { 0 };
    int wifi_rssi = 100;
    strcpy(str_mac, config.profile.mac);
    strcpy(str_did, config.profile.did);
    strcpy(str_version, APPLICATION_VERSION_STRING);
    snprintf(str_resp, sizeof(str_resp),
         "{\"did\":\"%s\",\"mac\":\"%s\",\"version\":\"%s\",\"rssi\":%d}", str_did, str_mac, str_version,
         wifi_rssi);
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p isn't find!", session);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
    int ret = miss_cmd_send(session, MISS_CMD_DEVINFO_RESP, (char *)str_resp, strlen(str_resp) + 1);
    if (0 != ret) {
        log_qcy(DEBUG_SERIOUS, "miss_cmd_send error, ret: %d", ret);
        pthread_rwlock_unlock(&ilock);
        return ret;
    }
    pthread_rwlock_unlock(&ilock);
	return 0;
}

int miss_cmd_video_start(int session_id, miss_session_t *session, char *param)
{
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( ( node->video_status != STREAM_NONE) ) {
		log_qcy(DEBUG_WARNING, "miss: video thread is already running! %p %d", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	if( node->task.func == session_task_none ) {
		node->task.func = session_task_live;
		node->task.status = TASK_INIT;
		node->task.msg_lock = 1;
	}
	if( !node->video )
		node->video_switch = 1;
	node->video = 1;
	pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_video_stop(int session_id, miss_session_t *session,char *param)
{
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( ( node->video_status != STREAM_START) ) {
		log_qcy(DEBUG_WARNING, "miss: video thread is not running! %p %d", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	if( node->video )
		node->video_switch = 1;
	node->video = 0;
	pthread_rwlock_unlock(&ilock);
	return 0;
}

int miss_cmd_audio_start(int session_id, miss_session_t *session,char *param)
{
	pthread_rwlock_wrlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( ( node->audio_status != STREAM_NONE) ) {
		log_qcy(DEBUG_WARNING, "miss: audio thread is already running! %p %d", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	if( !node->audio )
		node->audio_switch = 1;
	node->audio = 1;
    pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_audio_stop(int session_id, miss_session_t *session,char *param)
{
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( ( node->audio_status != STREAM_START) ) {
		log_qcy(DEBUG_WARNING, "miss: audio thread is not running! %p %d", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	if(node->audio)
		node->audio_switch = 1;
	node->audio = 0;
	pthread_rwlock_unlock(&ilock);
	return 0;
}

int miss_cmd_speaker_start(int session_id, miss_session_t *session, char *param)
{
    message_t	msg;
    log_qcy(DEBUG_INFO, "speaker start param string content: %s", param);
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_SPEAKER_CTL_PLAY;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_in.cat = SPEAKER_CTL_INTERCOM_START;
	msg.arg_in.wolf = session_id;
	msg.arg_in.handler = session;
	msg.arg_pass.cat = MISS_ASYN_SPEAKER_START;
	msg.arg_pass.wolf = session_id;
	msg.arg_pass.handler = session;
	manager_common_send_message(SERVER_SPEAKER, &msg);
	/****************************/
    if( node->task.func == session_task_none ) {
    	if(node->audio_status == STREAM_NONE) {
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_AUDIO_START;
			msg.sender = msg.receiver = SERVER_AUDIO;
			msg.arg_in.wolf = session_id;
			msg.arg_in.handler = session;
			msg.arg_pass.cat = MISS_ASYN_AUDIO_START;
			msg.arg_pass.wolf = session_id;
			msg.arg_pass.handler = session;
			manager_common_send_message(SERVER_AUDIO, &msg);
			/****************************/
    	}
    }
    else {
    	if( !node->audio )
    		node->audio_switch = 1;
    	node->audio = 1;
    }

	pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_speaker_stop(int session_id, miss_session_t *session, char *param)
{
    message_t	msg;
    log_qcy(DEBUG_INFO, "speaker stop param string content: %s", param);
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_SPEAKER_CTL_PLAY;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_in.cat = SPEAKER_CTL_INTERCOM_STOP;
	msg.arg_in.wolf = session_id;
	msg.arg_in.handler = session;
	msg.arg_pass.cat = MISS_ASYN_SPEAKER_STOP;
	msg.arg_pass.wolf = session_id;
	msg.arg_pass.handler = session;
	manager_common_send_message(SERVER_SPEAKER, &msg);
	/****************************/
    if( node->task.func == task_default ) {
    	if( node->audio_status == STREAM_START)  {
			/********message body********/
			msg_init(&msg);
			msg.message = MSG_AUDIO_STOP;
			msg.sender = msg.receiver = SERVER_AUDIO;
			msg.arg_in.wolf = session_id;
			msg.arg_in.handler = session;
			msg.arg_pass.cat = MISS_ASYN_AUDIO_STOP;
			msg.arg_pass.wolf = session_id;
			msg.arg_pass.handler = session;
			manager_common_send_message(SERVER_AUDIO, &msg);
			/****************************/
    	}
    }
	else {
		if( node->audio )
			node->audio_switch = 1;
		node->audio = 0;
	}
	pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_motor_ctrl(int session_id, miss_session_t *session,char *param)
{
    int ret = 0;
	message_t msg;
    static int direction = 0, op = 0;
    log_qcy(DEBUG_INFO, "motor param string content: %s", param);
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	ret = json_verify_get_int(param, "motor_operation", (int *)&direction);
	if (ret == 0) {
		log_qcy(DEBUG_INFO, "motor direction: %d", (int)direction);
	}
	ret = json_verify_get_int(param, "operation", (int *)&op);
	if (ret ==0 ) {
		log_qcy(DEBUG_INFO, "motor operation: %d", (int)op);
	}
    /********message body********/
    if( op != 0 && direction ) {
		msg_init(&msg);
		msg.sender = msg.receiver = SERVER_MISS;
		msg.message = MSG_DEVICE_CTRL_DIRECT;
		if(  direction == 1 )
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_VER_UP;
		else if( direction == 2)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_VER_DOWN;
		else if( direction == 3)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_HOR_LEFT;
		else if( direction == 6)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_HOR_RIGHT;
		else if( direction == 4)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_LEFT_UP;
		else if( direction == 7)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_RIGHT_UP;
		else if( direction == 5)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_LEFT_DOWN;
		else if( direction == 8)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_RIGHT_DOWN;
		else if( direction == 10)
			msg.arg_in.cat = DEVICE_CTRL_MOTOR_RESET;
		msg.arg_in.wolf = session_id;
		msg.arg_in.handler = session;
		msg.arg_pass.cat = MISS_ASYN_MOTOR_CTRL;
		msg.arg_pass.wolf = session_id;
		msg.arg_pass.handler = session;
		ret = server_device_message(&msg);
		direction = 0;
		op = 0;
	}
	pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_video_ctrl(int session_id, miss_session_t *session,char *param)
{
    int ret = 0, vq;
	message_t msg;
	ret = json_verify_get_int(param, "videoquality", (int *)&vq);
	if (ret < 0) {
		log_qcy(DEBUG_INFO, "IOTYPE_USER_IPCAM_SETSTREAMCTRL_REQ: %u", (int)vq);
		return -1;
	} else {
		log_qcy(DEBUG_INFO, "IOTYPE_USER_IPCAM_SETSTREAMCTRL_REQ, content: %s", param);
	}
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( node->source == SOURCE_LIVE ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_VIDEO_PROPERTY_SET;
		msg.sender = msg.receiver = SERVER_MISS;
		msg.arg_in.cat = VIDEO_PROPERTY_QUALITY;
		msg.arg_in.wolf = session_id;
		msg.arg_in.handler = session;
		msg.arg = &vq;
		msg.arg_size = sizeof(vq);
		msg.arg_pass.cat = MISS_ASYN_VIDEO_CTRL;
		msg.arg_pass.wolf = session_id;
		msg.arg_pass.handler = session;
		server_video_message(&msg);
		/****************************/
	}
	pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_audio_get_format(int session_id, miss_session_t *session, char *param)
{
    message_t	msg;
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	pthread_rwlock_unlock(&ilock);
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_AUDIO_PROPERTY_GET;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_in.cat = AUDIO_PROPERTY_FORMAT;
	msg.arg_in.wolf = session_id;
	msg.arg_in.handler = session;
	msg.arg_pass.cat = MISS_ASYN_AUDIO_FORMAT;
	msg.arg_pass.wolf = session_id;
	msg.arg_pass.handler = session;
	/****************************/
    server_audio_message(&msg);
    return 0;
}

int miss_cmd_player_ctrl(int session_id, miss_session_t *session, char *param)
{
	int ret = -1;
	int id = -1;
    unsigned long long starttime = 0;
    unsigned long long endtime = 0;
	unsigned int switchtolive = 0;
	unsigned int offset = 0;
	unsigned int speed = 1;
	unsigned int avchannelmerge = 0;
	char *msg = param;
	message_t	message;
	log_qcy(DEBUG_INFO,"Receive a msg: %s", msg);
	ret = json_verify_get_int(msg, "sessionid", (int *)&id);
	if (ret < 0) {
		log_qcy(DEBUG_INFO, "error param: id is needed");
		return -1;
	}
    ret = json_verify_get_int(msg, "starttime", &starttime);
	if (ret < 0) {
		log_qcy(DEBUG_INFO, "error param: starttime is needed");
		return -1;
	}
    ret = json_verify_get_int(msg, "endtime", &endtime);
	if (ret < 0) {
		log_qcy(DEBUG_INFO, "error param: endtime is needed");
		return -1;
	}
	log_qcy(DEBUG_INFO, "starttime: %llu endtime:%llu",starttime,endtime);
	ret = json_verify_get_int(msg, "autoswitchtolive", (int *)&switchtolive);
	if (ret < 0) {
		log_qcy(DEBUG_INFO, "error param: switchtolive is needed");
		return -1;
	}
	ret = json_verify_get_int(msg, "offset", (int *)&offset);
	if (ret < 0) {
		log_qcy(DEBUG_INFO, "error param: offset is needed");
		return -1;
	}
	ret = json_verify_get_int(msg, "speed", (int *)&speed);
	if (ret < 0) {
		speed = 1;
	} else {
		if (speed != 1 && speed != 4 && speed != 16) {
			log_qcy(DEBUG_INFO, "speed can only be 1/4/16 for now");
				return -1;
		}
	}
	ret = json_verify_get_int(msg, "avchannelmerge", (int *)&avchannelmerge);
	if (ret < 0) {
		avchannelmerge = 0;
	} else {
		avchannelmerge = 1;
	}
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	node->task.func = session_task_playback;
	node->task.msg_lock = 1;
	node->task.status = TASK_INIT;
	log_qcy(DEBUG_INFO, "Miss: switch to playback task");
	memset(&player[node->id], 0, sizeof(player_init_t));
	player[node->id].start = starttime;
	player[node->id].stop = endtime;
	player[node->id].offset = offset;
	player[node->id].speed = speed;
	player[node->id].channel_merge = avchannelmerge;
	player[node->id].session_id = node->id;
	player[node->id].session = session;
	if( (node->video_status == STREAM_START) ||
		(switchtolive == 1) )
		player[node->id].switch_to_live = 1;
	if( node->audio_status == STREAM_START) {
		player[node->id].switch_to_live_audio = 1;
		player[node->id].audio = 1;
	}
	/********message body********/
	msg_init(&message);
	message.message = MSG_PLAYER_REQUEST;
	message.sender = message.receiver = SERVER_MISS;
	message.arg_in.wolf = node->id;
	message.arg_in.handler = session;
	message.arg_pass.cat = MISS_ASYN_PLAYER_REQUEST;
	message.arg_pass.wolf = node->id;
	message.arg_pass.handler = session;
	message.arg_pass.duck = 0;
	message.arg = &player[node->id];
	message.arg_size = sizeof(player_init_t);
	manager_common_send_message(SERVER_PLAYER, &message);
	/****************************/
	pthread_rwlock_unlock(&ilock);
    return 0;
}

int miss_cmd_player_set_speed(int session_id, miss_session_t *session, char *param)
{
    int ret;
    message_t msg;
    int	speed;
	ret = json_verify_get_int(param, "speed", (int *)&speed);
	if (ret < 0) {
		speed = 1;
	} else {
		if (speed != 1 && speed != 4 && speed != 16) {
			log_qcy(DEBUG_WARNING, "speed can only be 1/4/16 for now");
				return -1;
		}
	}
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_PLAYER_PROPERTY_SET;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_in.cat = PLAYER_PROPERTY_SPEED;
	msg.arg_in.wolf = session_id;
	msg.arg_in.handler = session;
	msg.arg = &speed;
	msg.arg_size = sizeof(int);
	msg.arg_pass.cat = MISS_ASYN_PLAYER_SET;
	msg.arg_pass.wolf = session_id;
	msg.arg_pass.handler = session;
	/****************************/
    manager_common_send_message(SERVER_PLAYER, &msg);
	pthread_rwlock_unlock(&ilock);
    return 0;
}

static int miss_cmd_proc(message_t *msg)
{
	int ret = 0;
	int sessionnum = msg->arg_in.wolf;
	void *params = (void*)msg->arg;
	miss_session_t *session = (miss_session_t*)msg->arg_in.handler;
	log_qcy(DEBUG_INFO, "miss_on_cmd sesson:%p, cmd:%d, len:%d\n", session, msg->arg_in.cat, msg->arg_size);
	switch (msg->arg_in.cat) {
		case MISS_CMD_VIDEO_START:
			miss_cmd_video_start(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_VIDEO_STOP:
			miss_cmd_video_stop(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_AUDIO_START:
			miss_cmd_audio_start(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_AUDIO_STOP:
			miss_cmd_audio_stop(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_SPEAKER_START_REQ:
			miss_cmd_speaker_start(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_SPEAKER_STOP:
			miss_cmd_speaker_stop(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_STREAM_CTRL_REQ:
			miss_cmd_video_ctrl(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_GET_AUDIO_FORMAT_REQ:
			miss_cmd_audio_get_format(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_DEVINFO_REQ:
			miss_cmd_get_devinfo(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_MOTOR_REQ:
			miss_cmd_motor_ctrl(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_PLAYBACK_REQ:
			miss_cmd_player_ctrl(sessionnum, session, (char*)params);
			break;
		case MISS_CMD_PLAYBACK_SET_SPEED:
			miss_cmd_player_set_speed(sessionnum, session, (char*)params);
			break;
		default:
			log_qcy(DEBUG_WARNING, "unknown cmd:0x%x\n", msg->arg_in.cat);
			break;
	}
	return ret;
}

static int miss_rdt_proc(message_t *msg)
{
	char *buf = (char*)msg->arg;
	int session_id = msg->arg_in.wolf;
	miss_session_t *session = (miss_session_t*)msg->arg_in.handler;
	int enableRdt = msg->arg_in.cat;
	int ret = 0;
    unsigned int cmd_type;
    message_t message;
    char starttime[MAX_SYSTEM_STRING_SIZE];
    char endtime[MAX_SYSTEM_STRING_SIZE];
    memset(starttime, 0, sizeof(starttime));
    memset(endtime, 0, sizeof(endtime));
    if (NULL == buf)
        return -1;
    log_qcy(DEBUG_INFO, "recv msg: %s\n",buf);
	ret = json_verify_get_int(buf, "cmdtype", (int *)&cmd_type);
	if (ret < 0) {
		log_qcy(DEBUG_WARNING, "error param: cmdtype\n");
		return -1;
	}
	pthread_rwlock_rdlock(&ilock);
	session_node_t *node = miss_session_check_node(session);
	if( (node == NULL) ) {
		log_qcy(DEBUG_WARNING, "miss: session %p %d isn't find!", session, session_id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
    switch (cmd_type) {
        case GET_RECORD_FILE:
        case GET_RECORD_TIMESTAMP:
            ret = json_verify_get_string(buf,"starttime",starttime,sizeof(starttime)-1);
            if (ret < 0) {
            	log_qcy(DEBUG_WARNING, "cmdtype 5 error param: starttime\n");
            	pthread_rwlock_unlock(&ilock);
                return -1;
            }
            ret = json_verify_get_string(buf,"endtime",endtime,sizeof(endtime)-1);
            if (ret < 0) {
            	log_qcy(DEBUG_WARNING,"cmdtype 5 error param: endtime\n");
            	pthread_rwlock_unlock(&ilock);
                return -1;
            }
        	/******************************/
        	msg_init(&message);
        	message.message = MSG_PLAYER_GET_FILE_LIST;
        	message.sender = message.receiver = SERVER_MISS;
        	message.arg_in.cat = (unsigned int)time_date_to_stamp(starttime);// - _config_.timezone * 3600);
        	message.arg_in.dog = (unsigned int)time_date_to_stamp(endtime);// - _config_.timezone * 3600);
        	message.arg_in.duck = cmd_type;
        	message.arg_in.chick = 1;
        	message.arg_in.wolf = session_id;
        	message.arg_in.handler = session;
        	message.arg_pass.dog = cmd_type;
        	message.arg_pass.wolf = session_id;
        	message.arg_pass.handler = session;
        	manager_common_send_message(SERVER_PLAYER,    &message);
        	/******************************/
            break;
        case GET_RECORD_DATE:
        	/******************************/
        	msg_init(&message);
        	message.message = MSG_PLAYER_GET_FILE_DATE;
        	message.sender = message.receiver = SERVER_MISS;
        	message.arg_in.duck = cmd_type;
        	message.arg_in.wolf = session_id;
        	message.arg_in.handler = session;
        	message.arg_pass.dog = cmd_type;
        	message.arg_pass.wolf = session_id;
        	message.arg_pass.handler = session;
        	manager_common_send_message(SERVER_PLAYER,    &message);
        	/******************************/
        	break;
        case GET_RECORD_PICTURE:
            ret = json_verify_get_string(buf,"starttime",starttime,sizeof(starttime)-1);
            if (ret < 0) {
            	log_qcy(DEBUG_WARNING, "cmdtype 5 error param: starttime\n");
            	pthread_rwlock_unlock(&ilock);
                return -1;
            }
            ret = json_verify_get_string(buf,"endtime",endtime,sizeof(endtime)-1);
            if (ret < 0) {
            	log_qcy(DEBUG_WARNING,"cmdtype 5 error param: endtime\n");
            	pthread_rwlock_unlock(&ilock);
                return -1;
            }
        	/******************************/
        	msg_init(&message);
        	message.message = MSG_PLAYER_GET_PICTURE_LIST;
        	message.sender = message.receiver = SERVER_MISS;
        	message.arg_in.cat = (unsigned int)time_date_to_stamp(starttime);// - _config_.timezone * 3600);
        	message.arg_in.dog = (unsigned int)time_date_to_stamp(endtime);// - _config_.timezone * 3600);
        	message.arg_in.duck = cmd_type;
        	message.arg_in.wolf = session_id;
        	message.arg_in.handler = session;
        	message.arg_pass.dog = cmd_type;
        	message.arg_pass.wolf = session_id;
        	message.arg_pass.handler = session;
        	manager_common_send_message(SERVER_PLAYER,    &message);
        	/******************************/
            break;
        case GET_RECORD_MSG:
            break;
        default:
            log_qcy(DEBUG_SERIOUS, "Undefind cmd_type=%u!!!!!\n", cmd_type);
            break;
    }
	pthread_rwlock_unlock(&ilock);
    return 0;
}
/*
 * session task
 */
/*
 * task none
 */
static void session_task_none(session_node_t *node)
{
	node->task.old_status = node->task.status;
}

/*
 * task video start
 */
static void session_task_live(session_node_t *node)
{
	int ret = 0;
	int session_id;
    message_t msg;
	miss_session_t *session;
	session =  node->session;
	session_id = node->id;
	/*****************************/
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_in.wolf = session_id;
	msg.arg_in.handler = session;
	msg.arg_pass.wolf = session_id;
	msg.arg_pass.handler = session;
	/*****************************/
	node->task.old_status = node->task.status;
	switch( node->task.status ){
		case TASK_INIT: {
			log_qcy(DEBUG_INFO,"MISS: switch to live task!");
			if( node->source == SOURCE_NONE) {
				node->task.status = TASK_SETUP;
			}
			else
				node->task.status = TASK_WAIT;
			break;
		}
		case TASK_WAIT: {
			if( (node->audio_status != STREAM_NONE) || (node->video_status!=STREAM_NONE)) {
				if( node->audio_status != STREAM_NONE ) {
					if( node->source == SOURCE_LIVE ){
						msg.message = MSG_AUDIO_STOP;
						msg.arg_pass.cat = MISS_ASYN_AUDIO_STOP;
						manager_common_send_message(SERVER_AUDIO, &msg);
					}
				}
				if( node->video_status != STREAM_NONE ){
					if( node->source == SOURCE_LIVE ) {
						msg.message = MSG_VIDEO_STOP;
						msg.arg_pass.cat = MISS_ASYN_VIDEO_STOP;
						manager_common_send_message(SERVER_VIDEO, &msg);
					}
					else if( node->source == SOURCE_PLAYER) {
						msg.message = MSG_PLAYER_STOP;
						msg.arg_pass.cat = MISS_ASYN_PLAYER_STOP;
						manager_common_send_message(SERVER_PLAYER, &msg);
					}
				}
			}
			else {
				node->task.status = TASK_SETUP;
			}
			break;
		}
		case TASK_SETUP: {
		    /********message body********/
			msg.message = MSG_VIDEO_START;
	    	msg.arg_pass.duck = 0;
	    	msg.arg_pass.cat = MISS_ASYN_VIDEO_START;
	        manager_common_send_message(SERVER_VIDEO, &msg);
	        if( node->audio) {
				/********message body********/
				msg.message = MSG_AUDIO_START;
				msg.arg_pass.cat = MISS_ASYN_AUDIO_START;
				manager_common_send_message(SERVER_AUDIO, &msg);
	        }
	        node->task.status = TASK_IDLE;
	        node->task.timeout = 0;
			break;
		}
		case TASK_IDLE:
			if( (node->video_status == STREAM_START) ) {
				if( node->audio ) {
					if( (node->audio_status == STREAM_START) ) {
						node->task.status = TASK_RUN;
						node->source = SOURCE_LIVE;
					}
					else {
						node->task.timeout++;
						if( node->task.timeout > MISS_TASK_TIMEOUT) {
							node->task.timeout = 0;
					        log_qcy(DEBUG_INFO, "timeout inside task_live idle.");
							goto exit;
						}
					}
				}
				else {
					node->task.status = TASK_RUN;
					node->source = SOURCE_LIVE;
				}
			}
			else {
				node->task.timeout++;
				if( node->task.timeout > MISS_TASK_TIMEOUT) {
					node->task.timeout = 0;
			        log_qcy(DEBUG_INFO, "timeout inside task_live idle.");
					goto exit;
				}
			}
			break;
		case TASK_RUN: {
			if( node->task.msg_lock )
				node->task.msg_lock = 0;
			if( node->audio_switch ){
				if( node->audio ) {
					msg.message = MSG_AUDIO_START;
					msg.arg_pass.cat = MISS_ASYN_AUDIO_START;
					manager_common_send_message(SERVER_AUDIO, &msg);
				}
				else{
					msg.message = MSG_AUDIO_STOP;
					msg.arg_pass.cat = MISS_ASYN_AUDIO_STOP;
					manager_common_send_message(SERVER_AUDIO, &msg);
				}
				node->audio_switch = 0;
			}
			if( node->video_switch ){
				if( !node->video ) {
					msg.message = MSG_VIDEO_STOP;
					msg.arg_pass.cat = MISS_ASYN_VIDEO_STOP;
					manager_common_send_message(SERVER_VIDEO, &msg);
					if( node->audio ) {
						msg.message = MSG_AUDIO_STOP;
						msg.arg_pass.cat = MISS_ASYN_AUDIO_STOP;
						manager_common_send_message(SERVER_AUDIO, &msg);
					}
					node->video_switch = 0;
					goto exit;
				}
			}
			break;
		}
		case TASK_FINISH:
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_live = %d", node->task.status);
			break;
		}
	return;
exit:
	node->task.func = session_task_none;
	node->task.msg_lock = 0;
	log_qcy(DEBUG_INFO,"MISS: switch to default task!");
	return;
}



/*
 * task playback
 */
static void session_task_playback(session_node_t *node)
{
	int ret = 0;
	int session_id;
    message_t msg;
	miss_session_t *session;
	session =  node->session;
	session_id = node->id;
    /********message body********/
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_in.wolf = session_id;
	msg.arg_in.handler = session;
	msg.arg_pass.wolf = session_id;
	msg.arg_pass.handler = session;
	/*****************************/
	node->task.old_status = node->task.status;
	switch( node->task.status ){
		case TASK_INIT: {
			break;
		}
		case TASK_WAIT: {
			if( (node->audio_status != STREAM_NONE) || (node->video_status!=STREAM_NONE)	) {
				if( node->audio_status != STREAM_NONE) {
					if( node->source == SOURCE_LIVE ){
						msg.message = MSG_AUDIO_STOP;
						msg.arg_pass.cat = MISS_ASYN_AUDIO_STOP;
						manager_common_send_message(SERVER_AUDIO, &msg);
					}
				}
				if( (node->video_status!=STREAM_NONE) ) {
					if( node->source == SOURCE_LIVE ){
						msg.message = MSG_VIDEO_STOP;
						msg.arg_pass.cat = MISS_ASYN_VIDEO_STOP;
						manager_common_send_message(SERVER_VIDEO, &msg);
					}
/*					else if( node->source == SOURCE_PLAYER) {
						msg.message = MSG_PLAYER_STOP;
						msg.arg_pass.cat = MISS_ASYN_PLAYER_STOP;
						manager_common_send_message(SERVER_PLAYER, &msg);
					}
*/
				}
				node->task.timeout++;
				if( node->task.timeout > MISS_TASK_TIMEOUT) {
					node->task.timeout = 0;
					log_qcy(DEBUG_INFO, "timeout inside task_playback idle.");
					goto exit;
				}
			}
			else {
				node->task.status = TASK_RUN;
			}
			break;
		}
		case TASK_RUN: {
			if(node->task.msg_lock)
				node->task.msg_lock = 0;
			if( node->audio_switch ){
				if( node->audio ) {
					msg.message = MSG_PLAYER_AUDIO_START;
					msg.arg_pass.cat = MISS_ASYN_PLAYER_AUDIO_START;
					manager_common_send_message(SERVER_PLAYER, &msg);
				}
				else{
					msg.message = MSG_PLAYER_AUDIO_STOP;
					msg.arg_pass.cat = MISS_ASYN_PLAYER_AUDIO_STOP;
					manager_common_send_message(SERVER_PLAYER, &msg);
				}
				node->audio_switch = 0;
			}
			if( node->video_switch ){
				if( node->video ) {
					msg.message = MSG_PLAYER_START;
					msg.arg_pass.cat = MISS_ASYN_PLAYER_START;
					manager_common_send_message(SERVER_PLAYER, &msg);
					node->video_switch = 0;
				}
				else{
					msg.message = MSG_PLAYER_STOP;
					msg.arg_pass.cat = MISS_ASYN_PLAYER_STOP;
					manager_common_send_message(SERVER_PLAYER, &msg);
					node->video_switch = 0;
					goto exit;
				}
			}
			break;
		}
		case TASK_FINISH:
			goto exit2;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_player = %d", node->task.status);
			break;
		}
	return;
exit:
	node->task.func = session_task_none;
	node->task.msg_lock = 0;
	log_qcy(DEBUG_INFO,"MISS: switch to default task!");
	memset(&player[node->id], 0, sizeof(player_init_t));
	return;
exit2:
	if( player[node->id].switch_to_live ) {
		node->video = node->video_switch = 1;
		if( player[node->id].switch_to_live_audio) {
			node->audio = node->audio_switch = 1;
		}
		node->task.status = TASK_INIT;
		node->task.func = session_task_live;
		node->task.msg_lock = 1;
	}
	else {
		node->task.func = task_default;
		node->task.msg_lock = 0;
		log_qcy(DEBUG_INFO,"MISS: switch to default task!");
	}
	memset(&player[node->id], 0, sizeof(player_init_t));
	node->task.msg_lock = 0;
	return;
}

static void session_task_proc(void)
{
    struct list_handle *post;
    session_node_t *node = NULL;
    list_for_each(post, &(client_session.head)) {
    	node = list_entry(post, session_node_t, list);
        if(node && node->session) {
        	pthread_rwlock_wrlock(&ilock);
        	( *( void(*)(session_node_t*) ) node->task.func ) (node);
        	pthread_rwlock_unlock(&ilock);
        }
    }
}
/*
 *
 */
static int miss_check_condition(void)
{
	int ret = 1;
    struct list_handle *post;
    session_node_t *node = NULL;
	pthread_rwlock_wrlock(&ilock);
    if( info.old_status != info.status ) {
    	ret = 0;
    }
    else {
		list_for_each(post, &(client_session.head)) {
			node = list_entry(post, session_node_t, list);
			if(node && node->session) {
				if( node->task.old_status != node->task.status )
					ret = 0;
			}
		}
    }
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static int miss_message_block_helper(session_node_t *node)
{
	int ret = 0;
	int id = -1, id1, index = 0;
	miss_session_t *session;
	message_t msg;
	void *arg = NULL;
	if( !node->task.msg_lock ) return 0;
	//search for unblocked message and swap if necessory
	index = 0;
	ret = msg_buffer_probe_item(&message, index, &msg);
	msg_init(&msg);
	if(ret || (msg.arg_in.handler != node->session) ) return 0;
	if( msg_is_system(msg.message) || msg_is_response(msg.message) ) return 0;
	do {
		index++;
		arg = NULL;
		msg_init(&msg);
		ret = msg_buffer_probe_item(&message, index, &msg);
		if(ret) return 1;
		if( (msg_is_system(msg.message) || msg_is_response(msg.message)) ||
				( msg.arg_in.handler != node->session) ) {	//find one behind system or response message
			msg_buffer_swap_item(&message, 0, index);
			log_qcy(DEBUG_WARNING, "MISS: swapped message happend, message %x was swapped with message %x", id, id1);
			return 0;
		}
	}
	while(!ret);
	return ret;
}

static int miss_message_block(void)
{
	int ret = 0;
    struct list_handle *post;
    session_node_t *node = NULL;
	pthread_rwlock_wrlock(&ilock);
    list_for_each(post, &(client_session.head)) {
    	node = list_entry(post, session_node_t, list);
        if(node && node->session) {
        	ret |= miss_message_block_helper(node);
        	if(ret)
        		break;
        }
    }
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static int miss_message_filter(message_t  *msg)
{
	int ret = 0;
	if( info.task.func == task_exit) { //only system message
		if( !msg_is_system(msg->message) && !msg_is_response(msg->message) )
			return 1;
	}
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0;
	message_t msg;
	void *msg_id = NULL;
	char err[256];
	int st;
	if( miss_message_block() ) return 0;
//condition
	pthread_mutex_lock(&mutex);
	if( message.head == message.tail ) {
		if( miss_check_condition() ) {
			pthread_cond_wait(&cond,&mutex);
		}
	}
	msg_init(&msg);
	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&mutex);
	if( ret == 1) {
		return 0;
	}
	if( miss_message_filter(&msg) ) {
		msg_free(&msg);
		log_qcy(DEBUG_VERBOSE, "MISS message--- sender=%d, message=%x, ret=%d, head=%d, tail=%d was screened, the current task is %p", msg.sender, msg.message,
				ret, message.head, message.tail, info.task.func);
		return -1;
	}
	log_qcy(DEBUG_VERBOSE, "-----pop out from the MISS message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg.sender, msg.message,
			ret, message.head, message.tail);
	msg_init(&info.task.msg);
	msg_deep_copy(&info.task.msg, &msg);
	switch(msg.message){
		case MSG_MANAGER_EXIT:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_MISSRPC_ERROR:
			break;
		case MSG_VIDEO_START_ACK:
		case MSG_PLAYER_START_ACK:
		case MSG_VIDEO_STOP_ACK:
		case MSG_PLAYER_STOP_ACK:
		case MSG_AUDIO_START_ACK:
		case MSG_PLAYER_AUDIO_START_ACK:
		case MSG_AUDIO_STOP_ACK:
		case MSG_PLAYER_AUDIO_STOP_ACK:
		case MSG_VIDEO_PROPERTY_GET_ACK:
		case MSG_VIDEO_PROPERTY_SET_ACK:
		case MSG_VIDEO_PROPERTY_SET_EXT_ACK:
		case MSG_VIDEO_PROPERTY_SET_DIRECT_ACK:
		case MSG_PLAYER_PROPERTY_SET_ACK:
		case MSG_SPEAKER_CTL_PLAY_ACK:
		case MSG_PLAYER_REQUEST_ACK:
		case MSG_PLAYER_RELAY_ACK:
		case MSG_PLAYER_FINISH:
			miss_message_callback(&msg);
			break;
		case MSG_MISS_RPC_SEND:
			miss_rpc_send_proc(&msg);
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == MIIO_PROPERTY_CLIENT_STATUS ) {
				if( msg.arg_in.dog == STATE_CLOUD_CONNECTED )
					misc_set_bit( &info.init_status, MISS_INIT_CONDITION_MIIO_CONNECTED, 1);
			}
			else if( msg.arg_in.cat == MIIO_PROPERTY_DID_STATUS ) {
				if( msg.arg_in.dog == 1 )
					strcpy( config.profile.did, (char*)(msg.arg));
					if(strlen(config.profile.did) > 1) {
						misc_set_bit( &info.init_status, MISS_INIT_CONDITION_MIIO_DID, 1);
					}
			}
			break;
		case MSG_PLAYER_GET_FILE_LIST_ACK:
		case MSG_PLAYER_GET_FILE_DATE_ACK:
		case MSG_PLAYER_GET_PICTURE_LIST_ACK:
		case MSG_PLAYER_GET_INFOMATION_ACK:
			miss_get_player_infomation_ack(&msg);
			break;
		case MSG_MANAGER_EXIT_ACK:
			misc_set_bit(&info.error, msg.sender, 0);
			break;
		case MSG_MISS_CMD:
			miss_cmd_proc(&msg);
			break;
		case MSG_MISS_SERVER_STATUS:
			miss_server_set_status(&msg);
			break;
		case MSG_MISS_RDT:
			miss_rdt_proc(&msg);
			break;
		case MSG_MISS_SESSION_STATUS:
			miss_session_status(&msg);
			break;
		case MSG_MANAGER_DUMMY:
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

/*
 *
 */
static int server_none(void)
{
	int ret = 0;
	message_t msg;
	if( !misc_get_bit( info.init_status, MISS_INIT_CONDITION_CONFIG ) ) {
		ret = config_miss_read(&config);
		if( !ret && misc_full_bit(config.status, CONFIG_MISS_MODULE_NUM) ) {
			misc_set_bit(&info.init_status, MISS_INIT_CONDITION_CONFIG, 1);
		}
		else {
			info.status = STATUS_ERROR;
			return -1;
		}
	}
	if( config.profile.board_type && !misc_get_bit( info.init_status, MISS_INIT_CONDITION_MIIO_DID ) ) {
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MIIO_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_MISS;
		msg.arg_in.cat = MIIO_PROPERTY_DID_STATUS;
		manager_common_send_message(SERVER_MIIO,   &msg);
		/****************************/
	}
	int actual_init_num = MISS_INIT_CONDITION_NUM;
	if( !config.profile.board_type )
		actual_init_num--;
	if( misc_full_bit( info.init_status, actual_init_num ) )
		info.status = STATUS_WAIT;
	return ret;
}

static int server_setup(void)
{
	int ret = 0;
	if(miss_server_connect() < 0) {
		log_qcy(DEBUG_SERIOUS, "create miss sdk server fail");
		info.status = STATUS_ERROR;
		return -1;
	}
	log_qcy(DEBUG_INFO, "create miss sdk server finished");
	info.status = STATUS_IDLE;
	return ret;
}

static int server_restart(void)
{
	int ret = 0;
	miss_server_disconnect();
	sleep(5);
	info.status = STATUS_WAIT;
	return ret;
}


/****************************/
/*
 * exit: *->exit
 */
static void task_exit(void)
{
	info.old_status = info.status;
	switch( info.status ){
		case EXIT_INIT:
			log_qcy(DEBUG_INFO,"MISS: switch to exit task!");
			info.error = MISS_EXIT_CONDITION;
			if( info.task.msg.sender == SERVER_MANAGER) {
				info.error &= (info.task.msg.arg_in.cat);
			}
			info.status = EXIT_SERVER;
			break;
		case EXIT_SERVER:
			if( !info.error )
				info.status = EXIT_STAGE1;
			break;
		case EXIT_STAGE1:
			server_release_1();
			info.status = EXIT_THREAD;
			break;
		case EXIT_THREAD:
			info.thread_exit = info.thread_start;
			miss_broadcast_thread_exit();
			if( !info.thread_start )
				info.status = EXIT_STAGE2;
			break;
			break;
		case EXIT_STAGE2:
			server_release_2();
			info.status = EXIT_FINISH;
			break;
		case EXIT_FINISH:
			info.exit = 1;
		    /********message body********/
			message_t msg;
			msg_init(&msg);
			msg.message = MSG_MANAGER_EXIT_ACK;
			msg.sender = SERVER_MISS;
			manager_common_send_message(SERVER_MANAGER, &msg);
			/***************************/
			info.status = STATUS_NONE;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_exit = %d", info.status);
			break;
		}
	return;
}

static void task_default(void)
{
	info.old_status = info.status;
	switch( info.status ){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			info.status = STATUS_RUN;
			break;
		case STATUS_RUN:
			break;
		case STATUS_RESTART:
			server_restart();
			break;
		case STATUS_ERROR:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
	}
	return;
}

/*
 * server entry point
 */
static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_miss");
	pthread_detach(pthread_self());
	msg_buffer_init2(&message, MSG_BUFFER_OVERFLOW_NO, &mutex);
	//default task
	info.init = 1;
	info.task.func = task_default;
	while( !info.exit ) {
		info.task.func();
		session_task_proc();
		server_message_proc();
	}
	server_release_3();
	log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_miss-----------");
	pthread_exit(0);
}

/*
 * external interface
 */
int server_miss_start(void)
{
	int ret=-1;
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "miss server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_qcy(DEBUG_INFO, "miss server create successful!");
		return 0;
	}
}

int server_miss_message(message_t *msg)
{
	int ret=0;
	pthread_mutex_lock(&mutex);
	if( !message.init ) {
		log_qcy(DEBUG_INFO, "miss server is not ready for message processing!");
		pthread_mutex_unlock(&mutex);
		return MISS_LOCAL_ERR_MISS_GONE;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the miss message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in miss error =%d", ret);
	else {
		pthread_cond_signal(&cond);
	}
	pthread_mutex_unlock(&mutex);
	return ret;
}

int server_miss_video_message(message_t *msg)
{
	int ret = 0, id = -1;
	id = msg->arg_in.wolf;
	pthread_rwlock_rdlock(&ilock);
	if( !info.init ) {
		log_qcy(DEBUG_INFO, "miss video: miss server is not ready for message process!");
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_MISS_GONE;
	}
	session_node_t *pnod = miss_session_check_node_by_id( id );
	if( (pnod == NULL) || (msg->arg_in.handler!=pnod->session) ) {
		log_qcy(DEBUG_WARNING, "miss video: session id %d isn't find!", id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( ( pnod->video_status == STREAM_NONE) ) {
		log_qcy(DEBUG_WARNING, "miss video: video thread is not running! = %d", id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	pthread_rwlock_unlock(&ilock);
	pthread_mutex_lock(&vmutex[id]);
	if( (!video_buff[id].init) ) {
		log_qcy(DEBUG_WARNING, "miss video [ch=%d] is not ready for message processing!", id);
		pthread_mutex_unlock(&vmutex[id]);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	ret = msg_buffer_push(&video_buff[id], msg);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in miss video error =%d", ret);
	else {
		pthread_cond_signal(&vcond[id]);
	}
	pthread_mutex_unlock(&vmutex[id]);
	return ret;
}

int server_miss_audio_message(message_t *msg)
{
	int ret = 0, id = -1;
	id = msg->arg_in.wolf;
	pthread_rwlock_rdlock(&ilock);
	if( !info.init ) {
		log_qcy(DEBUG_INFO, "miss audio: miss server is not ready for message process!");
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_MISS_GONE;
	}
	session_node_t *pnod = miss_session_check_node_by_id( id );
	if( (pnod == NULL) || (msg->arg_in.handler!=pnod->session) ) {
		log_qcy(DEBUG_WARNING, "miss audio: session id %d isn't find!", id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_SESSION_GONE;
	}
	if( ( pnod->audio_status == STREAM_NONE) ) {
		log_qcy(DEBUG_WARNING, "miss audio: audio thread is not running! = %d", id);
		pthread_rwlock_unlock(&ilock);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	pthread_rwlock_unlock(&ilock);
	pthread_mutex_lock(&amutex[id]);
	if( (!audio_buff[id].init) ) {
		log_qcy(DEBUG_WARNING, "miss audio [ch=%d] is not ready for message processing!", id);
		pthread_mutex_unlock(&amutex[id]);
		return MISS_LOCAL_ERR_AV_NOT_RUN;
	}
	ret = msg_buffer_push(&audio_buff[id], msg);
	if( ret!=0 )
		log_qcy(DEBUG_INFO, "message push in miss audio error =%d", ret);
	else {
		pthread_cond_signal(&acond[id]);
	}
	pthread_mutex_unlock(&amutex[id]);
	return ret;
}
