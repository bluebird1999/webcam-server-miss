/*
 * miss_porting.c
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
#include <miss.h>
#include <miss_porting.h>
#include <malloc.h>
#ifdef DMALLOC_ENABLE
#include <dmalloc.h>
#endif
//program header
#include "../../tools/tools_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/speaker/speaker_interface.h"
#include "../../server/player/player_interface.h"
//server header
#include "miss.h"
#include "miss_interface.h"
#include "miss_local.h"

/*
 * static
 */
//variable
static 	miss_msg_t 	miss_msg;
//function

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * internal interface
 */
void* miss_get_context_from_id(int id)
{
	int i = 0;
	for (i = 0; i < MISS_MSG_MAX_NUM; i++) {
		if (miss_msg.msg_id[i] == id) {
			miss_msg.msg_id[i] = 0;
			miss_msg.timestamps[i] = 0;
			miss_msg.msg_num --;
			return miss_msg.rpc_id[i];
		}
	}
	return 0;
}

/*
 * interface & porting implementation
 */
int miss_rpc_send(void *rpc_id, const char *method, const char *params)
{
	int i = 0;
	int msg_id = misc_generate_random_id();
	if (miss_msg.msg_num >= MISS_MSG_MAX_NUM) {
		log_qcy(DEBUG_SERIOUS, "too much rpc msg processing!\n");
		return -1;
	}
	if (miss_msg.msg_num >= MISS_MSG_MAX_NUM) {
		log_qcy(DEBUG_SERIOUS, "no free msg id: %d!\n", miss_msg.msg_num);
		return -1;
	}
	for (i = 0; i< MISS_MSG_MAX_NUM; i++) {
		if (miss_msg.msg_id[i] == 0) {
			miss_msg.msg_id[i] = msg_id;
			miss_msg.rpc_id[i] = rpc_id;
			miss_msg.timestamps[i] = time(NULL);
			miss_msg.msg_num ++;
			break;
		}
	}
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.arg_in.cat = msg_id;
	msg.arg_in.handler = rpc_id;
	msg.message = MSG_MIIO_RPC_SEND;
	msg.sender = SERVER_MISS;
	msg.arg = params;
	msg.arg_size = strlen(params)+1;
	msg.extra = method;
	msg.extra_size = strlen(method)+1;
	log_qcy(DEBUG_VERBOSE, "---------miss rpc params---%s---------", params);
	log_qcy(DEBUG_VERBOSE, "---------miss rpc method---%s---------", method);
	/***************************/
	manager_common_send_message(SERVER_MIIO,   &msg);
}

int miss_statistics(miss_session_t *session, void *data, int len)
{
	int msg_id = misc_generate_random_id();
	if (data == NULL || len == 0) {
		log_qcy(DEBUG_WARNING, "miss_statistics params error!\n");
	}
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.arg_in.cat = msg_id;
	msg.message = MSG_MIIO_RPC_REPORT_SEND;
	msg.sender = SERVER_MISS;
	msg.arg_in.handler = (void*)session;
	msg.arg = data;
	msg.arg_size = len + 1;
	msg.extra = "_sync.camera_perf_data";
	msg.extra_size = strlen(msg.extra) + 1;
	/***************************/
	log_qcy(DEBUG_VERBOSE, "---------miss report data---%s---------", data);
	manager_common_send_message(SERVER_MIIO,   &msg);
}

int miss_on_connect(miss_session_t *session, void **user_data)
{
    int session_id = 0;
    log_qcy(DEBUG_INFO, "miss session connect session:%d \n",session);
    if(!session) {
        log_qcy(DEBUG_WARNING, "session is NULL return MISS_ERR_ABORTED\n");
        return MISS_ERR_ABORTED;
    }
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.message = MSG_MISS_SESSION_STATUS;
	msg.arg_in.cat = SESSION_STATUS_ADD;
	msg.arg_in.handler = (void*)session;
	msg.arg = user_data;
	manager_common_send_message(SERVER_MISS, &msg);
	return 0;
}

int miss_on_disconnect(miss_session_t *session, miss_error_e error, void *user_data)
{
	int session_id = -1;
	log_qcy(DEBUG_INFO, "miss server on disconnection, error=%d\n", error);
    if(!session) {
        log_qcy(DEBUG_WARNING, "session add:%p, user_data:%p return MISS_ERR_ABORTED\n", session, user_data);
        return MISS_ERR_ABORTED;
    }
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.message = MSG_MISS_SESSION_STATUS;
	msg.arg_in.cat = SESSION_STATUS_REMOVE;
	msg.arg_in.dog = error;
	msg.arg_in.handler = (void*)session;
	msg.arg = user_data;
	manager_common_send_message(SERVER_MISS,   &msg);
	return 0;
}

int miss_on_error(miss_session_t *session, miss_error_e error, void *user_data)
{
	log_qcy(DEBUG_SERIOUS, "miss server on error:%d\n", error);
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.message = MSG_MISS_SESSION_STATUS;
	msg.arg_in.cat = SESSION_STATUS_ERROR;
	msg.arg_in.dog = error;
	msg.arg_in.handler = (void*)session;
	msg.arg = user_data;
	manager_common_send_message(SERVER_MISS,   &msg);
	return 0;
}
/*
 * miss_on_audio_data() - MISS on audio data callback
 */
void miss_on_audio_data(miss_session_t *session,
		miss_frame_header_t *frame_header, void *data, void *user_data)
{
	message_t msg;
	int ret = 0;
    if (frame_header->length > 0) {
        /********message body********/
    	msg_init(&msg);
    	msg.message = MSG_SPEAKER_CTL_DATA;
    	msg.sender = msg.receiver = SERVER_MISS;
    	msg.arg_in.cat = SPEAKER_CTL_INTERCOM_DATA;
    	msg.arg = data;
    	msg.arg_size = frame_header->length;
    	ret = manager_common_send_message(SERVER_SPEAKER,    &msg);
    	/***************************/
    }
}

void miss_on_video_data(miss_session_t *session,
		miss_frame_header_t *frame, void *data, void *user_data)
{
	return ;
}

void miss_on_rdt_data(miss_session_t *session, void *rdt_data, uint32_t length, void *user_data)
{
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.message = MSG_MISS_RDT;
	msg.arg_in.cat = 1;
	msg.arg_in.handler = (void*)session;
	msg.arg = rdt_data;
	msg.arg_size = length + 1;
	msg.arg_pass.handler = (void*)session;
	if( user_data ) {
		msg.arg_in.wolf = *(int*)user_data;
		msg.arg_pass.wolf = *(int*)user_data;
	}
	manager_common_send_message(SERVER_MISS,   &msg);
    return;
}

int miss_on_server_ready()
{
	log_qcy(DEBUG_INFO, "------miss on server ready message------------");
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.message = MSG_MISS_SERVER_STATUS;
	msg.arg_in.cat = 1;
	manager_common_send_message(SERVER_MISS,   &msg);
	return 0;
}

void miss_on_cmd(miss_session_t *session, miss_cmd_e cmd,
		void *params, unsigned int length, void *user_data)
{
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_MISS;
	msg.message = MSG_MISS_CMD;
	if( user_data ) {
		msg.arg_in.wolf = *(int*)user_data;
		msg.arg_pass.wolf = *(int*)user_data;
	}
	msg.arg_in.handler = (void*)session;
	msg.arg = params;
	msg.arg_size = length + 1;
	msg.arg_pass.handler = (void*)session;
	msg.arg_in.cat = cmd;
	manager_common_send_message(SERVER_MISS,   &msg);
	return;
}
