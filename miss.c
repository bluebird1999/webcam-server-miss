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
#include <dmalloc.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/realtek/realtek_interface.h"
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
static message_buffer_t		video_buff;
static message_buffer_t		audio_buff;
static miss_config_t		config;
static client_session_t		client_session;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void task_default(void);
static void task_error(void);
static int server_release(void);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static void server_thread_termination(void);
//specific
static int miss_server_connect(void);
static int miss_server_disconnect(void);
static int session_send_video_stream(int chn_id, message_t *msg);
static int session_send_audio_stream(int chn_id, message_t *msg);
static stream_status_t session_get_node_status(session_node_t *node, int mode);
static session_node_t *miss_session_check_node(miss_session_t *session);
static miss_session_t *miss_session_get_node_id(int sid);
static void *session_stream_send_audio_func(void *arg);
static void *session_stream_send_video_func(void *arg);
static int miss_message_callback(message_arg_t arg_pass, int result, int size, void* para);

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
    //find session at list
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

static miss_session_t *miss_session_get_node_id(int sid)
{
    //find session at list
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

static int miss_message_callback(message_arg_t arg_pass, int result, int size, void* para)
{
	int ret = 0;
	int code;
	char audio_format[16];
	audio_iot_config_t config;
	pthread_t	stream_pid;
	miss_session_t *sid = miss_session_get_node_id( arg_pass.dog );
	session_node_t *pnod = miss_session_check_node(sid);
	if( sid == NULL || pnod == NULL ) {
		log_err("session id %d isn't find!");
		return -1;
	}
	switch( arg_pass.cat) {
		case MISS_ASYN_VIDEO_START:
			log_info("\n========start new stream thread=========\n");
			pnod->video_status = STREAM_START;
			pnod->video_channel = arg_pass.duck;
			pthread_create(&stream_pid, NULL, session_stream_send_video_func, (void*)pnod);
			pnod->video_tid = stream_pid;
			break;
		case MISS_ASYN_VIDEO_STOP:
			pnod->video_tid = -1;
			pnod->video_status = STREAM_NONE;
			break;
		case MISS_ASYN_AUDIO_START:
			log_info("\n========start new stream thread=========\n");
			pnod->audio_status = STREAM_START;
			pnod->audio_channel = arg_pass.duck;
			pthread_create(&stream_pid, NULL, session_stream_send_audio_func, (void*)pnod);
			pnod->audio_tid = stream_pid;
			break;
		case MISS_ASYN_AUDIO_STOP:
			pnod->audio_tid = -1;
			pnod->audio_status = STREAM_NONE;
			break;
		case MISS_ASYN_VIDEO_CTRL:
			if( result == 0) code = MISS_NO_ERROR;
			else code = MISS_ERR_CLIENT_NO_SUPPORT;
			ret = miss_cmd_send(sid,MISS_CMD_STREAM_CTRL_RESP, (void*)&code, sizeof(int));
			break;
		case MISS_ASYN_SPEAKER_START:
		case MISS_ASYN_SPEAKER_STOP:
			if( result == 0) code = MISS_NO_ERROR;
			else code = MISS_ERR_CLIENT_NO_SUPPORT;
			ret = miss_cmd_send(sid, MISS_CMD_SPEAKER_START_RESP, (void*)&code, sizeof(int));
			break;
		case MISS_ASYN_AUDIO_FORMAT:
			config = *((audio_iot_config_t*)para);
			if( config.format == RTS_A_FMT_ALAW ) strcpy(audio_format, "g711a");
			else if( config.format == RTS_A_FMT_ULAW ) strcpy(audio_format, "g711u");
			else if( config.format == RTS_A_FMT_AUDIO ) strcpy(audio_format, "pcm");
			else if( config.format == RTS_A_FMT_MP3 ) strcpy(audio_format, "mp3");
			else if( config.format == RTS_A_FMT_AAC ) strcpy(audio_format, "aac");
			else strcpy(audio_format, "unknown");
			if( result == 0) code = MISS_NO_ERROR;
			else code = MISS_ERR_CLIENT_NO_SUPPORT;
			ret = miss_cmd_send(sid, MISS_CMD_GET_AUDIO_FORMAT_RESP,(void*)audio_format, strlen(audio_format)+1);
			break;
	}
	return ret;
}

static stream_status_t session_get_node_status(session_node_t *node, int mode)
{
	int ret;
	stream_status_t status;

	if( node==NULL )
		return STREAM_NONE;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return STREAM_NONE;
	}
	if( mode==0 )
		status = node->video_status;
	else if( mode==1 )
		status = node->audio_status;

    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return status;
}

