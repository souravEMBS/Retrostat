/*
 * flash.h
 *
 * Created: 03-04-2018 07:30:05 PM
 *  Author: abhra
 */ 


#ifndef FLASH_H_
#define FLASH_H_

void init_storage(void);
void start_write(void);
void finish_write(void);
void save_data_to_flash(char *data, uint32_t length);
void update_image_status(void);
void set_bootloader_flag(void);

typedef struct nvm_status_header{
	uint8_t bootloader;
	uint8_t nvm_valid;
} nvm_status_t;

typedef struct flash_status_header{
	uint8_t image1_valid;
	uint8_t image2_valid;
} flash_status_t;

typedef struct image1_meta{
	uint16_t version;
	uint16_t size;
	uint32_t checksum;
} image1_meta_t;

typedef struct image2_meta{
	uint16_t version;
	uint16_t size;
	uint32_t checksum;
} image2_meta_t;

typedef enum{
	IMAGE1,
	IMAGE2
}image_t;

typedef enum{
	IMAGE1_VALID,
	IMAGE1_INVALID,
	IMAGE2_VALID,
	IMAGE2_INVALID
}image_valid_t;

void update_metadata(image_t image);
uint8_t verify_checksum(image_t image);
void write_image_state(image_valid_t validity);

#endif /* FLASH_H_ */