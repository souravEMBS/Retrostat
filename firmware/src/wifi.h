/*
 * wifi.h
 *
 * Created: 03-04-2018 07:23:38 PM
 *  Author: abhra
 */ 


#ifndef WIFI_H_
#define WIFI_H_


typedef enum {
	NOT_READY = 0, /*!< Not ready. */
	STORAGE_READY = 0x01, /*!< Storage is ready. */
	WIFI_CONNECTED = 0x02, /*!< Wi-Fi is connected. */
	GET_REQUESTED = 0x04, /*!< GET request is sent. */
	DOWNLOADING = 0x08, /*!< Running to download. */
	COMPLETED = 0x10, /*!< Download completed. */
	CANCELED = 0x20 /*!< Download canceled. */
} download_state;

typedef enum{
	METADATA,
	IMAGE
} now_downloading_t;


void init_wifi(void);
void connect_wifi(void);
void download(now_downloading_t);
void add_state(download_state mask);
void clear_state(download_state mask);
bool is_state_set(download_state mask);
void check_and_update_firmware(void);

#endif /* WIFI_H_ */