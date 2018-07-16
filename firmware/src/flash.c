/*
 * flash.c
 *
 * Created: 03-04-2018 07:28:50 PM
 *  Author: abhra
 */ 
#include <asf.h>
#include "flash.h"
#include "wifi.h"

#define APP_START_ADDRESS       0x4000
#define AT25DFX_BUFFER_SIZE		(256)
#define AT25DFX_SECTOR_SIZE		0x10000
#define AT25DFX_STATUS_ADDR		0x0
#define AT25DFX_IMAGE1_HEADER	0x1000
#define AT25DFX_IMAGE2_HEADER	0x2000
#define AT25DFX_IMAGE1_ADDR		(1 * AT25DFX_SECTOR_SIZE)	/* Sector after header */
#define AT25DFX_IMAGE2_ADDR		AT25DFX_IMAGE1_ADDR + (4 * AT25DFX_SECTOR_SIZE)	/* 4 Sectors (256KB) after Image 1 */
#define NVM_STATUS_ADDRESS		0x3F00

static uint8_t read_buffer[AT25DFX_BUFFER_SIZE];
static uint8_t write_buffer[AT25DFX_BUFFER_SIZE];
static uint8_t flash_byte_ptr, flash_sector_ptr;
static uint16_t flash_page_ptr;
struct spi_master_vec_module at25dfx_spi;
struct at25dfx_chip_module at25dfx_chip;

/** File download processing state. */
extern download_state down_state;
/** Http content length. */
extern uint32_t http_file_size;
/** Receiving content length. */
extern uint32_t received_file_size;

extern now_downloading_t now_downloading;
static uint32_t now_writing;

image1_meta_t image1_metadata;
image2_meta_t image2_metadata;
flash_status_t flash_status;
nvm_status_t nvm_status;

/**
 * \brief Initialize at25dfx.
 */
static void at25dfx_init(void)
{
	struct at25dfx_chip_config at25dfx_chip_conf;
	struct spi_master_vec_config at25dfx_spi_config;
	at25dfx_spi_master_vec_get_config_defaults(&at25dfx_spi_config);
	at25dfx_spi_config.baudrate = 12000000;
	at25dfx_spi_config.mux_setting = SPI_SIGNAL_MUX_SETTING_E;
	at25dfx_spi_config.pinmux_pad0 = PINMUX_PA16C_SERCOM1_PAD0;
	at25dfx_spi_config.pinmux_pad1 = PINMUX_UNUSED;
	at25dfx_spi_config.pinmux_pad2 = PINMUX_PA18C_SERCOM1_PAD2;
	at25dfx_spi_config.pinmux_pad3 = PINMUX_PA19C_SERCOM1_PAD3;
	spi_master_vec_init(&at25dfx_spi, SERCOM1, &at25dfx_spi_config);
	spi_master_vec_enable(&at25dfx_spi);
	
	at25dfx_chip_conf.type = AT25DFX_081A;
	at25dfx_chip_conf.cs_pin = PIN_PA07;
	if(at25dfx_chip_init(&at25dfx_chip, &at25dfx_spi, &at25dfx_chip_conf) != STATUS_OK){
		printf("AT25DX init failed\r\n");
		while(1);
	}
}

/**
 * \brief Verify CRC32 for downloaded image.
 */
