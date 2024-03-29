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

// Fair credit where it's due: most of the code come from the examples from
// Nathaël Pajani, I merely adapted them to fit the assignment that was given
// to me.
// I'm still proud of what I did, though :)

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
// The address is arbitrary, it just has to be different from the sensors.
// Our microcontroller had the number 26, which is 1A in hexadecimal, so that's why.
#define MODULE_ADDRESS   0x1A 
// Receptor address - arbitrary but different as well
#define RECEPTOR_ADDRESS 0x16 

#define SELECTED_FREQ FREQ_SEL_48MHz

// Error codes to be displayed on the screen
// Since we can't use minicom through UART0 since this is used to send message on rf,
// we display error codes on the last line of the screen instead so we know if
// something goes wrong and what.
//
// NOTE: UART can be used to display debug messages with the DEBUG flag.
//
// Misc errors go from 1-9
#define ERROR_FAULT_INFO	  1
// Honestly now i wonder where this one will be displayed but let's keep it anyway
#define ERROR_DISPLAY_FAILURE 2 

// Sensors errors go from 10-29
#define ERROR_TSL265X_CONFIG  10
#define ERROR_TSL256X_READ	11
#define ERROR_BME280_CONFIG   20
#define ERROR_BME280_READ	 21

// Communication errors go from 30-39
#define ERROR_CC1101_SEND	 30
#define ERROR_CC1101_RECEIVE  31

// PART ID INFO
// This has been gotten from lpcprog.
// The idea initially was to use UID as a salt for an encryption key,
// but due to the lack of time, encryption couldn't be implemented in the end.
//
// Part ID 0x3640c02b found on line 26
// Part ID is 0x3640c02b
// UID: 0x0211f5f5 - 0x4e434314 - 0x00393536 - 0x54002249

// Initialize the sensors values
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

// We're going to be using the green and red LEDs to warn about an error
// as well as the screen
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


// Since we use the Adafruit screen to display errors we need to initialize it here

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

// Displays a char, mostly useless by itself, but can be used
void display_char(uint8_t line, uint8_t col, uint8_t c)
{
	uint8_t tile = (c > FIRST_FONT_CHAR) ? (c - FIRST_FONT_CHAR) : 0;
	uint8_t* tile_data = (uint8_t*)(&font[tile]);
	ssd130x_buffer_set_tile(gddram, col, line, tile_data);
}

// Function we'll use to display a line on the Adafruit screen, most od the time
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

