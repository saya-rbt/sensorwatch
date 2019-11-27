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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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


#define MODULE_VERSION  0x01
#define MODULE_NAME "sensorwatch - Receptor microcontroller"
#define MODULE_ADDRESS  0x16 // Arbitrary, just have it different from the snesors
#define SENSORS_ADDRESS 0x1A // Receptor address - arbitrary but different as well

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
// We don't use these here but we do on the other microcontroller
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
// UID: 0x0214f5f5 - 0x4e434314 - 0x09393536 - 0x54001e3e

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
// const struct pio status_led_orange = LPC_GPIO_0_10;
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
    // gpio_clear(status_led_orange);
    gpio_clear(status_led_green);

    // // We use the orange LED to warn that we're booting
    // gpio_set(status_led_orange);
}


/* Define our fault handler. This one is not mandatory, the dummy fault handler
 * will be used when it's not overridden here.
 * Note : The default one does a simple infinite loop. If the watchdog is deactivated
 * the system will hang.
 */

// void fault_info(const char* name, uint32_t len)
// {
//  // TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
//  // uprintf(UART0, name);
//  char data[20];
//  snprintf(data, 20, "ERROR: %d - %c", ERROR_FAULT_INFO, name);
//  display_line(7, 0, data);
//  // gpio_clear(status_led_orange);
//  gpio_clear(status_led_green);
//  gpio_set(status_led_red);
//  while (1);
// }


/***************************************************************************** */
/* Communication over USB */
uint8_t text_received = 0;
#define NB_CHARS  15
char inbuff[NB_CHARS + 1];
void data_rx(uint8_t c)
{
    static int idx = 0;
    if ((c != 0) && (c != '\n') && (c != '\r')) {
        inbuff[idx++] = c;
        if (idx >= NB_CHARS) {
            inbuff[NB_CHARS] = 0;
            idx = 0;
            text_received = 1;
        }
    } else {
        if (idx != 0) {
            inbuff[idx] = 0;
            text_received = 1;
        }
        idx = 0;
    }
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

// #ifdef DEBUG
//  // TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
//  uprintf(UART0, "CC1101 RF link init done.\n\r");
// #endif
}

void handle_rf_rx_data(void)
{
    uint8_t data[RF_BUFF_LEN];
    int8_t ret = 0;
    uint8_t status = 0;

    /* Check for received packet (and get it if any) */
    ret = cc1101_receive_packet(data, RF_BUFF_LEN, &status);
    if(ret != 0)
    {
        // char data[20];
        // snprintf(data, 20, "ERROR: %d - %d", ERROR_CC1101_RECEIVE, ret);
        // display_line(7, 0, data);
        gpio_clear(status_led_green);
        gpio_set(status_led_red);
    }
    /* Go back to RX mode */
    cc1101_enter_rx_mode();

// #ifdef DEBUG
//  // TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
//  uprintf(UART0, "RF: ret:%d, st: %d.\n\r", ret, status);
// #endif

    if(data[1] == MODULE_ADDRESS)
    {
        gpio_clear(status_led_green);
        gpio_set(status_led_red);
        int len = data[0];
        int j = 0;
        char values[len];
        memcpy(&values, &data[4], len);
        for(int i = 4; i <= len; i++)
        {
            values[j] = data[i];
            j++;
        }
        gpio_clear(status_led_red);
        gpio_clear(status_led_green);
        uprintf(UART0, "%s e \n\r", values);
    }       
}


/* Data sent on radio comes from the UART, put any data received from UART in
 * cc_tx_buff and send when either '\r' or '\n' is received.
 * This function is very simple and data received between cc_tx flag set and
 * cc_ptr rewind to 0 may be lost. */
static volatile uint32_t cc_tx = 0;
static volatile uint8_t cc_tx_buff[RF_BUFF_LEN];
static volatile uint8_t cc_ptr = 1; // We start at 1 because 0 is the source
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
//  // TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
//  uprintf(UART0, "Received command : %c, buffer size: %d.\n\r",c,cc_ptr);
// #endif
    // Source address
    cc_tx_buff[0] = MODULE_ADDRESS;

    // Data
    // Most of the data is handled in the main loop, so we just use it as it is
    // passed here
    cc_tx_buff[1] = cc_checksum;
    if (cc_ptr < RF_BUFF_LEN) {
        cc_tx_buff[cc_ptr++] = c;
    } else {
        // Reset the pointer
        cc_ptr = 0;
    }
    if ((c == '\n') || (c == '\r')) {
        cc_tx = 1;
    }
}

void send_on_rf(void)
{
    uint8_t cc_tx_data[RF_BUFF_LEN + 2];
    uint8_t tx_len = cc_ptr;
    int ret = 0;

    /* Create a local copy */
    memcpy((char*)&(cc_tx_data[2]), (char*)cc_tx_buff, tx_len);
    /* "Free" the rx buffer as soon as possible */
    cc_ptr = 0;
    /* Prepare buffer for sending */
    cc_tx_data[0] = tx_len + 1;
    cc_tx_data[1] = SENSORS_ADDRESS; // Change it for different receptors
    /* Send */
    if (cc1101_tx_fifo_state() != 0) {
        cc1101_flush_tx_fifo();
    }
    ret = cc1101_send_packet(cc_tx_data, (tx_len + 2));
    if(ret != 0)
    {
        // char data[20];
        // snprintf(data, 20, "ERROR: %d - %d", ERROR_CC1101_SEND, ret);
        // display_line(7, 0, data);
        gpio_clear(status_led_green);
        gpio_set(status_led_red);
    }

// #ifdef DEBUG
//  // TODO: PRINT ON SCREEN INSTEAD!! UART0 WILL BE USED
//  uprintf(UART0, "Tx ret: %d\n\r", ret);
// #endif
}

/**************************************************************************** */
int main(void)
{
    // int ret = 0;
    system_init();
    uart_on(UART0, 115200, data_rx);
    i2c_on(I2C0, I2C_CLK_100KHz, I2C_MASTER);
    ssp_master_on(0, LPC_SSP_FRAME_SPI, 8, 4*1000*1000); /* bus_num, frame_type, data_width, rate */

    // status_led(green_only);

    /* Radio */
    rf_config();

    // When everything is up and running, we use the green LED
    // gpio_clear(status_led_orange);
    gpio_set(status_led_green);

    // uprintf(UART0, "App started\n\r");
    while (1)
    {
        uint8_t status = 0;
        // uprintf(UART0, "a");

        /* Alive notice*/
        // chenillard(250);

        /* RF */

        //TODO : uprintf what we receive through handle_rf_rx_data

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
    }
    return 0;
}