uint8_t verify_checksum(image_t image){
	uint32_t read_from = (image == IMAGE1) ? AT25DFX_IMAGE1_ADDR : AT25DFX_IMAGE2_ADDR;
	int blocks = http_file_size / AT25DFX_BUFFER_SIZE;
	int bytes = http_file_size % AT25DFX_BUFFER_SIZE;
	int blk;
	uint32_t checksum = 0;
	if(at25dfx_chip_wake(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX wake failed\r\n");
		while(1);
	}
	for(blk = 0; blk < blocks; blk++){
		if(at25dfx_chip_read_buffer(&at25dfx_chip, read_from + (blk * AT25DFX_BUFFER_SIZE), read_buffer, AT25DFX_BUFFER_SIZE) != STATUS_OK){
			printf("AT25DX read failed at page 0x%lx\r\n", read_from + (blk * AT25DFX_BUFFER_SIZE));
			while(1);
		}
		if(!blk)
		crc32_calculate(read_buffer, AT25DFX_BUFFER_SIZE, &checksum);
		else
		crc32_recalculate(read_buffer, AT25DFX_BUFFER_SIZE, &checksum);
	}
	if(at25dfx_chip_read_buffer(&at25dfx_chip, read_from + (blk * AT25DFX_BUFFER_SIZE), read_buffer, bytes) != STATUS_OK){
		printf("AT25DX read failed at page 0x%lx\r\n", read_from + (blk * AT25DFX_BUFFER_SIZE));
		while(1);
	}
	crc32_recalculate(read_buffer, bytes, &checksum);
	if(at25dfx_chip_sleep(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX sleep failed\r\n");
		while(1);
	}
	printf("New image Checksum: %lx\r\n", checksum);
	if(((image == IMAGE1) ? image1_metadata.checksum : image2_metadata.checksum) == checksum)
		return 1;
	else
		return 0;
}

/**
 * \brief Update metadata from flash.
 */
void update_metadata(image_t image){
	if(at25dfx_chip_wake(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX wake failed\r\n");
		while(1);
	}
	if(image == IMAGE1){
		if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_IMAGE1_HEADER, &image1_metadata, sizeof(image1_meta_t)) != STATUS_OK){
			printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_IMAGE1_HEADER);
			while(1);
		}
		printf("Current Image Version: %d, Size: %d, Checksum: %lx\r\n", image1_metadata.version, image1_metadata.size, image1_metadata.checksum);
	}
	else{
		if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_IMAGE2_HEADER, &image2_metadata, sizeof(image2_meta_t)) != STATUS_OK){
			printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_IMAGE2_HEADER);
			while(1);
		}
		printf("Current Image Version: %d, Size: %d, Checksum: %lx\r\n", image2_metadata.version, image2_metadata.size, image2_metadata.checksum);
	}
	if(at25dfx_chip_sleep(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX sleep failed\r\n");
		while(1);
	}
}

/**
 * \brief Update image status from flash.
 */
void update_image_status(void){
	if(at25dfx_chip_wake(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX wake failed\r\n");
		while(1);
	}
	if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_STATUS_ADDR, &flash_status, sizeof(flash_status)) != STATUS_OK){
		printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_STATUS_ADDR);
		while(1);
	}
	printf("Flash status: Image1: %s, Image2: %s\r\n", flash_status.image1_valid ? "valid" : "invalid", flash_status.image2_valid ? "valid" : "invalid");
	if(at25dfx_chip_sleep(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX sleep failed\r\n");
		while(1);
	}
}


/**
 * \brief Update metadata from flash.
 */
void write_image_state(image_valid_t validity){
	if(at25dfx_chip_wake(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX wake failed\r\n");
		while(1);
	}
	if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_STATUS_ADDR, false) != STATUS_OK){
		printf("AT25DX sector protect failed\r\n");
		while(1);
	}
	if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_STATUS_ADDR, &flash_status, sizeof(flash_status)) != STATUS_OK){
		printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_STATUS_ADDR);
		while(1);
	}
	switch(validity){
		case IMAGE1_VALID:
			flash_status.image1_valid = 1;
			break;
		case IMAGE1_INVALID:
			flash_status.image1_valid = 0;
			break;
		case IMAGE2_VALID:
			flash_status.image2_valid = 1;
			break;
		case IMAGE2_INVALID:
			flash_status.image2_valid = 0;
	}
	if(at25dfx_chip_erase_block(&at25dfx_chip, AT25DFX_STATUS_ADDR,  AT25DFX_BLOCK_SIZE_4KB) != STATUS_OK){
		printf("AT25DX sector erase failed at page 0x%x\r\n", AT25DFX_STATUS_ADDR);
		while(1);
	}
	if(at25dfx_chip_write_buffer(&at25dfx_chip, AT25DFX_STATUS_ADDR, &flash_status, sizeof(flash_status)) != STATUS_OK){
		printf("AT25DX write failed at page 0x%x\r\n", AT25DFX_STATUS_ADDR);
		while(1);
	}
	if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_STATUS_ADDR, true) != STATUS_OK){
		printf("AT25DX sector protect failed\r\n");
		while(1);
	}
	if(at25dfx_chip_sleep(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX sleep failed\r\n");
		while(1);
	}
}
/**
 * \brief Start flash write.
 */
