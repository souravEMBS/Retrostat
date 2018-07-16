/**
 * \file
 *
 * \brief Empty user application template
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * This is a bare minimum user application template.
 *
 * For documentation of the board, go \ref group_common_boards "here" for a link
 * to the board-specific documentation.
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 * -# Minimal main function that starts with a call to system_init()
 * -# Basic usage of on-board LED and button
 * -# "Insert application code here" comment
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
/*
 * Support and FAQ: visit <a href="http://www.atmel.com/design-support/">Atmel Support</a>
 */
#include <asf.h>

#define APP_START_ADDRESS       0x4000
#define NVM_STATUS_ADDRESS		0x3F00
#define AT25DFX_BUFFER_SIZE		(256)
#define AT25DFX_SECTOR_SIZE		0x10000
#define AT25DFX_STATUS_ADDR		0x0
#define AT25DFX_IMAGE1_HEADER	0x1000
#define AT25DFX_IMAGE2_HEADER	0x2000
#define AT25DFX_IMAGE1_ADDR		(1 * AT25DFX_SECTOR_SIZE)	/* Sector after header */
#define AT25DFX_IMAGE2_ADDR		AT25DFX_IMAGE1_ADDR + (4 * AT25DFX_SECTOR_SIZE)	/* 4 Sectors (256KB) after Image 1 */

static uint8_t read_buffer[AT25DFX_BUFFER_SIZE];
struct spi_master_vec_module at25dfx_spi;
struct at25dfx_chip_module at25dfx_chip;
static struct usart_module cdc_uart_module;

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

image1_meta_t image1_metadata;
image2_meta_t image2_metadata;
flash_status_t flash_status;
nvm_status_t nvm_status;

static void init_uart(void){
	struct usart_config usart_conf;

	usart_get_config_defaults(&usart_conf);
	usart_conf.mux_setting = EDBG_CDC_SERCOM_MUX_SETTING;
	usart_conf.pinmux_pad0 = EDBG_CDC_SERCOM_PINMUX_PAD0;
	usart_conf.pinmux_pad1 = EDBG_CDC_SERCOM_PINMUX_PAD1;
	usart_conf.pinmux_pad2 = EDBG_CDC_SERCOM_PINMUX_PAD2;
	usart_conf.pinmux_pad3 = EDBG_CDC_SERCOM_PINMUX_PAD3;
	usart_conf.baudrate    = 115200;

	stdio_serial_init(&cdc_uart_module, EDBG_CDC_MODULE, &usart_conf);
	usart_enable(&cdc_uart_module);
}

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
		while(1);
	}
}

static void configure_nvm(void)
{
	struct nvm_config config_nvm;
	nvm_get_config_defaults(&config_nvm);
	config_nvm.manual_page_write = false;
	nvm_set_config(&config_nvm);
}

static void update_image_status(void){
	if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_STATUS_ADDR, &flash_status, sizeof(flash_status)) != STATUS_OK){
		printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_STATUS_ADDR);
		while(1);
	}
	printf("Flash status: Image1: %s, Image2: %s\r\n", flash_status.image1_valid ? "valid" : "invalid", flash_status.image2_valid ? "valid" : "invalid");
}

static void update_nvm_status(void){
	enum status_code error_code;
	do{
		error_code = nvm_read_buffer(NVM_STATUS_ADDRESS, (uint8_t *)&nvm_status, sizeof(nvm_status));
	} while (error_code == STATUS_BUSY);
}

static void update_metadata(void){
	if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_IMAGE1_HEADER, &image1_metadata, sizeof(image1_meta_t)) != STATUS_OK){
		printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_IMAGE1_HEADER);
		while(1);
	}
	printf("Image1 Version: %d, Size: %d, Checksum: %lx\r\n", image1_metadata.version, image1_metadata.size, image1_metadata.checksum);

	if(at25dfx_chip_read_buffer(&at25dfx_chip, AT25DFX_IMAGE2_HEADER, &image2_metadata, sizeof(image2_meta_t)) != STATUS_OK){
		printf("AT25DX read failed at page 0x%x\r\n", AT25DFX_IMAGE2_HEADER);
		while(1);
	}
	printf("Image2 Version: %d, Size: %d, Checksum: %lx\r\n", image2_metadata.version, image2_metadata.size, image2_metadata.checksum);

}

