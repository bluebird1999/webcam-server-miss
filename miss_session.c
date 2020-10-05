/*
 * miss_session.c
 *
 *  Created on: Aug 15, 2020
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
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include <miss.h>
//program header
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/cJSON/cJSON.h"
#include "../../tools/json/json.h"
#include "../../tools/log.h"
#include "../../tools/misc.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/miio/miio.h"
#include "../../manager/manager_interface.h"
#include "../../server/config/config_miss_interface.h"
#include "../../server/config/config_miio_interface.h"
#include "../../server/video/video_interface.h"
#include "../../server/audio/audio_interface.h"
//server header
#include "miss_interface.h"
#include "miss_session.h"
#include "miss_local.h"
#include "miss_session_list.h"

/*
 * static
 */
//variable

//function
static int session_get_status(miss_session_t *session);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
int miss_session_video_start(int session_id, miss_session_t *session, char *param)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    pthread_t stream_pid;
    int ret;
    int channel=0;
    message_t	msg;

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
        if(psession_node && psession_node->session == session) {
        	break;
        }
    }
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video start command!");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
    if( psession_node->video_status != STREAM_NONE ) {
    	log_err("There is already one active video stream in this session.");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
	log_info("\n========start new stream thread=========\n");
	psession_node->video_status = STREAM_START;
	psession_node->video_channel = channel;
	pthread_create(&stream_pid, NULL, session_stream_send_video_func, (void*)psession_node);
	psession_node->video_tid = stream_pid;
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_VIDEO_START;
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
    if( server_video_message(&msg)!=0 ) {
    	log_err("video server start failed!");
    	psession_node->video_status = STREAM_NONE;
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_session_video_stop(int session_id, miss_session_t *session,char *param)
{
    int ret = 0;
    client_session_t* pclient_session= server_miss_get_client_info();
    message_t msg;
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
        if(psession_node && psession_node->session == session) {
			if( psession_node->video_status == STREAM_START){
				psession_node->video_status = STREAM_NONE;
            }
            break;
        }
    }
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video stop command!");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_VIDEO_STOP;
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
    if( server_video_message(&msg)!=0 ) {
    	log_err("video server stop failed!");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
    psession_node->video_tid = -1;
    psession_node->video_status = STREAM_NONE;
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	return 0;
}

int miss_session_audio_start(int session_id, miss_session_t *session,char *param)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    pthread_t stream_pid;
    int ret;
    int channel=0;
    message_t msg;
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
        if(psession_node && psession_node->session == session) {
        	break;
        }
    }
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video start command!");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
    if( psession_node->audio_status != STREAM_NONE ) {
    	log_err("There is already one active audio stream in this session.");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
	log_info("\n========start new stream thread=========\n");
	psession_node->audio_status = STREAM_START;
	psession_node->audio_channel = channel;
	pthread_create(&stream_pid, NULL, session_stream_send_audio_func, (void*)psession_node);
	psession_node->audio_tid = stream_pid;
    /********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_AUDIO_START;
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
    if( server_audio_message(&msg)!=0 ) {
    	log_err("audio server start failed!");
    	psession_node->video_status = STREAM_NONE;
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_session_audio_stop(int session_id, miss_session_t *session,char *param)
{
	int ret = 0;
	client_session_t* pclient_session= server_miss_get_client_info();
	message_t msg;
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
		if(psession_node && psession_node->session == session) {
			if( psession_node->audio_status == STREAM_START){
				psession_node->audio_status = STREAM_NONE;
			}
			break;
		}
	}
	if( psession_node==NULL ) {
		log_err("Session wasn't find during audio stop command!");
		ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
		return -1;
	}
	/********message body********/
	memset(&msg,0,sizeof(message_t));
	msg.message = MSG_AUDIO_STOP;
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
	if( server_audio_message(&msg)!=0 ) {
		log_err("audio server stop failed!");
		ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
		return -1;
	}
	psession_node->audio_tid = -1;
	psession_node->audio_status = STREAM_NONE;
	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	return 0;
}

int miss_session_speaker_start(miss_session_t *session,char *param)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    int ret;
	int speaker_enable = 0;

	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    //find session at list
    struct list_handle *post;
    session_node_t *psession_node = NULL, *pfins_session_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->session == session) {
        	pfins_session_node = session;
        }
    }
	if(pfins_session_node && speaker_enable == 0) {
	}
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	if(speaker_enable == 0)
		return 0;
    return -1;
}

