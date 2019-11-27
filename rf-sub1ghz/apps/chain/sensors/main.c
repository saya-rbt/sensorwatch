/****************************************************************************
 * rf-sub1ghz/sensors/main.c
 *
 * BME280 sensors reading and displaying on Adafruit display screen
 *
 * Copyright 2019 sayabiws@gmail.com
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	If not, see <http://www.gnu.org/licenses/>.
 *
 *************************************************************************** */

#include "core/system.h"
#include "core/systick.h"
#include "core/pio.h"
#include "lib/stdio.h"
#include "drivers/serial.h"
#include "drivers/gpio.h"
#include "drivers/ssp.h"
#include "drivers/i2c.h"

#include "drivers/adc.h"
#include "extdrv/cc1101.h"
#include "extdrv/status_led.h"
#include "extdrv/bme280_humidity_sensor.h"
#include "extdrv/ssd130x_oled_driver.h"
#include "extdrv/ssd130x_oled_buffer.h"
#include "extdrv/veml6070_uv_sensor.h"
#include "extdrv/tsl256x_light_sensor.h"
#include "lib/font.h"


#define MODULE_VERSION   0x01
#define MODULE_NAME "sensorwatch - Sensors display"
#define MODULE_ADDRESS   0x1A // Arbitrary, just have it different from the receptor
#define RECEPTOR_ADDRESS 0x16 // Receptor address - arbitrary but different as well

#define SELECTED_FREQ FREQ_SEL_48MHz

// Error codes to be displayed on the screen
// Since we can't use minicom through UART0 since this is used to send message on rf,
// we display error codes on the last line of the screen instead so we know if
// something goes wrong and what.
//
// Misc errors go from 1-9
#define ERROR_FAULT_INFO      1
// Honestly now i wonder where this one will be displayed but let's keep it anyway
#define ERROR_DISPLAY_FAILURE 2 

// Sensors errors go from 10-29
#define ERROR_TSL265X_CONFIG  10
#define ERROR_TSL256X_READ    11
#define ERROR_BME280_CONFIG   20
#define ERROR_BME280_READ     21

// Communication errors go from 30-39
#define ERROR_CC1101_SEND     30
#define ERROR_CC1101_RECEIVE  31

// PART ID INFO
//
// Part ID 0x3640c02b found on line 26
// Part ID is 0x3640c02b
// UID: 0x0211f5f5 - 0x4e434314 - 0x00393536 - 0x54002249

uint16_t uv = 0, ir = 0, humidity = 0;
uint32_t pressure = 0, temp = 0, lux = 0;

static volatile uint32_t update_display = 0;

/***************************************************************************** */
/* Pins configuration */
/* pins blocks are passed to set_pins() for pins configuration.
 * Unused pin blocks can be removed safely with the corresponding set_pins() call
 * All pins blocks may be safelly merged in a single block for single set_pins() call..
 */
const struct pio_config common_pins[] = {
	/* UART 0 */
	{ LPC_UART0_RX_PIO_0_1,  LPC_IO_DIGITAL },
	{ LPC_UART0_TX_PIO_0_2,  LPC_IO_DIGITAL },
	/* I2C 0 */
	{ LPC_I2C0_SCL_PIO_0_10, (LPC_IO_DIGITAL | LPC_IO_OPEN_DRAIN_ENABLE) },
	{ LPC_I2C0_SDA_PIO_0_11, (LPC_IO_DIGITAL | LPC_IO_OPEN_DRAIN_ENABLE) },
	/* SPI */
	{ LPC_SSP0_SCLK_PIO_0_14, LPC_IO_DIGITAL },
	{ LPC_SSP0_MOSI_PIO_0_17, LPC_IO_DIGITAL },
	{ LPC_SSP0_MISO_PIO_0_16, LPC_IO_DIGITAL },
	/* ADC */
	{ LPC_ADC_AD0_PIO_0_30, LPC_IO_ANALOG },
	{ LPC_ADC_AD1_PIO_0_31, LPC_IO_ANALOG },
	{ LPC_ADC_AD2_PIO_1_0,  LPC_IO_ANALOG },
	ARRAY_LAST_PIO,
};