static void write_nvm(image_t image){
		printf("Preparing %s Flash to NVM write\r\n", ((image == IMAGE1) ? "Image1" : "Image2"));
		uint32_t img_size = ((image == IMAGE1) ? image1_metadata.size : image2_metadata.size);
		int flash_pages = img_size / AT25DFX_BUFFER_SIZE;
		int flash_extra_bytes = img_size % AT25DFX_BUFFER_SIZE;
		uint32_t img_base = ((image == IMAGE1) ? AT25DFX_IMAGE1_ADDR : AT25DFX_IMAGE2_ADDR);
		int nvm_rows = (img_size / ( NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE)) + ((img_size % ( NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE)) ? 1 : 0);
		int blk;
		enum status_code error_code;
		for(blk = 0; blk < nvm_rows; blk++){
			do{
				error_code = nvm_erase_row(APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE));
			} while (error_code == STATUS_BUSY);
			//printf("Erased row with address 0x%x\r", APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE));
		}
		printf("Done nvm erase\r\n");
		for(blk = 0; blk < flash_pages; blk++){
			if(at25dfx_chip_read_buffer(&at25dfx_chip, img_base + (blk * AT25DFX_BUFFER_SIZE), read_buffer, AT25DFX_BUFFER_SIZE) != STATUS_OK){
				printf("AT25DX read failed at page 0x%lx\r\n", img_base + (blk * AT25DFX_BUFFER_SIZE));
				while(1);
			}
			int nvm_pages;
			for(nvm_pages = 0; nvm_pages < (AT25DFX_BUFFER_SIZE / NVMCTRL_PAGE_SIZE); nvm_pages++){
				do{
					error_code = nvm_write_buffer(APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE)
					+ (nvm_pages * NVMCTRL_PAGE_SIZE), &read_buffer[nvm_pages * NVMCTRL_PAGE_SIZE], NVMCTRL_PAGE_SIZE);
				} while (error_code == STATUS_BUSY);
				//printf("Written %d bytes to page 0x%x\r", NVMCTRL_PAGE_SIZE, APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE) + (nvm_pages * NVMCTRL_PAGE_SIZE));
			}
		}
		if(flash_extra_bytes){
			if(at25dfx_chip_read_buffer(&at25dfx_chip, img_base + (blk * AT25DFX_BUFFER_SIZE), read_buffer, flash_extra_bytes) != STATUS_OK){
				printf("AT25DX read failed at page 0x%lx\r\n", img_base + (blk * AT25DFX_BUFFER_SIZE));
				while(1);
			}
			int nvm_pages;
			for(nvm_pages = 0; nvm_pages < (flash_extra_bytes / NVMCTRL_PAGE_SIZE); nvm_pages++){
				do{
					error_code = nvm_write_buffer(APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE)
					+ (nvm_pages * NVMCTRL_PAGE_SIZE), &read_buffer[nvm_pages * NVMCTRL_PAGE_SIZE], NVMCTRL_PAGE_SIZE);
				} while (error_code == STATUS_BUSY);
				//printf("Written %d bytes to page 0x%x\r", NVMCTRL_PAGE_SIZE, APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE) + (nvm_pages * NVMCTRL_PAGE_SIZE));
			}
			if(flash_extra_bytes % NVMCTRL_PAGE_SIZE){
				do{
					error_code = nvm_write_buffer(APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE)
					+ (nvm_pages * NVMCTRL_PAGE_SIZE), &read_buffer[nvm_pages * NVMCTRL_PAGE_SIZE], flash_extra_bytes % NVMCTRL_PAGE_SIZE);
				} while (error_code == STATUS_BUSY);
				//printf("Written %d bytes to page 0x%x\r", flash_extra_bytes % NVMCTRL_PAGE_SIZE, APP_START_ADDRESS + (blk * NVMCTRL_ROW_PAGES * NVMCTRL_PAGE_SIZE) + (nvm_pages * NVMCTRL_PAGE_SIZE));
			}
		}
		printf("Done nvm write\r\n");
}

static void write_nvm_status(void){
	enum status_code error_code;
	do{
		error_code = nvm_erase_row(NVM_STATUS_ADDRESS);
	} while (error_code == STATUS_BUSY);
	do{
		error_code = nvm_write_buffer(NVM_STATUS_ADDRESS, (uint8_t *)&nvm_status, sizeof(nvm_status));
	} while (error_code == STATUS_BUSY);
}

