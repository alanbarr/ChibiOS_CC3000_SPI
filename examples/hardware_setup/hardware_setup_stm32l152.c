/* The following is an example of the hardware setup which is 
 * required before calling cc3000ChibiosWlanInit() on a STM32L152 platform.
 */
#include "cc3000_chibios_api.h"

/* SPI driver config */
SPIConfig chSpiConfig;

#ifdef STM32L1XX_MD
void setupCC3000Hw(void)
{
    /* SPI Config */
    /* BR bits and clock speeds:
     * 000  /2   =   16 MHz
     * 001  /4   =   8 MHz
     * 010  /8   =   4 MHz
     * 011  /16  =   2 MHz
     * 100  /32  =   1 MHz
     * 101  /64  =   500 kHz
     * 110  /128 =   250 kHz
     * 111  /256 =   125 kHz */
    chSpiConfig.cr1 =   /* 2nd clock transition first data capture edge */
                        SPI_CR1_CPHA |
                        /* BR: 011 - 2 MHz  */
                        (SPI_CR1_BR_1 | SPI_CR1_BR_0 );  

    /* Setup SPI pins. */
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

    /* Setup IRQ pin. */
    palSetPadMode(CHIBIOS_CC3000_IRQ_PORT, CHIBIOS_CC3000_IRQ_PAD,
                  PAL_MODE_INPUT_PULLUP);

    /* Setup WLAN EN pin. */
    palClearPad(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD);
    palSetPadMode(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD,
                  PAL_MODE_OUTPUT_PUSHPULL |
                  PAL_STM32_OSPEED_LOWEST);     /* 400 kHz */

    extObjectInit(&EXTD1);
    spiObjectInit(&SPID2);
}
#endif /* STM32L1XX_MD */