const struct pio cc1101_cs_pin = LPC_GPIO_0_15;
const struct pio cc1101_miso_pin = LPC_SSP0_MISO_PIO_0_16;
const struct pio cc1101_gdo0 = LPC_GPIO_0_6;
const struct pio cc1101_gdo2 = LPC_GPIO_0_7;

const struct pio temp_alert = LPC_GPIO_0_3;

const struct pio status_led_green = LPC_GPIO_0_28;
const struct pio status_led_red = LPC_GPIO_0_29;

const struct pio button = LPC_GPIO_0_12; // ISP button


/***************************************************************************** */
/* Basic system init and configuration */

void system_init()
{
	/* Stop the Watchdog */
	startup_watchdog_disable(); /* Do it right now, before it gets a chance to break in */
	system_set_default_power_state();
	clock_config(SELECTED_FREQ);
	set_pins(common_pins);
	gpio_on();
	status_led_config(&status_led_green, &status_led_red);

	/* System tick timer MUST be configured and running in order to use the sleeping
	 * functions */
	systick_timer_on(1); /* 1ms */
	systick_start();

	// // Clearing the LEDs
	gpio_clear(status_led_red);
	gpio_clear(status_led_green);
}


// since we use adafruit display to display errors we need to initialize it here

/**************************************************************************** */
/* Adafruit Oled Display */

#define DISPLAY_ADDR 0x7A
static uint8_t gddram[ 4 + GDDRAM_SIZE ];
struct oled_display display = {
	.bus_type = SSD130x_BUS_I2C,
	.address = DISPLAY_ADDR,
	.bus_num = I2C0,
	.charge_pump = SSD130x_INTERNAL_PUMP,
	.gpio_rst = LPC_GPIO_0_0,
	.video_mode = SSD130x_DISP_NORMAL,
	.contrast = 128,
	.scan_dir = SSD130x_SCAN_BOTTOM_TOP,
	.read_dir = SSD130x_RIGHT_TO_LEFT,
	.display_offset_dir = SSD130x_MOVE_TOP,
	.display_offset = 4,
	.gddram = gddram,
};

#define ROW(x) VERTICAL_REV(x)
DECLARE_FONT(font);

void display_char(uint8_t line, uint8_t col, uint8_t c)
{
	uint8_t tile = (c > FIRST_FONT_CHAR) ? (c - FIRST_FONT_CHAR) : 0;
	uint8_t* tile_data = (uint8_t*)(&font[tile]);
	ssd130x_buffer_set_tile(gddram, col, line, tile_data);
}

int display_line(uint8_t line, uint8_t col, char* text)
{
	int len = strlen((char*)text);
	int i = 0;

	for (i = 0; i < len; i++) {
		uint8_t tile = (text[i] > FIRST_FONT_CHAR) ? (text[i] - FIRST_FONT_CHAR) : 0;
		uint8_t* tile_data = (uint8_t*)(&font[tile]);
		ssd130x_buffer_set_tile(gddram, col++, line, tile_data);
		if (col >= (SSD130x_NB_COL / 8)) {
			col = 0;
			line++;
			if (line >= SSD130x_NB_PAGES) {
				return i;
			}
		}
	}
	return len;
}

/***************************************************************************** */
void periodic_display(uint32_t tick)
{
	update_display = 1;
}

/* Define our fault handler. This one is not mandatory, the dummy fault handler
 * will be used when it's not overridden here.
 * Note : The default one does a simple infinite loop. If the watchdog is deactivated
 * the system will hang.
 */

void fault_info(const char* name, uint32_t len)
{
	char data[20];
	snprintf(data, 20, "ERROR: %d - %c", ERROR_FAULT_INFO, name);
	display_line(7, 0, data);
	gpio_clear(status_led_green);
	gpio_set(status_led_red);
	while (1);
}

/***************************************************************************** */
/* Luminosity */

