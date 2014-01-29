#include "cc3000_chibios_api.h"
#include "ch.h"
#include "hal.h"
#include "board.h"
#include "chstreams.h"
#include "chprintf.h"
#include "string.h"
#include "socket.h"
#include "hci.h"
#include "nvmem.h"
#include "netapp.h"

/* Serial driver to be used */
#define SERIAL_DRIVER       SD1

/* SPI driver being used for CC3000 */
#define SPI_DRIVER          SPID2

/* EXT driver being used for CC3000 */
#define EXT_DRIVER          EXTD1

/* Enable / Disable chprintf's */
#define PRINT(fmt, ...)                                                     \
        chMtxLock(&printMtx);                                               \
        chprintf((BaseSequentialStream*)&SERIAL_DRIVER,                     \
                 "(%s:%d) " fmt "\n\r", __FILE__, __LINE__, __VA_ARGS__);   \
        chMtxUnlock();

/* LED for notification setup */
#define LED_PORT            GPIOB
#define LED_PIN             GPIOB_LED3

/* LED for error setup */
#define LED_ERROR_PORT      GPIOB
#define LED_ERROR_PIN       GPIOB_LED4

/* Remote information */
#define REMOTE_IP           0xA000001 /* 10.0.0.1 */
#define REMOTE_PORT         44444

/* Messages */
#define TX_MSG              "Hello World from CC3000"
#define TX_MSG_SIZE         strlen(TX_MSG)
#define RX_MSG_EXP          "Hello CC3000"
#define RX_MSG_EXP_SIZE     strlen(RX_MSG_EXP)

/* Access point config - arguments to wlan_connect */
#define SSID                "FYP"
#define SSID_LEN            strlen(SSID)
#define SEC_TYPE            WLAN_SEC_UNSEC
#define KEY                 NULL
#define KEY_LEN             0
#define BSSID               NULL

#define SUCCESS             0
#define ERROR               -1

/* Host to lookup and ping */
#define HOSTNAME            "www.google.co.uk"
#define HOSTNAME_LENGTH     strlen(HOSTNAME)

/* Mutex to protect all chprintf's over serial. */
Mutex printMtx;

static SPIConfig chSpiConfig;
static EXTConfig chExtConfig;

/* Ping example */
static void cc3000Ping(void)
{
    uint8_t patchVer[2];
    uint32_t remoteHostIp;
    tNetappIpconfigRetArgs ipConfig;

    PRINT("Before cc3000ChibiosWlanInit", NULL);
    cc3000ChibiosWlanInit(&SPI_DRIVER, &chSpiConfig,
                          &EXT_DRIVER, &chExtConfig,
                          0,0,0);
    PRINT("After cc3000ChibiosWlanInit", NULL);

    PRINT("Before wlan_start", NULL);
    wlan_start(0);
    PRINT("After wlan_start", NULL);

    /* Read version info */
    nvmem_read_sp_version(patchVer);
    PRINT("--Start of nvmem_read_sp_version--", NULL);
    PRINT("Package ID: %d", patchVer[0]);
    PRINT("Build Version: %d", patchVer[1]);
    PRINT("--End of nvmem_read_sp_version--", NULL);

    PRINT("Attempting to connect to network...", NULL);
    if (wlan_connect(SEC_TYPE, SSID, SSID_LEN, BSSID, KEY, KEY_LEN) != SUCCESS)
    {
        PRINT("Unable to connect to access point.", NULL);
        return;
    }

    while (cc3000AsyncData.connected != 1)
    {
        chThdSleep(MS2ST(5));
    }
    PRINT("Connected!", NULL);

    PRINT("Waiting for DHCP...", NULL);
    while (cc3000AsyncData.dhcp.present != 1)
    {
        chThdSleep(MS2ST(5));
    }
    PRINT("Received!", NULL);

    PRINT("Finding IP information...", NULL);
    netapp_ipconfig(&ipConfig);
    PRINT("Found!", NULL);

    PRINT("Looking up IP of %s...", HOSTNAME);
    gethostbyname(HOSTNAME, HOSTNAME_LENGTH, &remoteHostIp);
    remoteHostIp = htonl(remoteHostIp);
    PRINT("IP of %s is %x", HOSTNAME, remoteHostIp);

    /* I believe there is an active bug against the ping report where 
     * some of the numbers are incorrect. Loop here forever waiting on 
     * a valid response and hope it arrives... */
    while (cc3000AsyncData.ping.report.packets_received != 3)
    {
        PRINT("Pinging...", NULL);
        memset((void *)&cc3000AsyncData.ping, 0, sizeof(cc3000AsyncData.ping));
        netapp_ping_send(&remoteHostIp, 3, 10, 3000);
        
        while (cc3000AsyncData.ping.present != TRUE)
        {
            chThdSleep(MS2ST(100));
        }

        PRINT("--Ping Results--:", NULL);
        PRINT("Number of Packets Sent: %u", cc3000AsyncData.ping.report.packets_sent);
        PRINT("Number of Packet Received: %u", cc3000AsyncData.ping.report.packets_received);
        PRINT("Min Round Time: %u", cc3000AsyncData.ping.report.min_round_time);
        PRINT("Max Round Time: %u", cc3000AsyncData.ping.report.max_round_time);
        PRINT("Avg Round Time: %u", cc3000AsyncData.ping.report.avg_round_time);
        PRINT("--End of Ping Results--", NULL);
    }
    palSetPad(LED_PORT, LED_PIN);
    while(1);
}