static void *session_stream_send_video_func(void *arg)
{
	session_node_t *node=(session_node_t*)arg;
    int ret, ret1, channel;
    message_t	msg;

    misc_set_bit(&info.thread_start, THREAD_VIDEO, 1);
    misc_set_thread_name("miss_server_video_stream");
    channel = node->video_channel;
    pthread_detach(pthread_self());
    msg_buffer_init(&video_buff, MSG_BUFFER_OVERFLOW_YES);
    while( !server_get_status(STATUS_TYPE_EXIT)
    		&& session_get_node_status(node,0) == STREAM_START ) {
        //read video frame
    	ret = pthread_rwlock_wrlock(&video_buff.lock);
    	if(ret)	{
    		log_err("add message lock fail, ret = %d\n", ret);
    		continue;
    	}
    	msg_init(&msg);
    	ret = msg_buffer_pop(&video_buff, &msg);
    	ret1 = pthread_rwlock_unlock(&video_buff.lock);
    	if (ret1) {
    		log_err("add message unlock fail, ret = %d\n", ret1);
    		continue;
    	}
    	if( ret!=0 )
    		continue;
        if(ret == 0) {
        	session_send_video_stream(channel,&msg);
        }
        else {
            usleep(3000);
            continue;
        }
        msg_free(&msg);
    }
    log_info("-----------thread exit: server_miss_vstream----------");
    msg_buffer_release(&video_buff);
    misc_set_bit(&info.thread_start, THREAD_VIDEO, 0);
    pthread_exit(0);
}

static void *session_stream_send_audio_func(void *arg)
{
	session_node_t *node=(session_node_t*)arg;
    int ret, ret1, channel;
    message_t	msg;
    misc_set_bit(&info.thread_start, THREAD_AUDIO, 1);
    misc_set_thread_name("miss_server_audio_stream");
    channel = node->audio_channel;
    pthread_detach(pthread_self());
    msg_buffer_init(&audio_buff, MSG_BUFFER_OVERFLOW_YES);

    while( !server_get_status(STATUS_TYPE_EXIT)
    		&& session_get_node_status(node,1) == STREAM_START ) {
        //read
    	ret = pthread_rwlock_wrlock(&audio_buff.lock);
    	if(ret)	{
    		log_err("add message lock fail, ret = %d\n", ret);
    		continue;
    	}
    	msg_init(&msg);
    	ret = msg_buffer_pop(&audio_buff, &msg);
    	ret1 = pthread_rwlock_unlock(&audio_buff.lock);
    	if (ret1) {
    		log_err("add message unlock fail, ret = %d\n", ret1);
    		continue;
    	}
    	if( ret!=0 )
    		continue;
        if(ret == 0) {
        	session_send_audio_stream(channel,&msg);
        }
        else {
            usleep(3000);
            continue;
        }
        msg_free(&msg);
    }
    log_info("-----------thread exit: server_miss_astream----------");
    msg_buffer_release(&audio_buff);
    misc_set_bit(&info.thread_start, THREAD_AUDIO, 0);
    pthread_exit(0);
}

static int session_send_video_stream(int chn_id, message_t *msg)
{
	client_session_t* pclient_session = &client_session;
    miss_frame_header_t frame_info = {0};
    int ret;
    int flag;
    av_data_info_t	*avinfo;
    unsigned char	*p;

    p = (unsigned char*)msg->extra;
    avinfo = (av_data_info_t*)(msg->arg);
    frame_info.timestamp = avinfo->timestamp;
    frame_info.timestamp_s = avinfo->timestamp/1000;
    frame_info.sequence = avinfo->frame_index;
    frame_info.length = msg->extra_size;
    frame_info.flags |= FLAG_STREAM_TYPE_LIVE << 11;
    frame_info.flags |= FLAG_WATERMARK_TIMESTAMP_NOT_EXIST << 13;
    frame_info.codec_id = MISS_CODEC_VIDEO_H264;
    flag = p[4];
    if( flag != 0x41  )// I frame
    	frame_info.flags |= FLAG_FRAME_TYPE_IFRAME << 0;
    else
    	frame_info.flags |= FLAG_FRAME_TYPE_PBFRAME << 0;
    if(chn_id == 0)
        frame_info.flags |= FLAG_RESOLUTION_VIDEO_1080P << 17;
    else
        frame_info.flags |= FLAG_RESOLUTION_VIDEO_360P << 17;

	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    //find session at list
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && (psession_node->video_channel == chn_id)) {
            //send stream to miss
            ret = miss_video_send(psession_node->session, &frame_info, p);
            if (0 != ret) {
                log_err("=====>>>>>>avSendFrameData Error: %d,session:%p, videoChn: %d, size: %d\n", ret,
                    psession_node->session, chn_id, msg->extra_size);
            }
            else {
            }
        }
    }
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