/* Note : These are 8bits address */
#define TSL256x_ADDR	 0x52 /* Pin Addr Sel (pin2 of tsl256x) connected to GND */
struct tsl256x_sensor_config tsl256x_sensor = {
	.bus_num = I2C0,
	.addr = TSL256x_ADDR,
	.gain = TSL256x_LOW_GAIN,
	.integration_time = TSL256x_INTEGRATION_100ms,
	.package = TSL256x_PACKAGE_T,
};

void lux_config(int uart_num)
{
	int ret = 0;
	ret = tsl256x_configure(&tsl256x_sensor);
	if (ret != 0) {
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_TSL265X_CONFIG, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
}

void lux_display(int uart_num, uint16_t* ir, uint32_t* lux)
{
	uint16_t comb = 0;
	int ret = 0;

	ret = tsl256x_sensor_read(&tsl256x_sensor, &comb, ir, lux);
	if (ret != 0) {
		// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
		// uprintf(uart_num, "Lux read error: %d\n\r", ret);
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_TSL256X_READ, ret);
		display_line(7, 0, data);
		// gpio_clear(status_led_orange);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	} else {
		// If now everything works fine (e.g. we fix the problem)
		gpio_clear(status_led_red);
		gpio_set(status_led_green);
		// Reset the screen error line
		char data[20];
		snprintf(data, 20, "                ");
		display_line(7, 0, data);
	}
}

/***************************************************************************** */
/* BME280 Sensor */

/* Note : 8bits address */
#define BME280_ADDR 0xEC
struct bme280_sensor_config bme280_sensor = 
{
	.bus_num = I2C0,
	.addr = BME280_ADDR,
	.humidity_oversampling = BME280_OS_x16,
	.temp_oversampling = BME280_OS_x16,
	.pressure_oversampling = BME280_OS_x16,
	.mode = BME280_NORMAL,
	.standby_len = BME280_SB_62ms,
	.filter_coeff = BME280_FILT_OFF,
};