void setupSpiHw(void)
{
#ifdef STM32L1XX_MD

    /* SPI Config */
    chSpiConfig.end_cb = NULL;
    chSpiConfig.ssport = CHIBIOS_CC3000_PORT;
    chSpiConfig.sspad = CHIBIOS_CC3000_NSS_PAD;
    chSpiConfig.cr1 = SPI_CR1_CPHA |    /* 2nd clock transition first data capture edge */
                      (SPI_CR1_BR_1 | SPI_CR1_BR_0 );   /* BR: 011 - 2 MHz */

    /* Setup SPI pins */
    palSetPad(CHIBIOS_CC3000_PORT, CHIBIOS_CC3000_NSS_PAD);
    palSetPadMode(CHIBIOS_CC3000_PORT, CHIBIOS_CC3000_NSS_PAD,
                  PAL_MODE_OUTPUT_PUSHPULL |
                  PAL_STM32_OSPEED_LOWEST);     /* 400 kHz */

    palSetPadMode(CHIBIOS_CC3000_PORT, CHIBIOS_CC3000_SCK_PAD,
                  PAL_MODE_ALTERNATE(5) |       /* SPI */
                  PAL_STM32_OTYPE_PUSHPULL |
                  PAL_STM32_OSPEED_MID2);       /* 10 MHz */

    palSetPadMode(CHIBIOS_CC3000_PORT, CHIBIOS_CC3000_MISO_PAD,
                  PAL_MODE_ALTERNATE(5));       /* SPI */

    palSetPadMode(CHIBIOS_CC3000_PORT, CHIBIOS_CC3000_MOSI_PAD,
                  PAL_MODE_ALTERNATE(5) |       /* SPI */
                  PAL_STM32_OTYPE_PUSHPULL |
                  PAL_STM32_OSPEED_MID2);       /* 10 MHz */

    /* Setup IRQ pin */
    palSetPadMode(CHIBIOS_CC3000_IRQ_PORT, CHIBIOS_CC3000_IRQ_PAD,
                  PAL_MODE_INPUT_PULLUP);

    /* Setup WLAN EN pin.
       With the pin low, we sleep here to make sure CC3000 is off.  */
    palClearPad(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD);
    palSetPadMode(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD,
                  PAL_MODE_OUTPUT_PUSHPULL |
                  PAL_STM32_OSPEED_LOWEST);     /* 400 kHz */

#endif /* STM32L1XX_MD */

    extObjectInit(&EXT_DRIVER);
    spiObjectInit(&SPI_DRIVER);
}


int main(void)
{
    halInit();
    chSysInit();

    /* Led for status */
    palClearPad(LED_PORT, LED_PIN);
    palSetPadMode(LED_PORT, LED_PIN, PAL_MODE_OUTPUT_PUSHPULL);

    /* Led for error */
    palClearPad(LED_ERROR_PORT, LED_ERROR_PIN);
    palSetPadMode(LED_ERROR_PORT, LED_ERROR_PIN, PAL_MODE_OUTPUT_PUSHPULL);

    /* Serial for debugging */
    sdStart(&SERIAL_DRIVER, NULL);
    palSetPadMode(GPIOA, 9, PAL_MODE_ALTERNATE(7));
    palSetPadMode(GPIOA, 10, PAL_MODE_ALTERNATE(7));

    chMtxInit(&printMtx);
    
    setupSpiHw();

    cc3000Ping();

    /* Only hit this if an error occurs */
    palSetPad(LED_ERROR_PORT, LED_ERROR_PIN);
    wlan_stop();
    while (1);
    
    return 0;
}