static int session_send_audio_stream(int chn_id, message_t *msg)
{
	client_session_t* pclient_session = &client_session;
    miss_frame_header_t frame_info = {0};
    int ret;
    av_data_info_t *avinfo;
    unsigned char	*p;

    p = (unsigned char*)msg->extra;
    avinfo = (av_data_info_t*)msg->arg;
    frame_info.timestamp = avinfo->timestamp;
    frame_info.timestamp_s = avinfo->timestamp/1000;
    frame_info.sequence = avinfo->frame_index;
    frame_info.length = msg->extra_size;
    frame_info.codec_id = MISS_CODEC_AUDIO_G711A;
    frame_info.flags = FLAG_AUDIO_SAMPLE_8K << 3 | FLAG_AUDIO_DATABITS_16 << 7 | FLAG_AUDIO_CHANNEL_MONO << 9 |  FLAG_RESOLUTION_AUDIO_DEFAULT << 17;

	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    //find session at list
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && (psession_node->audio_channel == chn_id)) {
            //send stream to miss
            ret = miss_audio_send(psession_node->session, &frame_info, p);
            if (0 != ret) {
                log_err("=====>>>>>>avSendFrameData Error: %d,session:%p, audioChn: %d, size: %d\n", ret,
                    psession_node->session, chn_id, msg->extra_size);
            }
            else {
            }
        }
    }
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

static int miss_server_connect(void)
{
	int ret = MISS_NO_ERROR;
	char token[2*MAX_SYSTEM_STRING_SIZE] = {0x00};
	char key[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char did[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char model[MAX_SYSTEM_STRING_SIZE] = {0x00};
	char sdk[MAX_SYSTEM_STRING_SIZE] = {0x00};
	miss_server_config_t server = {0};
	miss_device_info_t dev = {0};

	//init client info
    memset(&client_session, 0, sizeof(client_session_t));
    miss_list_init(&client_session.head);

    strcpy(key, config.profile.key);
    strcpy(did, config.profile.did);
    strcpy(model, config.profile.model);
    strcpy(token, config.profile.token);
    strcpy(sdk, config.profile.sdk_type);

	server.max_session_num = config.profile.max_session_num;
	server.max_video_recv_size = config.profile.max_video_recv_size;
	server.max_audio_recv_size = config.profile.max_audio_recv_size;
	server.max_video_send_size = config.profile.max_video_send_size;
	server.max_audio_send_size = config.profile.max_audio_send_size;
	server.device_key = key;
	server.device_key_len = strlen((char*)key);
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
	miss_log_set_path(CONFIG_MISS_LOG_PATH);
	ret = miss_server_init(&dev, &server);
	if (MISS_NO_ERROR != ret) {
        log_err("miss server init fail ret:%d", ret);
        return -1;
	}
	client_session.miss_server_init = 1;
    return 0;
}

static int miss_server_disconnect(void)
{
	int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add miss server wrlock fail, ret = %d", ret);
		return -1;
	}
	if(client_session.miss_server_init == 0) {
		log_info("miss server miss_server_init is %d!", client_session.miss_server_init);
		ret = pthread_rwlock_unlock(&info.lock);
		return 0;
	}
	client_session.miss_server_init = 0;
	log_info("miss_server_finish end");

    //free session list
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(client_session.head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->session) {
            miss_server_session_close(psession_node->session);
	        miss_list_del(&(psession_node->list));
	        free(psession_node);
        }
    }
	miss_server_finish();
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add miss server wrlock fail, ret = %d", ret);
		return -1;
	}
	return 0;
}

static void server_thread_termination(void)
{
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_MISS_SIGINT;
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
	manager_message(&msg);
}

static int server_release(void)
{
	int ret = 0;
	miss_server_disconnect();
	msg_buffer_release(&message);
	return ret;
}

