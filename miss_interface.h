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
#define		MSG_MISS_BASE						(SERVER_MISS<<16)
#define		MSG_MISS_SERVER_START				MSG_MISS_BASE | 0x0001
#define		MSG_MISS_SERVER_STOP				MSG_MISS_BASE | 0x0002
#define		MSG_MISS_SERVER_ERROR				MSG_MISS_BASE | 0x0003
#define		MSG_MISS_SESSION_CONNECTED			MSG_MISS_BASE | 0x0004
#define		MSG_MISS_SESSION_DISCONNECTED		MSG_MISS_BASE | 0x0005
#define		MSG_MISS_SESSION_ERROR				MSG_MISS_BASE | 0x0006
#define		MSG_MISS_SERVER_VIDEO_START			MSG_MISS_BASE | 0x0010
#define		MSG_MISS_SERVER_VIDEO_STOP			MSG_MISS_BASE | 0x0020
#define		MSG_MISS_SERVER_VIDEO_CTRL			MSG_MISS_BASE | 0x0021
#define		MSG_MISS_SERVER_AUDIO_START			MSG_MISS_BASE | 0x0030
#define		MSG_MISS_SERVER_AUDIO_STOP			MSG_MISS_BASE | 0x0040

#define		MSG_MISS_SIGINT						MSG_MISS_BASE | 0x0101
#define		MSG_MISS_SIGTERM					MSG_MISS_BASE | 0x0102
#define		MSG_MISS_EXIT						MSG_MISS_BASE | 0X0110

#define		MSG_MISS_SNAPSHOT					MSG_MISS_BASE | 0x1000
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