// Luminosity sensor setup
void lux_config(int uart_num)
{
	int ret = 0;
	ret = tsl256x_configure(&tsl256x_sensor);
	if (ret != 0) {
		// If the sensor couldn't be initialized, we warn the user
		// by displaying a message and lighting the red led up
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_TSL265X_CONFIG, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
}

// Reading the luminosity sensor
void lux_display(int uart_num, uint16_t* ir, uint32_t* lux)
{
	uint16_t comb = 0;
	int ret = 0;

	ret = tsl256x_sensor_read(&tsl256x_sensor, &comb, ir, lux);
	if (ret != 0)
	{
		// Error display again
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_TSL256X_READ, ret);
		display_line(7, 0, data);
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
		snprintf(data, 20, "				");
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

// Humidity and temperature sensors setup
void bme_config(int uart_num)
{
	int ret = 0;
	ret = bme280_configure(&bme280_sensor);
	if (ret != 0)
	{
		// Error display
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
		snprintf(data, 20, "				");
		display_line(7, 0, data);
	}
}

/* BME will obtain temperature, pressure and humidity values */
// Pressure isn't used in our application, but it might be in the future.
void bme_display(int uart_num, uint32_t* pressure, uint32_t* temp, uint16_t* humidity)
{
	int ret = 0;
	ret = bme280_sensor_read(&bme280_sensor, pressure, temp, humidity);
	if(ret != 0)
	{
		// Error display
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_BME280_READ, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
	else
	{
		// Values compensation
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
		snprintf(data, 20, "				");
		display_line(7, 0, data);
	}
}


/******************************************************************************/
/* RF Communication */
#define RF_BUFF_LEN	64

// Flag set when we receive data from the rf we have to handle
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

#ifdef DEBUG
	uprintf(UART0, "CC1101 RF link init done.\n\r");
#endif
}

// These three variables will be used to remember the order in which temperature,
// humidity and luminosity will be displayed on the screen.
static volatile uint8_t tmppos = 0;
static volatile uint8_t luxpos = 1;
static volatile uint8_t hmdpos = 2;

/* Payload types
 *
 * In order to send and receive data, whether it is values from the sensors
 * or an order change request to the sensors' display, we encapsulate it
 * in a struct to make it easier to handle.
 *
 */

// Packets containing values gathered from the sensors go here to be sent to the
// receptor (gateway to the Pi)
typedef struct vpayload_t
{
	char source;
	char checksum;
	uint32_t tmp;
	uint16_t hmd;
	uint32_t lux;
} vpayload_t;

// Packets gathered from rf containing the values' order in which to display them
typedef struct opayload_t
{
	char source;
	char first;
	char second;
	char third;
} opayload_t;

// This will be used to store data from the sensors before sending it through rf
static volatile vpayload_t cc_tx_vpayload;

// Function called when data comes from the radio
void handle_rf_rx_data(void)
{
	uint8_t data[RF_BUFF_LEN];
	int8_t ret = 0;
	uint8_t status = 0;

	/* Check for received packet (and get it if any) */
	ret = cc1101_receive_packet(data, RF_BUFF_LEN, &status);
	if(ret < 0)
	{
		// Error display
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_CC1101_RECEIVE, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}
	/* Go back to RX mode */
	cc1101_enter_rx_mode();

#ifdef DEBUG
	uprintf(UART0, "RF: ret:%d, st: %d.\n\r", ret, status);
#endif

	// Storing the order locally so we don't mess up with volatile variables
	opayload_t rec_order_payload;

	// Address verification
	if(data[1] == MODULE_ADDRESS)
	{
		// We use the led to signal we're handling the data.
        // However, it barely blinks so it's barely noticeable, but still.
		gpio_clear(status_led_green);
		gpio_set(status_led_red);

		// Copying the received packet in our struct so we can handle it better
		memcpy(&rec_order_payload, &data[2], sizeof(opayload_t));
		
		// If someone asks for the same value twice, we refuse.
		// The user doesn't make the rules, we do. :)
		if((rec_order_payload.first != rec_order_payload.second) &&
			(rec_order_payload.second != rec_order_payload.third) &&
			(rec_order_payload.third != rec_order_payload.first)
		)
		{
			// So this is really ugly, but it's the best way I found.
			switch(rec_order_payload.first)
			{
				case 'T':
					tmppos = 0;
					break;
				case 'L':
					luxpos = 0;
					break;
				case 'H':
					hmdpos = 0;
					break;
			}

			switch(rec_order_payload.second)
			{
				case 'T':
					tmppos = 1;
					break;
				case 'L':
					luxpos = 1;
					break;
				case 'H':
					hmdpos = 1;
					break;
			}

			switch(rec_order_payload.third)
			{
				case 'T':
					tmppos = 2;
					break;
				case 'L':
					luxpos = 2;
					break;
				case 'H':
					hmdpos = 2;
					break;
			}
		}
		// We don't change the order if the packet is invalid
		else return;
		gpio_clear(status_led_red);
		gpio_set(status_led_green);
	}
}


/* Data sent on radio comes from the UART, put any data received from UART in
 * cc_tx_buff and send when either '\r' or '\n' is received.
 * This function is very simple and data received between cc_tx flag set and
 * cc_ptr rewind to 0 may be lost. */
static volatile uint32_t cc_tx = 0;
static volatile uint8_t cc_tx_buff[RF_BUFF_LEN];
static volatile uint8_t cc_ptr = 0;
static volatile unsigned char cc_checksum = 0;

// Deprecated, since the microcontroller doesn't receive data from the UART anymore
void handle_uart_cmd(uint8_t c)
{
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

// Sending data on the radio
void send_on_rf(void)
{
	uint8_t cc_tx_data[RF_BUFF_LEN + 2];
	uint8_t tx_len = sizeof(vpayload_t);
	int ret = 0;

	/* Create a local copy */
	vpayload_t vpayload;
	vpayload.source = cc_tx_vpayload.source;
	vpayload.checksum = cc_tx_vpayload.checksum;
	vpayload.tmp = cc_tx_vpayload.tmp;
	vpayload.lux = cc_tx_vpayload.lux;
	vpayload.hmd = cc_tx_vpayload.hmd;

	// Copy our structure into the packet we're going to send
	memcpy((char*)&(cc_tx_data[2]), &vpayload, sizeof(vpayload_t));
	/* "Free" the rx buffer as soon as possible */
	cc_ptr = 0;
	/* Prepare buffer for sending */
	// Length
	cc_tx_data[0] = tx_len + 1;
	// Destination address
	cc_tx_data[1] = RECEPTOR_ADDRESS; // Change it for different receptors

	/* Send */
	if (cc1101_tx_fifo_state() != 0) {
		cc1101_flush_tx_fifo();
	}

	ret = cc1101_send_packet(cc_tx_data, (tx_len + 2));
	if(ret < 0)
	{
		// Error display
		char data[20];
		snprintf(data, 20, "ERROR: %d - %d", ERROR_CC1101_SEND, ret);
		display_line(7, 0, data);
		gpio_clear(status_led_green);
		gpio_set(status_led_red);
	}

#ifdef DEBUG
	uprintf(UART0, "Tx ret: %d\n\r", ret);
#endif
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

			// Preparing what we want to display
			snprintf(data, 20, "TMP: %d.%d	dC", temp/10, temp%10);
			// ... and displaying it.
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
				// In case the screen fails, we display it on... the screen.
				// That aside, we also set the red led, so it's not totally stupid.
				char data[20];
				snprintf(data, 20, "ERROR: %d - %d", ERROR_DISPLAY_FAILURE, ret);
				display_line(7, 0, data);
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
				snprintf(data, 20, "				");
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
		char checksumByte[8];
		checksumByte[0] = '0';
		checksumByte[1] = '0';

		// For parity bit and division checksum calculation
		// This is super long, ugly and there's probably an easier way to do it,
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
		cc_tx_vpayload.source = MODULE_ADDRESS;
		cc_tx_vpayload.checksum = cc_checksum;
		cc_tx_vpayload.tmp = temp;
		cc_tx_vpayload.lux = lux;
		cc_tx_vpayload.hmd = humidity;

		// We set the "please send me" flag
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
			handle_rf_rx_data();
			check_rx = 0;
		}
		
		// We add a delay not to flood the frequency and also the receptors
		msleep(1000);
	}
	return 0;
}