static int server_set_status(int type, int st)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		config.status = st;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return ret;
}

static int server_get_status(int type)
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		st = info.status;
	else if(type== STATUS_TYPE_EXIT)
		st = info.exit;
	else if(type==STATUS_TYPE_CONFIG)
		st = config.status;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg;
	message_t send_msg;
	msg_init(&msg);
	msg_init(&send_msg);
	int st;
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_err("add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1) {
		return 0;
	}
	switch(msg.message){
		case MSG_MANAGER_EXIT:
			server_set_status(STATUS_TYPE_EXIT,1);
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_CLOUD_CONNECTED:
			if( server_get_status(STATUS_TYPE_STATUS) == STATUS_WAIT) {
				if( server_get_status(STATUS_TYPE_CONFIG) == ( (1<<CONFIG_MISS_MODULE_NUM) -1 ) )
					server_set_status(STATUS_TYPE_STATUS, STATUS_SETUP);
			}
			break;
		case MSG_MIIO_MISSRPC_ERROR:
			if( server_get_status(STATUS_TYPE_STATUS) == STATUS_RUN) {
				server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR );
			}
			break;
		case MSG_VIDEO_GET_PARA_ACK:
		case MSG_VIDEO_CTRL_ACK:
		case MSG_VIDEO_CTRL_EXT_ACK:
		case MSG_VIDEO_CTRL_DIRECT_ACK:
		case MSG_VIDEO_START_ACK:
		case MSG_VIDEO_STOP_ACK:
		case MSG_AUDIO_GET_PARA_ACK:
		case MSG_AUDIO_START_ACK:
		case MSG_AUDIO_STOP_ACK:
			if( msg.arg_pass.handler != NULL)
				( *( int(*)(message_arg_t,int,int,void*) ) msg.arg_pass.handler ) (msg.arg_pass, msg.result, msg.arg_size, msg.arg);
			break;
		default:
			log_err("not processed message = %d", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick) > 10 ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_MISS;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		ret = manager_message(&msg);
		/***************************/
	}
	return ret;
}

static void task_error(void)
{
	unsigned int tick=0;
	switch( info.status ) {
		case STATUS_ERROR:
			log_err("!!!!!!!!error in miss, restart in 5 s!");
			info.tick = time_get_now_stamp();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_stamp();
			if( (tick - info.tick) > 5 ) {
				info.exit = 1;
				info.tick = tick;
			}
			break;
	}
	usleep(1000);
	return;
}

static void task_default(void)
{
	int ret = 0;
	switch( info.status ){
		case STATUS_NONE:
			ret = config_miss_read(&config);
			if( ret == 0 )
				server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
			else
				sleep(1);
			break;
		case STATUS_WAIT:
			break;
		case STATUS_SETUP:
		    if(miss_server_connect() < 0) {
		        log_err("create session server fail");
		        server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
		        return;
		    }
		    log_info("create session server finished");
		    server_set_status(STATUS_TYPE_STATUS, STATUS_IDLE);
			break;
		case STATUS_IDLE:
			server_set_status(STATUS_TYPE_STATUS, STATUS_START);
			break;
		case STATUS_START:
			server_set_status(STATUS_TYPE_STATUS, STATUS_RUN);
			break;
		case STATUS_RUN:
			break;
		case STATUS_STOP:
			break;
		case STATUS_RESTART:
			break;
		case STATUS_ERROR:
			info.task.func = task_error;
			break;
	}
	usleep(1000);
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
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
		heart_beat_proc();
	}
	if( info.exit ) {
		while( info.thread_start ) {
		}
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_MISS;
		manager_message(&msg);
		/***************************/
	}
	server_release();
	log_info("-----------thread exit: server_miss-----------");
	pthread_exit(0);
}

/*
 * internal interface
 */
int miss_cmd_get_devinfo(int SID, miss_session_t *session, char *buf)
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
    int ret = miss_cmd_send(session, MISS_CMD_DEVINFO_RESP, (char *)str_resp, strlen(str_resp) + 1);
    if (0 != ret) {
        log_err("miss_cmd_send error, ret: %d", ret);
        return ret;
    }
	return 0;
}