static void start_write_flash(void){
	if(at25dfx_chip_wake(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX wake failed\r\n");
		while(1);
	}
	if (at25dfx_chip_check_presence(&at25dfx_chip) == STATUS_OK) {
		enum status_code status =STATUS_OK;
		if(now_downloading == IMAGE){
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR, false) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_erase_block(&at25dfx_chip, AT25DFX_IMAGE1_ADDR,  AT25DFX_BLOCK_SIZE_64KB) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (1 * AT25DFX_SECTOR_SIZE), false) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_erase_block(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (1 * AT25DFX_SECTOR_SIZE), AT25DFX_BLOCK_SIZE_64KB) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (2 * AT25DFX_SECTOR_SIZE), false) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_erase_block(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (2 * AT25DFX_SECTOR_SIZE), AT25DFX_BLOCK_SIZE_64KB) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (3 * AT25DFX_SECTOR_SIZE), false) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_erase_block(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (3 * AT25DFX_SECTOR_SIZE), AT25DFX_BLOCK_SIZE_64KB) != STATUS_OK)	status = STATUS_ERR_IO;
			if(status == STATUS_ERR_IO){
				printf("AT25DX sector erase failed\r\n");
				while(1);
			}
			now_writing = AT25DFX_IMAGE1_ADDR;
		}
		else if(now_downloading == METADATA){
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_STATUS_ADDR, false) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_erase_block(&at25dfx_chip, AT25DFX_IMAGE1_HEADER,  AT25DFX_BLOCK_SIZE_4KB) != STATUS_OK)	status = STATUS_ERR_IO;
			if(status == STATUS_ERR_IO){
				printf("AT25DX sector erase failed\r\n");
				while(1);
			}
			now_writing = AT25DFX_IMAGE1_HEADER;
		}
	}
	else{
		printf("AT25DX check presence failed\r\n");
		while(1);
	}
}

/**
 * \brief Finish flash write.
 */
static void finish_write_flash(void){
	if (at25dfx_chip_check_presence(&at25dfx_chip) == STATUS_OK) {
		enum status_code status =STATUS_OK;
		if(now_downloading == IMAGE){
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR, true) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (1 * AT25DFX_SECTOR_SIZE), true) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (2 * AT25DFX_SECTOR_SIZE), true) != STATUS_OK)	status = STATUS_ERR_IO;
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_IMAGE1_ADDR + (3 * AT25DFX_SECTOR_SIZE), true) != STATUS_OK)	status = STATUS_ERR_IO;
			if(status == STATUS_ERR_IO){
				printf("AT25DX sector erase failed\r\n");
				while(1);
			}
		}
		else if(now_downloading == METADATA){
			if(at25dfx_chip_set_sector_protect(&at25dfx_chip, AT25DFX_STATUS_ADDR, true) != STATUS_OK)	status = STATUS_ERR_IO;
			if(status == STATUS_ERR_IO){
				printf("AT25DX sector erase failed\r\n");
				while(1);
			}
		}
	}
	else{
		printf("AT25DX check presence failed\r\n");
		while(1);
	}
	if(at25dfx_chip_sleep(&at25dfx_chip) != STATUS_OK){
		printf("AT25DX sleep failed\r\n");
		while(1);
	}
}

/**
 * \brief Write data to flash.
 */
