/****************************************************************************
 * rf-sub1ghz/receptor/main.c
 *
 * Gateway microcontroller between the sensors one and the Raspberry Pi
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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


#define MODULE_VERSION  0x01
#define MODULE_NAME "sensorwatch - Receptor microcontroller"
// The address is arbitrary, it just has to be different from the sensors.
// Our microcontroller had the number 22, which is 16 in hexadecimal, so that's why.
#define MODULE_ADDRESS  0x16 
// Sensors' microcontroller address - arbitrary but different as well
#define SENSORS_ADDRESS 0x1A 

#define SELECTED_FREQ FREQ_SEL_48MHz

// Error codes to be displayed on the screen
// Since we can't use minicom through UART0 since this is used to send message on rf,
// we display error codes on the last line of the screen instead so we know if
// something goes wrong and what.
//
// Misc errors go from 1-9
#define ERROR_FAULT_INFO	  1
// Honestly now i wonder where this one will be displayed but let's keep it anyway
#define ERROR_DISPLAY_FAILURE 2 

// Sensors errors go from 10-29
// We don't use these here but we do on the other microcontroller
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
// UID: 0x0214f5f5 - 0x4e434314 - 0x09393536 - 0x54001e3e

// Display flag: set to 1 when we need to update the Adafruit screen display
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
// instead of UART0, since it will be use to transfer data.
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

	// Clearing the LEDs
	gpio_clear(status_led_red);
	gpio_clear(status_led_green);
}

/******************************************************************************/
/* RF Communication */
#define RF_BUFF_LEN 64

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

/* Payload types
 *
 * In order to send and receive data, whether it is values from the sensors
 * or an order change request to the sensors' display, we encapsulate it
 * in a struct to make it easier to handle.
 *
 */

// Packets containing values received from the sensors go here
typedef struct vpayload_t
{
	char source;
	char checksum;
	uint32_t tmp;
	uint16_t hmd;
	uint32_t lux;
} vpayload_t;

// Packets containing the order we're sending the sensors' microcontroller go here
typedef struct opayload_t
{
	char source;
	char first;
	char second;
	char third;
} opayload_t;

// This will be used to transfer data from where we got it (rf) to the USB (UART0)
static volatile vpayload_t cc_tx_vpayload;

// Function called when data comes from the radio
void handle_rf_rx_data(void)
{
	uint8_t data[RF_BUFF_LEN];
	uint8_t status = 0;

	/* Check for received packet (and get it if any) */
	cc1101_receive_packet(data, RF_BUFF_LEN, &status);
	
	/* Go back to RX mode */
	cc1101_enter_rx_mode();

#ifdef DEBUG
    uprintf(UART0, "RF: ret:%d, st: %d.\n\r", ret, status);
#endif

    // We instantate it locally so we don't mess up with volatile data (yet :))
	vpayload_t received_payload;

    // Address verification
	if(data[1] == MODULE_ADDRESS)
	{
        // We use the led to signal we're handling the data.
        // However, it barely blinks so it's barely noticeable, but still.
		gpio_clear(status_led_green);
		gpio_set(status_led_red);

        // Copy the received data in our own struct so we can handle it better
		memcpy(&received_payload, &data[2], sizeof(vpayload_t));
		
        // I couldn't manage to handle checksum verification in time,
        // so this is merely a relic from it, unfortunately.
		char checksumByte[8];
		snprintf(checksumByte, sizeof(checksumByte), "%c%c%c%c%c%c%c%c",
			(received_payload.checksum&0x80)?'1':'0',
			(received_payload.checksum&0x40)?'1':'0',
			(received_payload.checksum&0x20)?'1':'0',
			(received_payload.checksum&0x10)?'1':'0',
			(received_payload.checksum&0x08)?'1':'0',
			(received_payload.checksum&0x04)?'1':'0',
			(received_payload.checksum&0x02)?'1':'0',
			(received_payload.checksum&0x01)?'1':'0'
		);

        // Sending our sensors values on the USB, which will then
        // be handled on the Raspberry Pi and then to the app.
		uprintf(UART0, "%d.%d;%d.0;%d.%d;", 
			received_payload.tmp/10, received_payload.tmp%10,
			received_payload.lux,
			received_payload.hmd/10, received_payload.hmd%10);

        // We're done handling the data, so we're resetting the LEDs.
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
void handle_uart_cmd(uint8_t c)
{
#ifdef DEBUG
    uprintf(UART0, "Received command : %c, buffer size: %d.\n\r",c,cc_ptr);
#endif

	// Data
	// Most of the data is handled in the main loop, so we just use it as it is
	// passed here
	if (cc_ptr < RF_BUFF_LEN)
	{
		cc_tx_buff[cc_ptr++] = c;
	} else {
		// Reset the pointer
		cc_ptr = 0;
	}
	if ((c == '\n') || (c == '\r') || (cc_ptr>=3)) {
        // Using the leds again to signal we're ready to send
		gpio_clear(status_led_green);
		gpio_set(status_led_red);

        // Setting the "please send me" flag
		cc_tx = 1;

        // Resetting the pointer
		cc_ptr = 0;

        // Resetting the leds
		gpio_clear(status_led_red);
		gpio_set(status_led_green);
	}
}

void send_on_rf(void)
{
	uint8_t cc_tx_data[sizeof(opayload_t) + 2];
	int ret = 0;
	opayload_t opayload;

	/* Create a local copy */
	// Source address
	opayload.source = MODULE_ADDRESS;
	opayload.first = cc_tx_buff[0];
	opayload.second = cc_tx_buff[1];
	opayload.third = cc_tx_buff[2];

    // Preparing our packet by copying our payload inside
    // 0 and 1 indexes are for length and destination, respectively
	memcpy((char*)&(cc_tx_data[2]), &opayload, sizeof(opayload_t));

	/* Prepare buffer for sending */
    // Length
	cc_tx_data[0] = sizeof(opayload_t) + 1;
    // Destination
	cc_tx_data[1] = SENSORS_ADDRESS; // Change it for different sensors' microcontrollers

	/* Send */
	if (cc1101_tx_fifo_state() != 0) {
		cc1101_flush_tx_fifo();
	}
	ret = cc1101_send_packet(cc_tx_data, (sizeof(opayload_t) + 2));
	if(ret < 0)
	{
		// Since we don't use UART to signal problems and we don't have a screen
        // here either, we're using what we can, aka the LEDs again.
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
	// Setup phase
	system_init();
	uart_on(UART0, 115200, handle_uart_cmd);
	i2c_on(I2C0, I2C_CLK_100KHz, I2C_MASTER);
	ssp_master_on(0, LPC_SSP_FRAME_SPI, 8, 4*1000*1000); /* bus_num, frame_type, data_width, rate */

	/* Radio */
	rf_config();

	// When everything is up and running, we use the green LED
	gpio_set(status_led_green);

	while (1)
	{
		uint8_t status = 0;

		/* RF */
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
	}
	return 0;
}