int miss_cmd_video_start(int session_id, miss_session_t *session, char *param)
{
    pthread_t stream_pid;
    int ret;
    int channel=0;
    message_t	msg;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	session_node_t *psession_node = miss_session_check_node(session);
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video start command!");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    if( psession_node->video_status != STREAM_NONE ) {
    	log_err("There is already one active video stream in this session.");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_VIDEO_START;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_pass.cat = MISS_ASYN_VIDEO_START;
	msg.arg_pass.dog = session_id;
	msg.arg_pass.duck = channel;
	msg.arg_pass.handler = miss_message_callback;
    server_video_message(&msg);
	/****************************/
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_video_stop(int session_id, miss_session_t *session,char *param)
{
    int ret = 0;
    message_t msg;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	session_node_t *psession_node = miss_session_check_node(session);
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video stop command!");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    if( psession_node->video_status != STREAM_START ) {
    	log_err("There is no one active video stream in this session.");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_VIDEO_STOP;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_pass.cat = MISS_ASYN_VIDEO_STOP;
	msg.arg_pass.dog = session_id;
	msg.arg_pass.handler = miss_message_callback;
	server_video_message(&msg);
	/****************************/
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	return 0;
}

int miss_cmd_audio_start(int session_id, miss_session_t *session,char *param)
{
    pthread_t stream_pid;
    int ret;
    int channel=0;
    message_t msg;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	session_node_t *psession_node = miss_session_check_node(session);
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during audio start command!");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    if( psession_node->audio_status != STREAM_NONE ) {
    	log_err("There is already one active audio stream in this session.");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_AUDIO_START;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_pass.cat = MISS_ASYN_AUDIO_START;
	msg.arg_pass.dog = session_id;
	msg.arg_pass.duck = channel;
	msg.arg_pass.handler = miss_message_callback;
	server_audio_message(&msg);
	/****************************/
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_audio_stop(int session_id, miss_session_t *session,char *param)
{
	int ret = 0;
	message_t msg;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	session_node_t *psession_node = miss_session_check_node(session);
	if( psession_node==NULL ) {
		log_err("Session wasn't find during audio stop command!");
		ret = pthread_rwlock_unlock(&info.lock);
		return -1;
	}
    if( psession_node->audio_status != STREAM_START ) {
    	log_err("There is no one active audio stream in this session.");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
	/********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_AUDIO_STOP;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_pass.cat = MISS_ASYN_AUDIO_STOP;
	msg.arg_pass.dog = session_id;
	msg.arg_pass.handler = miss_message_callback;
	server_audio_message(&msg);
	/****************************/
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	return 0;
}

int miss_cmd_speaker_start(int session_id, miss_session_t *session, char *param)
{
    int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	session_node_t *psession_node = miss_session_check_node(session);
	if( psession_node==NULL ) {
		log_err("Session wasn't find during speaker start command!");
		ret = pthread_rwlock_unlock(&info.lock);
		return -1;
	}
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_speaker_stop(int session_id, miss_session_t *session, char *param)
{
    int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_video_ctrl(int session_id, miss_session_t *session,char *param)
{
    int ret = 0, vq;
	ret = json_verify_get_int(param, "videoquality", (int *)&vq);
	if (ret < 0) {
		log_info("IOTYPE_USER_IPCAM_SETSTREAMCTRL_REQ: %u\n", (unsigned int)vq);
		return -1;
	} else {
		log_info("IOTYPE_USER_IPCAM_SETSTREAMCTRL_REQ, content: %s\n", param);
	}
    ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	session_node_t *psession_node = miss_session_check_node(session);
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video start command!");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_VIDEO_CTRL;
	msg.arg_in.cat = VIDEO_CTRL_QUALITY;
	msg.arg = &vq;
	msg.arg_size = sizeof(vq);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_pass.cat = MISS_ASYN_VIDEO_CTRL;
	msg.arg_pass.dog = session_id;
	msg.arg_pass.handler = miss_message_callback;
	server_video_message(&msg);
	/****************************/
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_audio_get_format(int session_id, miss_session_t *session, char *param)
{
    int ret;
    message_t	msg;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    session_node_t *psession_node = miss_session_check_node(session);
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during audio check command!");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_AUDIO_GET_PARA;
	msg.sender = msg.receiver = SERVER_MISS;
	msg.arg_pass.cat = MISS_ASYN_AUDIO_FORMAT;
	msg.arg_pass.dog = session_id;
	msg.arg_pass.handler = miss_message_callback;
	/****************************/
    if( server_audio_message(&msg)!=0 ) {
    	log_err("audio check message failed!");
    	ret = pthread_rwlock_unlock(&info.lock);
    	return -1;
    }
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_player_ctrl(int session_id, miss_session_t *session, char *param)
{
    int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_cmd_player_set_speed(int session_id, miss_session_t *session, char *param)
{
    int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_session_add(miss_session_t *session)
{
    session_node_t *session_node = NULL;
    int session_id = -1;
    int ret;

	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}

    if(client_session.use_session_num >= MAX_CLIENT_NUMBER) {
    	log_err("use_session_num:%d max:%d!\n", client_session.use_session_num, MAX_CLIENT_NUMBER);
    	goto SESSION_ADD_ERR;
    }
    //maloc session at list and init it
    session_node = malloc(sizeof(session_node_t));
    if(!session_node) {
        log_err("session add malloc error\n");
        goto SESSION_ADD_ERR;
    }
    memset(session_node, 0, sizeof(session_node_t));
    session_node->session = session;
    miss_list_add_tail(&(session_node->list), &(client_session.head));
    session_id = client_session.use_session_num;
    client_session.use_session_num ++;

    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	log_info("[miss_session_add]miss:%d session_node->session:%d\n",session,session_node->session);
    return session_id;

SESSION_ADD_ERR:
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    log_err("[miss_session_add]miss fail return MISS_ERR_MAX_SESSION\n");
	return -1;
}

int miss_session_del(miss_session_t *session)
{
    int ret = -1;
    message_t msg;

    if(session)
        miss_server_session_close(session);
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    //free session at list
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(client_session.head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->session == session) {
        	if( psession_node->video_status == STREAM_START ) {
        		psession_node->video_status == STREAM_NONE;
        	    /********message body********/
        		msg_init(&msg);
        		msg.message = MSG_VIDEO_STOP;
        		msg.sender = msg.receiver = SERVER_MISS;
        		server_video_message(&msg);
        		/****************************/
        	}
        	if( psession_node->audio_status == STREAM_START ) {
        		psession_node->audio_status == STREAM_NONE;
        	    /********message body********/
        		msg_init(&msg);
        		msg.message = MSG_AUDIO_STOP;
        		msg.sender = msg.receiver = SERVER_MISS;
        		server_audio_message(&msg);
        		/****************************/
        	}
            miss_list_del(&(psession_node->list));
            free(psession_node);
            ret = 0;
            break;
        }

    }
    client_session.use_session_num --;
    ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	return ret;
}

int miss_session_close_all(void)
{
    int ret = MISS_NO_ERROR;
	ret = pthread_rwlock_wrlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	log_info("miss server close all start! \n");
	//close and del all session
	struct list_handle *post = NULL;
	session_node_t *psession_node = NULL;
	list_for_each(post, &(client_session.head)) {
		log_info("miss server free session start! \n");
		psession_node = list_entry(post, session_node_t, list);
		if(psession_node) {
			log_info("miss session close session:%p\n", psession_node->session);
			miss_server_session_close(psession_node->session);
			log_info("miss session del node start!psession_node:%p\n", psession_node);
			miss_list_del(&(psession_node->list));
			log_info("miss session del node end!\n");
			free(psession_node);
			log_info("miss session del node free!\n");
		}
		log_info("miss server free session end! \n");
	}
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	return 0;
}


/*
 * external interface
 */
int server_miss_start(void)
{
	int ret=-1;
	msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	pthread_rwlock_init(&info.lock, NULL);
	pthread_rwlock_init(&video_buff.lock, NULL);
	pthread_rwlock_init(&audio_buff.lock, NULL);
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_err("miss server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_err("miss server create successful!");
		return 0;
	}
}

int server_miss_message(message_t *msg)
{
	int ret=0,ret1;
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_info("push into the miss message queue: sender=%d, message=%d, ret=%d", msg->sender, msg->message, ret);
	if( ret!=0 )
		log_err("message push in miss error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}

int server_miss_video_message(message_t *msg)
{
	int ret=0,ret1;
	ret = pthread_rwlock_wrlock(&video_buff.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&video_buff, msg);
	if( ret!=0 )
		log_err("message push in miss error =%d", ret);
	ret1 = pthread_rwlock_unlock(&video_buff.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}

int server_miss_audio_message(message_t *msg)
{
	int ret=0,ret1=0;
	ret = pthread_rwlock_wrlock(&audio_buff.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&audio_buff, msg);
	if( ret!=0 )
		log_err("message push in miss error =%d", ret);
	ret1 = pthread_rwlock_unlock(&audio_buff.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}