void save_data_to_flash(char *data, uint32_t length){
		// Flash storage
		if ((data == NULL) || (length < 1)) {
			printf("store_file_packet: empty data.\r\n");
			return;
		}

		if (!is_state_set(DOWNLOADING)) {
			memset((uint8_t *)&read_buffer, 0, AT25DFX_BUFFER_SIZE);
			memset((uint8_t *)&write_buffer, 0, AT25DFX_BUFFER_SIZE);
			flash_byte_ptr = 0;
			flash_page_ptr = 0;
			flash_sector_ptr = 0;
			start_write();
			received_file_size = 0;
			add_state(DOWNLOADING);
		}
		uint32_t bytes_to_write = (length + flash_byte_ptr), data_ptr = 0;
		if (data != NULL) {
			if (at25dfx_chip_check_presence(&at25dfx_chip) == STATUS_OK) {
				while(bytes_to_write != 0){
					if(bytes_to_write < AT25DFX_BUFFER_SIZE){
						memcpy(&write_buffer[flash_byte_ptr], (uint8_t *)&data[data_ptr], bytes_to_write);
						flash_byte_ptr += bytes_to_write;
						//printf("Added %lu bytes to buff\r\n", bytes_to_write);
						data_ptr += bytes_to_write;
						received_file_size += bytes_to_write;
						bytes_to_write = 0;
					}
					else{
						memcpy(&write_buffer[flash_byte_ptr], (uint8_t *)&data[data_ptr], (AT25DFX_BUFFER_SIZE - flash_byte_ptr));
						data_ptr += (AT25DFX_BUFFER_SIZE - flash_byte_ptr);
						received_file_size += (AT25DFX_BUFFER_SIZE - flash_byte_ptr);
						flash_byte_ptr = 0;
						bytes_to_write -= AT25DFX_BUFFER_SIZE;
						if(at25dfx_chip_write_buffer(&at25dfx_chip, now_writing + (flash_sector_ptr * AT25DFX_SECTOR_SIZE)
						+ (flash_page_ptr * AT25DFX_BUFFER_SIZE), write_buffer, AT25DFX_BUFFER_SIZE) != STATUS_OK){
							printf("AT25DX write failed at page 0x%lx\r\n", now_writing + (flash_sector_ptr * AT25DFX_SECTOR_SIZE)
							+ (flash_page_ptr * AT25DFX_BUFFER_SIZE));
							while(1);
						}
						//printf("AT25DX writen %u bytes at page 0x%lx\r\n",  AT25DFX_BUFFER_SIZE, now_writing + (flash_sector_ptr * AT25DFX_SECTOR_SIZE)
						//+ (flash_page_ptr * AT25DFX_BUFFER_SIZE));
						flash_page_ptr++;
						if((flash_page_ptr * AT25DFX_BUFFER_SIZE) >= AT25DFX_SECTOR_SIZE){
							flash_sector_ptr++;
							flash_page_ptr = 0;
						}
					}
					//printf("data_ptr: %u byte_ptr: %u page_ptr: %u sector_ptr: %u bytes: %lu\r\n", data_ptr, flash_byte_ptr, flash_page_ptr, flash_sector_ptr, bytes_to_write);
				}
			}
			else{
				printf("AT25DX check presence failed\r\n");
				while(1);
			}

			if (received_file_size >= http_file_size) {
				if(flash_byte_ptr){
					if(at25dfx_chip_write_buffer(&at25dfx_chip, now_writing + (flash_sector_ptr * AT25DFX_SECTOR_SIZE)
					+ (flash_page_ptr * AT25DFX_BUFFER_SIZE), write_buffer, flash_byte_ptr) != STATUS_OK){
						printf("AT25DX write failed at page 0x%lx\r\n", now_writing + (flash_sector_ptr * AT25DFX_SECTOR_SIZE)
						+ (flash_page_ptr * AT25DFX_BUFFER_SIZE));
						while(1);
					}
					//printf("AT25DX writen %u bytes at page 0x%lx\r\n", flash_byte_ptr, now_writing + (flash_sector_ptr * AT25DFX_SECTOR_SIZE)
					//+ (flash_page_ptr * AT25DFX_BUFFER_SIZE));
					//printf(" byte_ptr: %u page_ptr: %u sector_ptr: %u\r\n", flash_byte_ptr, flash_page_ptr, flash_sector_ptr);
				}
				printf("store_file_packet: chunk size[%lu], received[%lu], file size[%lu]\r\n", (unsigned long)length, (unsigned long)received_file_size, (unsigned long)http_file_size);
				//f_close(&file_object);
				printf("store_file_packet: %s file saved successfully.\r\n", (now_downloading == IMAGE) ? "Firmware" : "Metadata");
				//port_pin_set_output_level(LED_0_PIN, false);
				add_state(COMPLETED);
				finish_write_flash();
				return;
			}
			printf("store_file_packet: chunk size[%lu], received[%lu], file size[%lu]\r", (unsigned long)length, (unsigned long)received_file_size, (unsigned long)http_file_size);
		}
}

/**
 * \brief Initialize Flash storage.
 */
static void init_flash_storage(void)
{
	// Flash init
	at25dfx_init();
	memset((uint8_t *)&read_buffer, 0, AT25DFX_BUFFER_SIZE);
	memset((uint8_t *)&write_buffer, 0, AT25DFX_BUFFER_SIZE);
	memset((uint8_t *)&image1_metadata, 0, sizeof(image1_metadata));
	memset((uint8_t *)&image2_metadata, 0, sizeof(image2_metadata));
	memset((uint8_t *)&flash_status, 0, sizeof(flash_status));
	flash_byte_ptr = 0;
	flash_page_ptr = 0;
	flash_sector_ptr = 0;
	add_state(STORAGE_READY);
}

void init_storage(void){
	init_flash_storage();
}

void start_write(void){
	start_write_flash();
}

void finish_write(void){
	finish_write_flash();
}

void set_bootloader_flag(void){
	nvm_status.bootloader = 1;
	nvm_status.nvm_valid = 1;
	enum status_code error_code;
	do{
		error_code = nvm_erase_row(NVM_STATUS_ADDRESS);
	} while (error_code == STATUS_BUSY);
	do{
		error_code = nvm_write_buffer(NVM_STATUS_ADDRESS, (uint8_t *)&nvm_status, sizeof(nvm_status));
	} while (error_code == STATUS_BUSY);
	nvm_status.bootloader = 0;
	nvm_status.nvm_valid = 1;
}