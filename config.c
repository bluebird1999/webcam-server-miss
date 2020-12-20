/*
 * config_miss.c
 *
 *  Created on: Aug 28, 2020
 *      Author: ning
 */


/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <malloc.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"

/*
 * static
 */
static int						dirty;
static miss_config_t			miss_config;
static config_map_t miss_config_profile_map[] = {
	{"board_type",  			&(miss_config.profile.board_type),  		cfg_u32, 0,0, 0,10,    },
	{"sdk_type",  				&(miss_config.profile.sdk_type),  			cfg_string, 'device',0, 0,32,    },
	{"log_path",                &(miss_config.profile.log_path),            cfg_string, '/tmp/',0, 0,32,    },
    {"max_session_num", 		&(miss_config.profile.max_session_num), 	cfg_s32, 128,0, -1,10000,    },
    {"max_video_recv_size",     &(miss_config.profile.max_video_recv_size), cfg_s32, 512000,0, -1,1000000,	},
    {"max_audio_recv_size",     &(miss_config.profile.max_audio_recv_size), cfg_s32, 51200,0, -1,1000000,  	},
    {"max_video_send_size",     &(miss_config.profile.max_video_send_size), cfg_s32, 512000,0, -1,1000000,	},
    {"max_audio_send_size",     &(miss_config.profile.max_audio_send_size), cfg_s32, 51200,0, -1,1000000,	},
    {NULL,},
};

//function
static int miss_config_device_read(int);
static int miss_config_device_write(void);
static int miss_config_save(void);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int miss_config_save(void)
{
	int ret = 0;
	message_t msg;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	if( misc_get_bit(dirty, CONFIG_MISS_PROFILE) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_MISS_PROFILE_PATH);
		ret = write_config_file(&miss_config_profile_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_MISS_PROFILE, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = miss_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	return ret;
}

int config_miss_update_token(miss_config_t* mconfig)
{
	int ret = 0;
	ret = miss_config_device_read(miss_config.profile.board_type);
	if(!ret)
		misc_set_bit(&miss_config.status, CONFIG_MISS_DEVICE,1);
	else
		misc_set_bit(&miss_config.status, CONFIG_MISS_DEVICE,0);

	strncpy(mconfig->profile.token, miss_config.profile.token, strlen(miss_config.profile.token));
	return ret;
}

static int miss_config_device_read(int board)
{
	FILE *fp = NULL;
	int pos = 0;
	int len = 0;
	char *data = NULL;
	int fileSize = 0;
	int ret;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	//read device.conf
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.miio_path, CONFIG_MISS_DEVICE_PATH);
	fp = fopen(fname, "rb");
	if (fp == NULL) {
		return -1;
	}
	if (0 != fseek(fp, 0, SEEK_END)) {
		fclose(fp);
		return -1;
	}
	fileSize = ftell(fp);
    if(fileSize > 0) {
    	data = malloc(fileSize);
    	if(!data) {
    		fclose(fp);
    		return -1;
    	}
    	memset(data, 0, fileSize);
    	if(0 != fseek(fp, 0, SEEK_SET)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	if (fread(data, 1, fileSize, fp) != (fileSize)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	fclose(fp);
    	char *ptr_did = 0;
    	char *ptr_key = 0;
    	char *ptr_mac = 0;
    	char *ptr_model = 0;
    	char *ptr_vendor = 0;
    	char *p,*m;
    	if( !board ) {
			ptr_did = strstr(data, "did=");
			ptr_key = strstr(data, "key=");
			ptr_mac = strstr(data, "mac=");
    	}
    	ptr_model = strstr(data, "model=");
    	ptr_vendor = strstr(data, "vendor=");
    	if( !board && ptr_did && ptr_key && ptr_mac ) {
    		len = 9;//did length
    		memcpy(miss_config.profile.did,ptr_did+4,len);
    		len = 16;//key length
    		memcpy(miss_config.profile.key,ptr_key+4,len);
    		len = 17;//mac length
    		memcpy(miss_config.profile.mac,ptr_mac+4,len);
    	}
    	if( ptr_model && ptr_vendor) {
			p = ptr_model+6; m = miss_config.profile.model;
			while(*p!='\n' && *p!='\0') {
				memcpy(m, p, 1);
				m++;p++;
			}
			*m = '\0';
			p = ptr_vendor+7; m = miss_config.profile.vendor;
			while(*p!='\n' && *p!='\0') {
				memcpy(m, p, 1);
				m++;p++;
			}
			*m = '\0';
    	}
    	free(data);
    }
	fileSize = 0;
	len = 0;
	//read device.token
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.miio_path, CONFIG_MISS_TOKEN_PATH);
	fp = fopen(fname, "rb");
	if (fp == NULL) {
		return -1;
	}
	if (0 != fseek(fp, 0, SEEK_END)) {
		fclose(fp);
		return -1;
	}
	fileSize = ftell(fp);
    if(fileSize > 0) {
    	data = malloc(fileSize);
    	if(!data) {
    		fclose(fp);
    		return -1;
    	}
    	memset(data, 0, fileSize);
    	if(0 != fseek(fp, 0, SEEK_SET)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	if (fread(data, 1, fileSize, fp) != (fileSize)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	fclose(fp);
		memcpy(miss_config.profile.token,data,fileSize-1);
    	free(data);
    }
    else {
		log_qcy(DEBUG_SERIOUS, "device.token -->file date err!!!\n");
        return -1;
    }
	return 0;
}

static int miss_config_device_write(void)
{
	int ret=0;
	return ret;
}

/*
 * interface
 */
int config_miss_read(miss_config_t *mconfig)
{
	int ret,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(&miss_config.profile, 0, sizeof(miss_profile_t));
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_MISS_PROFILE_PATH);
	ret = read_config_file(&miss_config_profile_map, fname);
	if(!ret1) {
		misc_set_bit(&miss_config.status, CONFIG_MISS_PROFILE,1);
	}
	else
		misc_set_bit(&miss_config.status, CONFIG_MISS_PROFILE,0);
	ret1 |= ret;
	ret = miss_config_device_read(miss_config.profile.board_type);
	if(!ret)
		misc_set_bit(&miss_config.status, CONFIG_MISS_DEVICE,1);
	else
		misc_set_bit(&miss_config.status, CONFIG_MISS_DEVICE,0);
	ret1 |= ret;
	memcpy(mconfig, &miss_config, sizeof(miss_config_t));
	return ret1;
}

int config_miss_set(int module, void *arg)
{
	int ret = 0;
	if(dirty==0) {
		message_t msg;
		message_arg_t arg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_ADD;
		msg.sender = SERVER_MISS;
		msg.arg_in.cat = FILE_FLUSH_TIME;	//1min
		msg.arg_in.dog = 0;
		msg.arg_in.duck = 0;
		msg.arg_in.handler = &miss_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_MISS_PROFILE) {
		memcpy( (miss_profile_t*)(&miss_config.profile), arg, sizeof(miss_profile_t));
	}
	return ret;
}
