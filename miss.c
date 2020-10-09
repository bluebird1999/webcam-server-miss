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
//program header
#include "../../manager/manager_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/config/config_miio_interface.h"
#include "../../server/config/config_miss_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/config/config_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
#include "../../server/realtek/realtek_interface.h"
//server header
#include "miss.h"
#include "miss_interface.h"
#include "miss_session.h"
#include "miss_session_list.h"
#include "miss_local.h"

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
static int					thread_start;
static int					thread_exit;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static int server_none(void);
static int server_wait(void);
static int server_setup(void);
static int server_idle(void);
static int server_start(void);
static int server_run(void);
static int server_stop(void);
static int server_restart(void);
static int server_error(void);
static int server_release(void);
static int server_set_thread_start(int index);
static int server_set_thread_exit(int index);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static void server_thread_termination(void);
//specific
static int miss_server_connect(void);
static int miss_server_disconnect(void);
static int session_send_video_stream(int chn_id, message_t *msg);
static int session_send_audio_stream(int chn_id, message_t *msg);
static stream_status_t session_get_node_status(session_node_t *node, int mode);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */


/*
 * helper
 */
static int server_set_thread_exit(int index)
{
	thread_exit |= (1<<index);
}

static int server_set_thread_start(int index)
{
	thread_start |= (1<<index);
}

static stream_status_t session_get_node_status(session_node_t *node, int mode)
{
	int ret;
	stream_status_t status;

	if( node==NULL )
		return STREAM_NONE;
	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return STREAM_NONE;
	}
	if( mode==0 )
		status = node->video_status;
	else if( mode==1 )
		status = node->audio_status;

    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return status;
}

static int session_send_video_stream(int chn_id, message_t *msg)
{
	client_session_t* pclient_session = server_miss_get_client_info();
    miss_frame_header_t frame_info = {0};
    int ret;
    int flag;
    av_data_info_t	*info;
    unsigned char	*p;

    p = (unsigned char*)msg->extra;
    info = (av_data_info_t*)(msg->arg);
    frame_info.timestamp = info->timestamp;
    frame_info.timestamp_s = info->timestamp/1000;
    frame_info.sequence = info->frame_index;
    frame_info.length = msg->extra_size;
    frame_info.flags |= FLAG_STREAM_TYPE_LIVE << 11;
    frame_info.flags |= FLAG_WATERMARK_TIMESTAMP_NOT_EXIST << 13;
    frame_info.codec_id = MISS_CODEC_VIDEO_H264;
    if( h264_is_iframe( &p[4] )  )// I frame
    	frame_info.flags |= FLAG_FRAME_TYPE_IFRAME << 0;
    else
    	frame_info.flags |= FLAG_FRAME_TYPE_PBFRAME << 0;
    if(chn_id == 0)
        frame_info.flags |= FLAG_RESOLUTION_VIDEO_1080P << 17;
    else
        frame_info.flags |= FLAG_RESOLUTION_VIDEO_360P << 17;

	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
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
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

static int session_send_audio_stream(int chn_id, message_t *msg)
{
	client_session_t* pclient_session = server_miss_get_client_info();
    miss_frame_header_t frame_info = {0};
    int ret;
    av_data_info_t *info;
    unsigned char	*p;

    p = (unsigned char*)msg->extra;
    info = (av_data_info_t*)msg->arg;
    frame_info.timestamp = info->timestamp;
    frame_info.timestamp_s = info->timestamp/1000;
    frame_info.sequence = info->frame_index;
    frame_info.length = msg->extra_size;
    frame_info.codec_id = MISS_CODEC_AUDIO_G711A;
    frame_info.flags = FLAG_AUDIO_SAMPLE_8K << 3 | FLAG_AUDIO_DATABITS_16 << 7 | FLAG_AUDIO_CHANNEL_MONO << 9 |  FLAG_RESOLUTION_AUDIO_DEFAULT << 17;

	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
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
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
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
	message_arg_t *rd;
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
	case MSG_CONFIG_READ_ACK:
		if( msg.result==0 ) {
			memcpy( (miss_config_t*)(&config), (miss_config_t*)msg.arg, msg.arg_size);
		}
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
	}
	msg_free(&msg);
	return ret;
}

/*
 * State Machine
 */
static int server_none(void)
{
	static int try=0;
	int ret = 0;
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_CONFIG_READ;
	msg.sender = msg.receiver = SERVER_MISS;
	/***************************/
	ret = server_config_message(&msg);
	if( ret == 0 )
		server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
	else
		sleep(1);
	return ret;
}

static int server_wait(void)
{
	int ret = 0;
	return 0;
}

static int server_setup(void)
{
	int ret = 0;
    if(miss_server_connect() < 0) {
        log_err("create session server fail");
        server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
        return -1;
    }
    log_info("create session server finished");
    server_set_status(STATUS_TYPE_STATUS, STATUS_IDLE);
	return ret;
}

static int server_idle(void)
{
	int ret = 0;
	return ret;
}

static int server_start(void)
{
	int ret = 0;
	return ret;
}

static int server_run(void)
{
	int ret = 0;
	return ret;
}

static int server_stop(void)
{
	int ret = 0;
	return ret;
}

static int server_restart(void)
{
	int ret = 0;
	return ret;
}

static int server_error(void)
{
	int ret = 0;
	server_release();
	log_err("!!!!!!!!error in miss!!!!!!!!");
	return ret;
}

/*
 * Server Main function
 */
static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_miss");
	pthread_detach(pthread_self());

	while( !server_get_status(STATUS_TYPE_EXIT) ) {
	switch( server_get_status(STATUS_TYPE_STATUS) ){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			server_wait();
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			server_idle();
			break;
		case STATUS_START:
			server_start();
			break;
		case STATUS_RUN:
			server_run();
			break;
		case STATUS_STOP:
			server_stop();
			break;
		case STATUS_RESTART:
			server_restart();
			break;
		case STATUS_ERROR:
			server_error();
			break;
		}
		server_message_proc();
	}
	server_release();
	if( server_get_status(STATUS_TYPE_EXIT) ) {
		while( thread_exit != thread_start ) {
		}
		message_t msg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_MISS;
		/***************************/
		manager_message(&msg);
		thread_start = 0;
		thread_exit = 0;
	}
	log_info("-----------thread exit: server_miss-----------");
	pthread_exit(0);
}

/*
 * internal interface
 */
int server_miss_get_info(int SID, miss_session_t *session, char *buf)
{
    char str_resp[1024] = { 0 };
	char str_did[64] = { 0 };
	char str_mac[18] = { 0 };
	char str_version[64] = { 0 };
    int wifi_rssi = 100;
    //debug test hyb add
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

client_session_t *server_miss_get_client_info(void)
{
	return &client_session;
}

server_info_t *server_miss_get_server_info(void)
{
	return &info;
}

void *session_stream_send_video_func(void *arg)
{
	session_node_t *node=(session_node_t*)arg;
    int ret, ret1, channel;
    message_t	msg;

    server_set_thread_start(THREAD_VIDEO);
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
    server_set_thread_exit(THREAD_VIDEO);
    pthread_exit(0);
}

void *session_stream_send_audio_func(void *arg)
{
	session_node_t *node=(session_node_t*)arg;
    int ret, ret1, channel;
    message_t	msg;

    server_set_thread_start(THREAD_AUDIO);
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
    server_set_thread_exit(THREAD_AUDIO);
    pthread_exit(0);
}

/*
 * external interface
 */
int server_miss_start(void)
{
	int ret=-1;
	msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	pthread_rwlock_init(&info.lock, NULL);
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
