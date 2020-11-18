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
static int rdt_cmd_parse(char *buf, int len, miss_session_t *session, int enableRdt);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */
/*
 * miss_porting helper function
 */
static int rdt_cmd_parse(char *buf, int len, miss_session_t *session, int enableRdt)
{
    int ret = 0;
	char *msg = buf;
    unsigned int cmd_type;
    message_t message;
    char starttime[MAX_SYSTEM_STRING_SIZE];
    char endtime[MAX_SYSTEM_STRING_SIZE];
    memset(starttime, 0, sizeof(starttime));
    memset(endtime, 0, sizeof(endtime));
    if (NULL == buf)
        return -1;
    log_qcy(DEBUG_INFO, "recv msg: %s\n",msg);
	ret = json_verify_get_int(msg, "cmdtype", (int *)&cmd_type);
	if (ret < 0) {
		log_qcy(DEBUG_WARNING, "error param: cmdtype\n");
		return -1;
	}
    switch (cmd_type) {
        case GET_RECORD_FILE:
        case GET_RECORD_TIMESTAMP:
            ret = json_verify_get_string(buf,"starttime",starttime,sizeof(starttime)-1);
            if (ret < 0) {
            	log_qcy(DEBUG_WARNING, "cmdtype 5 error param: starttime\n");
                return -1;
            }
            ret = json_verify_get_string(buf,"endtime",endtime,sizeof(endtime)-1);
            if (ret < 0) {
            	log_qcy(DEBUG_WARNING,"cmdtype 5 error param: endtime\n");
                return -1;
            }
        	/******************************/
        	msg_init(&message);
        	message.message = MSG_PLAYER_GET_FILE_LIST;
        	message.sender = message.receiver = SERVER_MISS;
        	message.arg_pass.handler = session;
        	message.arg_pass.cat = cmd_type;
        	message.arg_in.cat = (unsigned int)time_date_to_stamp(starttime);// - _config_.timezone * 3600);
        	message.arg_in.dog = (unsigned int)time_date_to_stamp(endtime);// - _config_.timezone * 3600);
        	server_player_message(&message);
        	/******************************/
            break;
        case GET_RECORD_DATE:
        	/******************************/
        	msg_init(&message);
        	message.message = MSG_PLAYER_GET_FILE_DATE;
        	message.sender = message.receiver = SERVER_MISS;
        	message.arg_pass.handler = session;
        	message.arg_pass.cat = cmd_type;
        	server_player_message(&message);
        	/******************************/
        	break;
        case GET_RECORD_PICTURE: {
            int ret = 0;
            unsigned int Num = 0;;
            char starttime[32];
            char endtime[32];
            memset(starttime, 0, sizeof(starttime));
            memset(endtime, 0, sizeof(endtime));
            ret = json_verify_get_string(buf,"starttime",starttime,sizeof(starttime)-1);
            if (ret < 0)
            {
                log_info ("cmdtype 5 error param: starttime\n");
                return -1;
            }
            ret = json_verify_get_string(buf,"endtime",endtime,sizeof(endtime)-1);
            if (ret < 0)
            {
                log_info ("cmdtype 5 error param: endtime\n");
                return -1;
            }
            //log_qcy(DEBUG_SERIOUS, "starttime: %s endtime:%s\n",starttime,endtime);
//          miss_playbackSearch_pic(session,starttime,endtime);
            break;
        }
        case GET_RECORD_MSG: {
            break;
        }
        default:
            log_qcy(DEBUG_SERIOUS, "Undefind cmd_type=%u!!!!!\n", cmd_type);
            break;
    }
    return 0;
}

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
	server_miio_message(&msg);
}

int miss_statistics(miss_session_t *session, void *data, int len)
{
	int msg_id = misc_generate_random_id();
	if (data == NULL || len == 0) {
		log_qcy(DEBUG_SERIOUS, "miss_statistics params error!\n");
	}
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.arg_in.cat = msg_id;
	msg.message = MSG_MIIO_RPC_REPORT_SEND;
	msg.sender = SERVER_MISS;
	msg.arg = data;
	msg.arg_size = len + 1;
	msg.extra = "_sync.camera_perf_data";
	msg.extra_size = strlen(msg.extra) + 1;
	/***************************/
	log_qcy(DEBUG_VERBOSE, "---------miss report data---%s---------", data);
	server_miio_message(&msg);
}