int miss_session_speaker_stop(miss_session_t *session,char *param)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    int ret;
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
        if(psession_node && psession_node->session == session) {
        }
    }
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_session_video_ctrl(miss_session_t *session,char *param)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    int ret = 0, vq;

	ret = json_verify_get_int(param, "videoquality", (int *)&vq);
	if (ret < 0) {
		log_info("IOTYPE_USER_IPCAM_SETSTREAMCTRL_REQ: %u\n", (unsigned int)vq);
		return -1;
	} else {
		log_info("IOTYPE_USER_IPCAM_SETSTREAMCTRL_REQ, content: %s\n", param);
	}
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
        if(psession_node && psession_node->session == session) {

            break;
        }
    }
    if( psession_node==NULL ) {
    	log_err("Session wasn't find during video start command!");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_VIDEO_CTRL;
	msg.arg_in.cat = VIDEO_CTRL_QUALITY;
	msg.arg = &vq;
	msg.arg_size = sizeof(int);
	msg.sender = msg.receiver = SERVER_MISS;
	/****************************/
    if( server_video_message(&msg)!=0 ) {
    	log_err("change video parameter failed!");
    	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
    	return -1;
    }
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    return 0;
}

int miss_session_add(miss_session_t *session)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    session_node_t *session_node = NULL;
    int session_id = -1;
    int ret;

	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}

    if(pclient_session->use_session_num >= MAX_CLIENT_NUMBER) {
    	log_err("use_session_num:%d max:%d!\n", pclient_session->use_session_num, MAX_CLIENT_NUMBER);
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
    log_err("[miss_session_add]miss:%d session_node:%p\n",session,session_node);
    miss_list_add_tail(&(session_node->list), &(pclient_session->head));
    log_err("[miss_session_add]miss:%d session_node->session:%d\n",session,session_node->session);
    session_id = pclient_session->use_session_num;
	pclient_session->use_session_num ++;

    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
	log_err("[miss_session_add]miss:%d session_node->session:%d\n",session,session_node->session);
    return session_id;

SESSION_ADD_ERR:
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    log_err("[miss_session_add]miss fail return MISS_ERR_MAX_SESSION\n");
	return -1;
}

int miss_session_del(miss_session_t *session)
{
    client_session_t* pclient_session= server_miss_get_client_info();
    int ret, session_del_ok = 0;
    message_t msg;

    if(session)
        miss_server_session_close(session);

	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
    //free session at list
    struct list_handle *post;
    session_node_t *psession_node = NULL;
    list_for_each(post, &(pclient_session->head)) {
        psession_node = list_entry(post, session_node_t, list);
        if(psession_node && psession_node->session == session) {
        	if( psession_node->video_status == STREAM_START ) {
        		psession_node->video_status == STREAM_NONE;
        	    /********message body********/
        		msg_init(&msg);
        		msg.message = MSG_VIDEO_STOP;
        		/****************************/
        		server_video_message(&msg);
        	}
        	if( psession_node->audio_status == STREAM_START ) {
        		psession_node->audio_status == STREAM_NONE;
        	    /********message body********/
        		msg_init(&msg);
        		msg.message = MSG_AUDIO_STOP;
        		/****************************/
//        		server_audio_message(&msg);
        	}
            miss_list_del(&(psession_node->list));
            free(psession_node);
            session_del_ok = 1;
            break;
        }

    }
    pclient_session->use_session_num --;
    ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session unlock fail, ret = %d\n", ret);
	}
    if(session_del_ok == 1)
        return 0;
	return -1;
}

int miss_session_close_all(void)
{
    int ret = MISS_NO_ERROR;
    client_session_t* pclient_session= server_miss_get_client_info();

	ret = pthread_rwlock_wrlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}

	log_info("miss server close all start! \n");
	//close and del all session
	struct list_handle *post = NULL;
	session_node_t *psession_node = NULL;
	list_for_each(post, &(pclient_session->head)) {
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

	ret = pthread_rwlock_unlock(&server_miss_get_server_info()->lock);
	if (ret) {
		log_err("add session wrlock fail, ret = %d\n", ret);
		return -1;
	}
	return 0;
}
