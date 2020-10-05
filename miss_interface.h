/*
 * miss_interface.h
 *
 *  Created on: Aug 28, 2020
 *      Author: ning
 */

#ifndef SERVER_MISS_MISS_INTERFACE_H_
#define SERVER_MISS_MISS_INTERFACE_H_

/*
 * header
 */
#include "../../manager/manager_interface.h"

/*
 * define
 */
#define		SERVER_MISS_VERSION_STRING			"alpha-3.1"

#define		MSG_MISS_BASE						(SERVER_MISS<<16)
#define		MSG_MISS_SIGINT						MSG_MISS_BASE | 0x0000
#define		MSG_MISS_SIGINT_ACK					MSG_MISS_BASE | 0x1000
#define		MSG_MISS_VIDEO_DATA					MSG_MISS_BASE | 0x0010
#define		MSG_MISS_AUDIO_DATA					MSG_MISS_BASE | 0x0011

/*
 * structure
 */

/*
 * function
 */
int server_miss_start(void);
int server_miss_message(message_t *msg);
int server_miss_video_message(message_t *msg);
int server_miss_audio_message(message_t *msg);

#endif /* SERVER_MISS_MISS_INTERFACE_H_ */
