/* CC3000 acts as a client, transmiting and receiving messages over UDP.
 * udp_server.py should be correctly configuring and running on
 * a suitable host when running this program.
 * This program sends a UDP message to the python server which will reply,
 * with each reply toggling an LED. */

#include "ch.h"
#include "hal.h"
#include "board.h"
#include "chstreams.h"
#include "chprintf.h"
#include "string.h"
#include "cc3000_chibios_api.h"
#include "socket.h"
#include "hci.h"
#include "nvmem.h"
#include "netapp.h"

/* Serial driver to be used */
#define SERIAL_DRIVER           SD1

/* SPI driver being used for CC3000 */
#define SPI_DRIVER              SPID2

/* EXT driver being used for CC3000 */
#define EXT_DRIVER              EXTD1

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

Mutex printMtx;

static SPIConfig chSpiConfig;
static EXTConfig chExtConfig;

static void cc3000Udp(void)
{
    uint8_t patchVer[2];
    int sock;
    sockaddr_in destAddr;
    sockaddr fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    char rxBuffer[32];
    int recvRtn = 0;
    tNetappIpconfigRetArgs ipConfig;

    PRINT("Before cc3000ChibiosWlanInit", NULL);
    cc3000ChibiosWlanInit(&SPI_DRIVER, &chSpiConfig,
                          &EXT_DRIVER, &chExtConfig,
                          0,0,0);
    PRINT("After cc3000ChibiosWlanInit", NULL);

    PRINT("Before wlan_start", NULL);
    wlan_start(0);
    PRINT("After wlan_start", NULL);

    nvmem_read_sp_version(patchVer);
    PRINT("--Start of nvmem_read_sp_version--", NULL);
    PRINT("Package ID: %d", patchVer[0]);
    PRINT("Build Version: %d", patchVer[1]);
    PRINT("--End of nvmem_read_sp_version--", NULL);

    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(REMOTE_PORT);
    destAddr.sin_addr.s_addr = htonl(REMOTE_IP);

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

    PRINT("Creating socket...", NULL);
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == ERROR)
    {
        PRINT("socket() returned error.", NULL);
        return;
    }
    PRINT("Created!", NULL);

    while (1)
    {
        chThdSleep(S2ST(3));
        palTogglePad(LED_PORT, LED_PIN);

        PRINT("Sending...", NULL);
        if (sendto(sock, TX_MSG, TX_MSG_SIZE, 0,
                        (sockaddr*)&destAddr,
                        sizeof(destAddr)) == ERROR)
        {
            PRINT("sendto() returned error.", NULL);
            return;
        }
        PRINT("Sent!", NULL);

        memset(rxBuffer, 0, sizeof (rxBuffer));

        PRINT("Receiving...", NULL);
        if ((recvRtn = recvfrom(sock, rxBuffer, sizeof(rxBuffer),
                                0, &fromAddr, &fromLen)) == ERROR)
        {
            PRINT("recvfrom() returned error.", NULL);
            return;
        }

        PRINT("Receive return %d.", recvRtn);
        if (recvRtn == RX_MSG_EXP_SIZE)
        {
            if (strcmp(rxBuffer, RX_MSG_EXP) == 0)
            {
                palTogglePad(LED_PORT, LED_PIN);
                PRINT("Received the expected message: %s", rxBuffer);
                continue;
            }
        }
        
        PRINT("Receive not as expected.", NULL);
    }
}


void setupSpiHw(void)
{
#ifdef STM32L1XX_MD

    /* SPI Config */
    chSpiConfig.end_cb = NULL;
    chSpiConfig.ssport = CHIBIOS_CC3000_PORT;
    chSpiConfig.sspad = CHIBIOS_CC3000_NSS_PAD;
    chSpiConfig.cr1 = SPI_CR1_CPHA |    /* 2nd clock transition first data capture edge */
                      (SPI_CR1_BR_1 | SPI_CR1_BR_0 );   /* BR: 011 - 2 MHz  */
 
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

    /* Mtx to protect chprintf */
    chMtxInit(&printMtx);
    
    /* Setup hardware for interfacing with CC3000 */
    setupSpiHw();

    cc3000Udp();

    /* Only hit this if an error occurs */
    palSetPad(LED_ERROR_PORT, LED_ERROR_PIN);
    wlan_stop();
    while (1);

  return 0;
}