int miss_on_connect(miss_session_t *session, void **user_data)
{
    int session_id = 0;
    int *pSession_valu = NULL;
    log_qcy(DEBUG_INFO, "miss session connect session:%d \n",session);
    if(!session) {
        log_qcy(DEBUG_SERIOUS, "session is NULL return MISS_ERR_ABORTED\n");
        return MISS_ERR_ABORTED;
    }
	if((session_id = miss_session_add(session)) < 0) {
		log_qcy(DEBUG_SERIOUS, "miss session not valid session id, return MISS_ERR_MAX_SESSION!\n");
        //miss_server_session_close(session);
		return MISS_ERR_ABORTED;
	}
    pSession_valu = malloc(sizeof(int));
    *pSession_valu = session_id;
    *user_data = pSession_valu;
	return 0;
}

int miss_on_error(miss_session_t *session, miss_error_e error, void *user_data)
{
	log_qcy(DEBUG_SERIOUS, "miss server on error:%d\n", error);
	if(error == MISS_ERR_TIMOUT ) {
		;
	}
	else if(error == MISS_ERR_ABORTED) {
//		server_miss_message(MSG_MISS_SERVER_ERROR,NULL);
	}
	return 0;
}

int miss_on_disconnect(miss_session_t *session, miss_error_e error, void *user_data)
{
	int session_id = -1;
	log_qcy(DEBUG_INFO, "miss server on disconnection, error=%d\n", error);
    if(!session) {
        log_qcy(DEBUG_SERIOUS, "session add:%p, user_data:%p return MISS_ERR_ABORTED\n", session, user_data);
        return MISS_ERR_ABORTED;
    }
    if( error != MISS_ERR_CLOSE_BY_LOCAL)
    	miss_session_del(session);
    if(user_data) {
		session_id = *(int*)(user_data);
	    free(user_data);
	    log_qcy(DEBUG_SERIOUS, "miss disconnect user_data:%d\n", session_id);
	}
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
    	ret = server_speaker_message(&msg);
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
	rdt_cmd_parse(rdt_data, length, session, 1);
    return;
}

int miss_on_server_ready()
{
	log_qcy(DEBUG_INFO, "------miss on server ready message------------");
	return 0;
}

void miss_on_cmd(miss_session_t *session, miss_cmd_e cmd,
		void *params, unsigned int length, void *user_data)
{
	log_qcy(DEBUG_SERIOUS, "miss_on_cmd sesson:%p, cmd:%d, len:%d\n", session, cmd, length);
	int sessionnum = *(int*)user_data;
	char test_player[256]={0};
	long long int start, end;
	switch (cmd) {
	case MISS_CMD_VIDEO_START:
		miss_cmd_video_start(sessionnum, session, (char*)params);
		break;
	case MISS_CMD_VIDEO_STOP:
		miss_cmd_video_stop(sessionnum, session, (char*)params);
		break;
	case MISS_CMD_AUDIO_START:
/*		start = time_date_to_stamp("20201025153835") - _config_.timezone * 3600;
		printf("-------------------%d",start);
		end =   time_date_to_stamp("20201025153855") - _config_.timezone * 3600;
		printf("-------------------%d",end);
		sprintf(test_player, "{\"starttime\":%ld,\"offset\":1,\"speed\":1,\"autoswitchtolive\":1,\"sessionid\":1,\"avchannelmerge\":1, \"endtime\":1603604395,\"op\":1}", start, end);
		miss_cmd_player_ctrl(sessionnum, session, (char*)test_player);
*/
		miss_cmd_audio_start(sessionnum, session, (char*)params);
		break;
	case MISS_CMD_AUDIO_STOP:
/*
		start = time_date_to_stamp("20201021200159") - _config_.timezone * 3600;
		end =   time_date_to_stamp("20201021201358") - _config_.timezone * 3600;
		strcpy(test_player,"{\"starttime\":1583078400,\"offset\":1,\"speed\":1,\"autoswitchtolive\":1,\"sessionid\":1,\"avchannelmerge\":1,\"endtime\":1583251199,\"op\":0}");
		miss_cmd_player_ctrl(sessionnum, session, (char*)test_player);
*/
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
	case MISS_CMD_PLAYBACK_REQ:
		miss_cmd_player_ctrl(sessionnum, session, (char*)params);
		break;
	case MISS_CMD_PLAYBACK_SET_SPEED:
		miss_cmd_player_set_speed(sessionnum, session, (char*)params);
		break;
	case MISS_CMD_DEVINFO_REQ:
		miss_cmd_get_devinfo(sessionnum, session, (char*)params);
		break;
	case MISS_CMD_MOTOR_REQ:
		miss_cmd_motor_ctrl(sessionnum, session, (char*)params);
		break;
	default:
		log_qcy(DEBUG_SERIOUS, "unknown cmd:0x%x\n", cmd);
		return ;
	}
	return ;
}
