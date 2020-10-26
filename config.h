/*
 * config_miss.h
 *
 *  Created on: Aug 28, 2020
 *      Author: ning
 */

#ifndef SERVER_MISS_CONFIG_H_
#define SERVER_MISS_CONFIG_H_

/*
 * header
 */

/*
 * define
 */
#define		CONFIG_MISS_MODULE_NUM 		2
#define		CONFIG_MISS_PROFILE			0
#define		CONFIG_MISS_DEVICE			1

#define 	CONFIG_MISS_LOG_PATH				"/mnt/nfs/log/miss.log"
#define 	CONFIG_MISS_PROFILE_PATH			"/opt/qcy/config/miss_profile.config"
#define		CONFIG_MISS_DEVICE_PATH				"/etc/miio/device.conf"
#define		CONFIG_MISS_TOKEN_PATH				"/etc/miio/device.token"

/*
 * structure
 */
typedef struct miss_profile_t {
	int		board_type;
	char 	did[MAX_SYSTEM_STRING_SIZE];
	char 	key[MAX_SYSTEM_STRING_SIZE];
	char 	mac[MAX_SYSTEM_STRING_SIZE];
	char 	model[MAX_SYSTEM_STRING_SIZE];
	char 	vendor[MAX_SYSTEM_STRING_SIZE];
	char 	sdk_type[MAX_SYSTEM_STRING_SIZE];
	char 	token[2*MAX_SYSTEM_STRING_SIZE];
	int		max_session_num;
	int		max_video_recv_size;
	int		max_audio_recv_size;
	int		max_video_send_size;
	int		max_audio_send_size;
} miss_profile_t;

typedef struct miss_config_t {
	int							status;
	miss_profile_t				profile;
} miss_config_t;


/*
 * function
 */
int config_miss_read(miss_config_t*);
int config_miss_set(int module, void *arg);
int config_miss_get_config_status(int module);



#endif /* SERVER_MISS_CONFIG_H_ */