static uint8_t verify_checksum(image_t image){
	int pages = ((image == IMAGE1) ? image1_metadata.size : image2_metadata.size) / NVMCTRL_PAGE_SIZE;
	int extra_bytes = ((image == IMAGE1) ? image1_metadata.size : image2_metadata.size) % NVMCTRL_PAGE_SIZE;
	int blk;
	uint32_t checksum = 0;
	enum status_code error_code;
	for(blk = 0; blk < pages; blk++){
		do{
			error_code = nvm_read_buffer(APP_START_ADDRESS + (blk * NVMCTRL_PAGE_SIZE), read_buffer, NVMCTRL_PAGE_SIZE);
		} while (error_code == STATUS_BUSY);
		if(!blk)
		crc32_calculate(read_buffer, NVMCTRL_PAGE_SIZE, &checksum);
		else
		crc32_recalculate(read_buffer, NVMCTRL_PAGE_SIZE, &checksum);
	}
	do{
		error_code = nvm_read_buffer(APP_START_ADDRESS + (blk * NVMCTRL_PAGE_SIZE), read_buffer, extra_bytes);
	} while (error_code == STATUS_BUSY);
	crc32_recalculate(read_buffer, extra_bytes, &checksum);
	printf("New image Checksum: %lx\r\n", checksum);
	if(((image == IMAGE1) ? image1_metadata.checksum : image2_metadata.checksum) == checksum)
		return 1;
	else
		return 0;
}


static void jump_to_firmware(void){
	/* Pointer to the Application Section */
	void (*application_code_entry)(void);
	/* Rebase the Stack Pointer */
	__set_MSP(*(uint32_t *)APP_START_ADDRESS);
	/* Rebase the vector table base address TODO: use RAM */
	SCB->VTOR = ((uint32_t)APP_START_ADDRESS & SCB_VTOR_TBLOFF_Msk);
	/* Load the Reset Handler address of the application */
	application_code_entry = (void (*)(void))(unsigned *)(*(unsigned *)
	(APP_START_ADDRESS + 4));
	/* Jump to user Reset Handler in the application */
	application_code_entry();
}

int main (void)
{
	system_init();
	configure_nvm();
	struct port_config pin_conf;
	port_get_config_defaults(&pin_conf);
	pin_conf.direction  = PORT_PIN_DIR_OUTPUT;
	port_pin_set_config(PIN_PA05, &pin_conf);
	update_nvm_status();
	if (nvm_status.bootloader || !nvm_status.nvm_valid) {
		port_pin_set_output_level(PIN_PA05, true);
		at25dfx_init();
		init_uart();
		printf("Entered Bootloader mode\n\r");
		if(at25dfx_chip_wake(&at25dfx_chip) != STATUS_OK){
			while(1);
		}
		if (at25dfx_chip_check_presence(&at25dfx_chip) != STATUS_OK) {
			printf("Flash Chip not found\n\r");
			while(1);
		}
		update_image_status();
		update_metadata();
		if(flash_status.image1_valid){
			nvm_status.bootloader = 0;
			nvm_status.nvm_valid = 0;
			write_nvm_status();
			printf("Marked NVM image invalid\r\n");
			write_nvm(IMAGE1);
			if(verify_checksum(IMAGE1)){
				printf("Checksum verified\r\n");
				nvm_status.bootloader = 0;
				nvm_status.nvm_valid = 1;
				write_nvm_status();
				printf("Marked NVM image valid\r\n");
			}
			else{
				printf("Checksum verify failed. Trying Image2\r\n");
				if(flash_status.image2_valid){
					write_nvm(IMAGE2);
					if(verify_checksum(IMAGE2)){
						printf("Checksum verified\r\n");
						nvm_status.bootloader = 0;
						nvm_status.nvm_valid = 1;
						write_nvm_status();
						printf("Marked NVM image valid\r\n");
					}
					else{
						printf("Checksum verify failed.\r\n");
					}
				}
			}
		}
		
		
		printf("Exiting Bootloader mode\n\r");
		
		if(at25dfx_chip_sleep(&at25dfx_chip) != STATUS_OK){
			printf("AT25DX sleep failed\r\n");
			while(1);
		}
		usart_disable(&cdc_uart_module);
		port_pin_set_output_level(PIN_PA05, false);
			
		//while(1);
		// Jump to firmware
		jump_to_firmware();
	}
	else{
		// Jump to firmware
		jump_to_firmware();
	}
	while(1);
}