void bme_config(int uart_num)
{
	int ret = 0;
	ret = bme280_configure(&bme280_sensor);
	if (ret != 0)
	{
		// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
		// uprintf(uart_num, "Sensor config error: %d\n\r", ret);
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_BME280_CONFIG, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
	else
	{
		// If everything works fine
		gpio_clear(status_led_red);
		gpio_set(status_led_green);
		// Reset the screen error line
		char data[20];
		snprintf(data, 20, "                ");
		display_line(7, 0, data);
	}
}

/* BME will obtain temperature, pressure and humidity values */
void bme_display(int uart_num, uint32_t* pressure, uint32_t* temp, uint16_t* humidity)
{
	int ret = 0;
	ret = bme280_sensor_read(&bme280_sensor, pressure, temp, humidity);
	if(ret != 0)
	{
		// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
		// uprintf(uart_num, "Sensor read error: %d\n\r", ret);
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_BME280_READ, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
	else
	{
		int comp_temp = 0;
		uint32_t comp_pressure = 0;
		uint32_t comp_humidity = 0;
		comp_temp = bme280_compensate_temperature(&bme280_sensor, *temp)/10;
		comp_pressure = bme280_compensate_pressure(&bme280_sensor, *pressure)/100;
		comp_humidity = bme280_compensate_humidity(&bme280_sensor, *humidity)/10;
		*temp = comp_temp;
		*pressure = comp_pressure;
		*humidity = comp_humidity;

		// If now everything works fine (e.g. we fix the problem)
		gpio_clear(status_led_red);
		gpio_set(status_led_green);
		// Reset the screen error line
		char data[20];
		snprintf(data, 20, "                ");
		display_line(7, 0, data);
	}
}


/***************************************************************************** */
/* Communication over USB */
// uint8_t text_received = 0;
// #define NB_CHARS  15
// char inbuff[NB_CHARS + 1];
// void data_rx(uint8_t c)
// {
// 	static int idx = 0;
// 	if ((c != 0) && (c != '\n') && (c != '\r')) {
// 		inbuff[idx++] = c;
// 		if (idx >= NB_CHARS) {
// 			inbuff[NB_CHARS] = 0;
// 			idx = 0;
// 			text_received = 1;
// 		}
// 	} else {
// 		if (idx != 0) {
// 			inbuff[idx] = 0;
// 			text_received = 1;
// 		}
// 		idx = 0;
// 	}
// }

/******************************************************************************/
/* RF Communication */
#define RF_BUFF_LEN	64

static volatile int check_rx = 0;
void rf_rx_calback(uint32_t gpio)
{
	check_rx = 1;
}

static uint8_t rf_specific_settings[] = {
	CC1101_REGS(gdo_config[2]), 0x07, /* GDO_0 - Assert on CRC OK | Disable temp sensor */
	CC1101_REGS(gdo_config[0]), 0x2E, /* GDO_2 - FIXME : do something usefull with it for tests */
	CC1101_REGS(pkt_ctrl[0]), 0x0F, /* Accept all sync, CRC err auto flush, Append, Addr check and Bcast */
#if (RF_915MHz == 1)
	/* FIXME : Add here a define protected list of settings for 915MHz configuration */
#endif
};

/* RF config */
void rf_config(void)
{
	config_gpio(&cc1101_gdo0, LPC_IO_MODE_PULL_UP, GPIO_DIR_IN, 0);
	cc1101_init(0, &cc1101_cs_pin, &cc1101_miso_pin); /* ssp_num, cs_pin, miso_pin */
	/* Set default config */
	cc1101_config();
	/* And change application specific settings */
	cc1101_update_config(rf_specific_settings, sizeof(rf_specific_settings));
	set_gpio_callback(rf_rx_calback, &cc1101_gdo0, EDGE_RISING);
	cc1101_set_address(MODULE_ADDRESS);

// #ifdef DEBUG
// 	// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
// 	uprintf(UART0, "CC1101 RF link init done.\n\r");
// #endif
}

static volatile uint8_t tmppos = 0;
static volatile uint8_t luxpos = 1;
static volatile uint8_t hmdpos = 2;

typedef struct vpayload_t
{
	char checksum;
	uint32_t tmp;
	uint16_t hmd;
	uint32_t lux;
} vpayload_t;

static volatile vpayload_t cc_tx_vpayload;

void handle_rf_rx_data(void)
{
	uint8_t data[RF_BUFF_LEN];
	int8_t ret = 0;
	uint8_t status = 0;

	/* Check for received packet (and get it if any) */
	ret = cc1101_receive_packet(data, RF_BUFF_LEN, &status);
	if(ret < 0)
	{
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_CC1101_RECEIVE, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
	/* Go back to RX mode */
	cc1101_enter_rx_mode();

// #ifdef DEBUG
// 	// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
// 	uprintf(UART0, "RF: ret:%d, st: %d.\n\r", ret, status);
// #endif

	char checksumByte[8];
	snprintf(checksumByte, sizeof(checksumByte), "%c%c%c%c%c%c%c%c",
		(data[3]&0x80)?'1':'0',
		(data[3]&0x40)?'1':'0',
		(data[3]&0x20)?'1':'0',
		(data[3]&0x10)?'1':'0',
		(data[3]&0x08)?'1':'0',
		(data[3]&0x04)?'1':'0',
		(data[3]&0x02)?'1':'0',
		(data[3]&0x01)?'1':'0'
	);

	// If the packet has the correct header and all
	// letters are different (ie. we don't ask for the same value twice),
	// we handle the packet
	if((checksumByte[0] == 0) && (checksumByte[1] == 1) && 
		(
			(data[4] != data[5]) &&
			(data[5] != data[6]) &&
			(data[6] != data[4])
		)
	)
	{
		int j = 2;
		for(int i = 4; i <= 6; i++)
		{
			switch(data[i])
			{
				case 'T':
					if((checksumByte[j] == 0) && (checksumByte[j+1] == 0))
					{
						tmppos = i-4;
						break;
					}
					else return;
				case 'L':
					if((checksumByte[j] == 0) && (checksumByte[j+1] == 1))
					{
						luxpos = i-4;
						break;
					}
					else return;
				case 'H':
					if((checksumByte[j] == 1) && (checksumByte[j+1] == 0))
					{
						hmdpos = i-4;
						break;
					}
					else return;
				default:
					return;
			}
			j = j + 2;
		}

		// switch(data[4])
		// {
		// 	case 'T':
		// 		if((checksum[2] == 0) && (checksum[3] == 0))
		// 		{
		// 			tmppos = 0;
		// 			break;
		// 		}
		// 		else return;
		// 	case 'L':
		// 		if((checksum[2] == 0) && (checksum[3] == 1))
		// 		{
		// 			luxpos = 0;
		// 			break;
		// 		}
		// 		else return;
		// 	case 'H':
		// 		if((checksum[2] == 1) && (checksum[3] == 0))
		// 		{
		// 			hmdpos = 0;
		// 			break;
		// 		}
		// 		else return;
		// 	default:
		// 		return;
		// }

		// switch(data[5])
		// {
		// 	case 'T':
		// 		if((checksum[4] == 0) && (checksum[5] == 0))
		// 		{
		// 			tmppos = 1;
		// 			break;
		// 		}
		// 		else return;
		// 	case 'L':
		// 		if((checksum[4] == 0) && (checksum[5] == 1))
		// 		{
		// 			luxpos = 1;
		// 			break;
		// 		}
		// 		else return;
		// 	case 'H':
		// 		if((checksum[4] == 1) && (checksum[5] == 0))
		// 		{
		// 			hmdpos = 1;
		// 			break;
		// 		}
		// 		else return;
		// 	default:
		// 		return;
		// }

		// switch(data[6])
		// {
		// 	case 'T':
		// 		if((checksum[6] == 0) && (checksum[7] == 0))
		// 		{
		// 			tmppos = 2;
		// 			break;
		// 		}
		// 		else return;
		// 	case 'L':
		// 		if((checksum[6] == 0) && (checksum[7] == 1))
		// 		{
		// 			luxpos = 2;
		// 			break;
		// 		}
		// 		else return;
		// 	case 'H':
		// 		if((checksum[6] == 1) && (checksum[7] == 0))
		// 		{
		// 			hmdpos = 2;
		// 			break;
		// 		}
		// 		else return;
		// 	default:
		// 		return;
		// }
	}
	// We don't change the order if the packet is invalid
	else return;
}


/* Data sent on radio comes from the UART, put any data received from UART in
 * cc_tx_buff and send when either '\r' or '\n' is received.
 * This function is very simple and data received between cc_tx flag set and
 * cc_ptr rewind to 0 may be lost. */
static volatile uint32_t cc_tx = 0;
static volatile uint8_t cc_tx_buff[RF_BUFF_LEN];
static volatile uint8_t cc_ptr = 0;
static volatile unsigned char cc_checksum = 0;
void handle_uart_cmd(uint8_t c)
{
	// TODO IN THE RECEPTOR: since we will recieve changeFormat()
	// through the RPi (USB, so UART0), we need to handle it differently.
	// An example is if(c == "T" || c == "L" || c == "H") we call a different
	// function, that will send it to the sensors microcontroller,
	// otherwise it will just send it to the Pi and return.
	// This is not for this code but otherwise i'll forget about it.

// #ifdef DEBUG
// 	// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
	// char data[20];
	// snprintf(data, 20, "ERROR: f");
	// display_line(7, 0, data);
	// uprintf(UART0, "Received command : %c, buffer size: %d.\n\r",c,cc_ptr);
// #endif
	// Source address
	// cc_tx_buff[0] = MODULE_ADDRESS;

	// Data
	// Most of the data is handled in the main loop, so we just use it as it is
	// passed here
	// cc_tx_buff[1] = cc_checksum;
	if (cc_ptr < RF_BUFF_LEN) {
		cc_tx_buff[cc_ptr++] = c;
	} else {
		// Reset the pointer
		cc_ptr = 0;
	}
	if ((c == '\n') || (c == '\r')) {
		cc_ptr = 0;
		cc_tx = 1;
	}
}

void send_on_rf(void)
{
	uint8_t cc_tx_data[RF_BUFF_LEN + 2];
	uint8_t tx_len = sizeof(vpayload_t);
	int ret = 0;

	/* Create a local copy */
	vpayload_t vpayload;
	vpayload.checksum = cc_tx_vpayload.checksum;
	vpayload.tmp = cc_tx_vpayload.tmp;
	vpayload.lux = cc_tx_vpayload.lux;
	vpayload.hmd = cc_tx_vpayload.hmd;
	memcpy((char*)&(cc_tx_data[2]), &vpayload, sizeof(vpayload_t));
	/* "Free" the rx buffer as soon as possible */
	cc_ptr = 0;
	/* Prepare buffer for sending */
	cc_tx_data[0] = tx_len + 1;
	cc_tx_data[1] = RECEPTOR_ADDRESS; // Change it for different receptors
	/* Send */
	if (cc1101_tx_fifo_state() != 0) {
		cc1101_flush_tx_fifo();
	}
	// uprintf(UART0, "%x %x %x %x\n\r", cc_tx_data[0], cc_tx_data[1], cc_tx_data[2], cc_tx_data[3]);
	ret = cc1101_send_packet(cc_tx_data, (tx_len + 2));
	if(ret < 0)
	{
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_CC1101_SEND, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
	else
	{
		uprintf(UART0, "connard");
	}

// #ifdef DEBUG
// 	// TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
	// uprintf(UART0, "Tx ret: %d\n\r", ret);
// #endif
}

/**************************************************************************** */
int main(void)
{
	int ret = 0;
	system_init();
	uart_on(UART0, 115200, handle_uart_cmd);
	i2c_on(I2C0, I2C_CLK_100KHz, I2C_MASTER);
	ssp_master_on(0, LPC_SSP_FRAME_SPI, 8, 4*1000*1000); /* bus_num, frame_type, data_width, rate */

	/* Sensors config */
	bme_config(UART0);
	lux_config(UART0);

	/* Radio */
	rf_config();

	/* Configure and start display */
	ret = ssd130x_display_on(&display);
	/* Clear screen */
	ssd130x_buffer_set(gddram, 0x00);
	ret = ssd130x_display_full_screen(&display);


	/* Flag to set to 1 when we go above 1000 lx */
	int biglux = 0;

	/* Add periodic handler */
	add_systick_callback(periodic_display, 250);

	// When everything is up and running, we use the green LED
	gpio_set(status_led_green);

	while (1)
	{
		uint8_t status = 0;

		/* Alive notice*/
		// chenillard(250);

		/* Read the sensors */
		bme_display(UART0, &pressure, &temp, &humidity);
		lux_display(UART0, &ir, &lux);

		/* Display */
		if (update_display == 1)
		{
			char data[20];

			/* Update display */
			// If the luminosity is too big, we display it using kilolux
			if(lux > 1000)
				biglux = 1;
			else
				biglux = 0;
			snprintf(data, 20, "TMP: %d.%d	dC", temp/10, temp%10);
			display_line(tmppos, 0, data);
			if(biglux)
				snprintf(data, 20, "LUX: %d.0 klx", (lux/1000)/*, (lux/1000)%10*/);
			else
				snprintf(data, 20, "LUX: %d.0 lx	 ", lux/*, lux%10*/);
			display_line(luxpos, 0, data);
			snprintf(data, 20, "HMD: %d.%d	rH", humidity/10, humidity%10);
			display_line(hmdpos, 0, data);

			// Send to screen 
			ret = ssd130x_display_full_screen(&display);
			if(ret < 0)
			{
				// uprintf(UART0, "Display update error: %d\n\r", ret);
				char data[20];
				snprintf(data, 20, "ERROR: %d - %d", ERROR_DISPLAY_FAILURE, ret);
				display_line(7, 0, data);
				// gpio_clear(status_led_orange);
				gpio_clear(status_led_green);
				gpio_set(status_led_red);
			}
			else
			{
				// If now everything works fine (e.g. we fix the problem)
				gpio_clear(status_led_red);
				gpio_set(status_led_green);
				// Reset the screen error line
				char data[20];
				snprintf(data, 20, "                ");
				display_line(7, 0, data);
			}
			update_display = 0;
		}

		// We forge the 4th byte of our header here as it is easier to handle
		// than in handle_uart_cmd
		//
		// The first 2 bits are for the type of message: 00 for values, 01 for 
		// format change request, 10 and 11 for keys exchange (not available)
		// Since the sensors will only send values, it is always 00.
		// We have 2 bits so that we can add different types in the future if
		// we want to make our protocol evolve :)
		char checksumByte[8];
		checksumByte[0] = '0';
		checksumByte[1] = '0';

		// For parity bit and division checksum calculation
		// This is super long and there's probably an easier way to do it,
		// but I don't really have the time by now, unfortunately
		//
		// Temperature
		if((temp/10)%2 == 0)
		{
			checksumByte[2] = '0';
			checksumByte[3] = '1';
		}
		else if((temp/10)%3 == 0)
		{
			checksumByte[2] = '1';
			checksumByte[3] = '0';
		}
		else if((temp/10)%5 == 0)
		{
			checksumByte[2] = '1';
			checksumByte[3] = '1';
		}
		else
		{
			checksumByte[2] = '0';
			checksumByte[3] = '0';
		}

		// Lux
		if(lux%2 == 0)
		{
			checksumByte[4] = '0';
			checksumByte[5] = '1';
		}
		else if(lux%3 == 0)
		{
			checksumByte[4] = '1';
			checksumByte[5] = '0';
		}
		else if(lux%5 == 0)
		{
			checksumByte[4] = '1';
			checksumByte[5] = '1';
		}
		else
		{
			checksumByte[4] = '0';
			checksumByte[5] = '0';
		}

		// Humidity
		if((humidity/10)%2 == 0)
		{
			checksumByte[6] = '0';
			checksumByte[7] = '1';
		}
		else if((humidity/10)%3 == 0)
		{
			checksumByte[6] = '1';
			checksumByte[7] = '0';
		}
		else if((humidity/10)%5 == 0)
		{
			checksumByte[6] = '1';
			checksumByte[7] = '1';
		}
		else
		{
			checksumByte[6] = '0';
			checksumByte[7] = '0';
		}

		// Good, now we have our char containing our byte in string form.
		// Let's convert it to a proper 8 bit unsigned char.

		// BIT OPERATIONS MAGIC
		cc_checksum = 0;
		for (int i = 0; i < 8; ++i )
			cc_checksum |= (checksumByte[i] == '1') << (7 - i);

		/* RF */
		// Buffering data into the UART0 before sending it
		// It will then be handled by handle_uart_cmd
		// char payload[25];
		// snprintf(payload, 25, "%c%c%d.%d;%d.0;%d.%d",
		// 	MODULE_ADDRESS, cc_checksum,
		// 	temp/10, temp%10,
		// 	lux,
		// 	humidity/10, humidity%10);

		cc_tx_vpayload.checksum = cc_checksum;
		cc_tx_vpayload.tmp = temp;
		cc_tx_vpayload.lux = lux;
		cc_tx_vpayload.hmd = humidity;

		cc_tx = 1;

		if (cc_tx == 1) 
		{
			send_on_rf();
			cc_tx = 0;
		}

		/* Do not leave radio in an unknown or unwated state */
		do
		{
			status = (cc1101_read_status() & CC1101_STATE_MASK);
		} while (status == CC1101_STATE_TX);

		if (status != CC1101_STATE_RX) {
			static uint8_t loop = 0;
			loop++;
			if (loop > 10)
			{
				if (cc1101_rx_fifo_state() != 0)
				{
					cc1101_flush_rx_fifo();
				}
				cc1101_enter_rx_mode();
				loop = 0;
			}
		}

		if (check_rx == 1)
		{
			check_rx = 0;
			handle_rf_rx_data();
		}
		msleep(1000);
	}
	return 0;
}