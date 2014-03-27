/** @file
*   @brief     CC3000 SPI driver for ChibiOS/RT.
*   @details   This bridges TI's CC3000 Host Driver to ChibiOS/RT to
*              facilitate communications with a CC3000 module.              */
/*****************************************************************************
*  Renamed to cc3000_spi.c and adapted for ChibiOS/RT by Alan Barr 2014.
*
*  spi.c - CC3000 Host Driver Implementation.
*  Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*    Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*
*    Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the
*    distribution.
*
*    Neither the name of Texas Instruments Incorporated nor the names of
*    its contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*****************************************************************************/

#include "cc3000_chibios_config.h"
#include "async_handler.h"
#include "cc3000_spi.h"
#include "hci.h"
#include "wlan.h"

/* OP Codes for CC3000_SPI_INDEX_OP */
/** @brief Operation opcode for write. */
#define CC3000_SPI_OP_WRITE         1
/** @brief Operation opcode for read. */
#define CC3000_SPI_OP_READ          3

/* Indexes into TX Buffer */
/** @brief Index of operation opcode in the transmit buffer. */
#define CC3000_SPI_INDEX_OP         0
/** @brief Index of most significant byte of length in the transmit buffer. */
#define CC3000_SPI_INDEX_LEN_MSB    1
/** @brief Index of least significant byte of length in the transmit buffer. */
#define CC3000_SPI_INDEX_LEN_LSB    2
/** @brief Index of first busy byte in the transmit buffer. */
#define CC3000_SPI_INDEX_BUSY_1     3
/** @brief Index of second busy byte in the transmit buffer. */
#define CC3000_SPI_INDEX_BUSY_2     4

/** @brief Size of HEADERS_SIZE_EVNT. */
#define CC3000_HEADERS_SIZE_EVNT    (SPI_HEADER_SIZE + 5)

/** @brief Overflow indicator added to the end of transmit and receive buffers. */
#define CC3000_SPI_MAGIC_NUMBER     (0xDE)
/** @brief Location of #CC3000_SPI_MAGIC_NUMBER in transmit buffer. */
#define CC3000_SPI_TX_MAGIC_INDEX   (CC3000_TX_BUFFER_SIZE - 1)
/** @brief Location of #CC3000_SPI_MAGIC_NUMBER in receive buffer. */
#define CC3000_SPI_RX_MAGIC_INDEX   (CC3000_RX_BUFFER_SIZE - 1)

/** @brief Minimum number of bytes that can be read. */
#define CC3000_SPI_MIN_READ_B       (10)

/** @brief Value of byte introduced to create a delay. */
#define CC3000_SPI_BUSY             0

/** @brief The various states of the CC3000 SPI driver. */
typedef enum
{
    SPI_STATE_POWERUP,         ///< CC3000 powered up (Enable pin high).
    SPI_STATE_INITIALIZED,     ///< CC3000 has responded to powerup (IRQ low).
    SPI_STATE_IDLE,            ///< Idle.
    SPI_STATE_WRITE_REQUESTED, ///< Write has been requested by selecting CC3000.
    SPI_STATE_WRITE_PERMITTED, ///< Write has been permitted by CC3000 acknowledging.
    SPI_STATE_READ             ///< Performing a read operation.
} spiState;

/** @brief Information required by CC3000 SPI driver. */
typedef struct
{
    gcSpiHandleRx rxHandlerCb;      ///< Handler function for received data.
    unsigned short txPacketLength;  ///< Number of bytes to transmit.
    unsigned short rxPacketLength;  ///< Number of bytes received. (DEBUG)
    spiState spiState;              ///< Current state of the driver.
    unsigned char *pTxPacket;       ///< Points to data to be transmitted.
    unsigned char *pRxPacket;       ///< Points to where to store received data.
} tSpiInformation;

/** @brief Transmit buffer.
 *  @todo TI issue. The host driver (ver 1.11.1) *knows* its going to be called
 *  this, but still goes and stored it in tSLInformation.pucTxCommandBuffer...
 *  why? */
unsigned char wlan_tx_buffer[CC3000_TX_BUFFER_SIZE];

/** @brief Receive buffer. */
static unsigned char spi_buffer[CC3000_RX_BUFFER_SIZE];

/** @brief These bytes should be sent to the CC3000 on every SPI read. */
static const unsigned char spiReadCommand[] =
                        {CC3000_SPI_OP_READ, CC3000_SPI_BUSY, CC3000_SPI_BUSY};

/** @brief Pointer to the ChibiOS SPI driver being used for CC3000 
 *         communications. */
static SPIDriver * chSpiDriver;
/** @brief Holds the SPI driver config. */
static SPIConfig chSpiConfig;
/** @brief Pointer to the ChibiOS EXT driver being used for CC3000 
 *         IRQ line monitoring. */
static EXTDriver * chExtDriver;
/** @brief Pointer to the EXT driver config. */
static EXTConfig * chExtConfig;
/** @brief CC3000 SPI driver data. */
static volatile tSpiInformation spiInformation;
/** @brief ChibiOS/RT semaphore to signal #irqSignalHandlerThread(). */
static Semaphore irqSem;
/** @brief ChibiOS/RT thread working aread for #irqSignalHandlerThread(). */
static WORKING_AREA(irqSignalHandlerThreadWorkingArea,
                    CHIBIOS_CC3000_IRQ_THD_AREA);

/** @brief Flag to allow the IRQ thread to defer handling an
 *         interrupt.
 *  @details Needed to allow #SpiResumeSpi() and #SpiPauseSpi() to work as
 *           intended.
 **/
static volatile bool spiPaused = true;

#if CHIBIOS_CC3000_SPI_EXCLUSIVE == FALSE
/** @brief Stores the state of the SPI bus before we acquired it. */
bool spiWasStoppedBeforeAcquired;
#endif

/** @brief Signals CC3000 for intent to communicate. */
static void selectCC3000(void)
{
#if CHIBIOS_CC3000_SPI_EXCLUSIVE == FALSE 
    spiAcquireBus(chSpiDriver);
    spiStart(chSpiDriver, &chSpiConfig);
#endif

    spiSelect(chSpiDriver);
}


/** @brief Signals CC3000 for to end communications. */
static void unselectCC3000(void)
{
    spiUnselect(chSpiDriver);

#if CHIBIOS_CC3000_SPI_EXCLUSIVE == FALSE 
    spiStop(chSpiDriver);
    spiReleaseBus(chSpiDriver);
#endif
}

/* returns true if set. */
static bool setSpiState(spiState state)
{
#if 0
    bool rtn = false;

    chSysLock();
    /* Reading only happends in irq. */
    if (state == SPI_STATE_READ)
    {
        if (spiInformation.spiState == SPI_STATE_IDLE)
        {
            spiInformation.spiState = SPI_STATE_READ;
            rtn = true;
        }
        else
        {
            return false;
        }

        chSysUnlock();
        return rtn;
    }
    
    else if (state == SPI_STATE_INITIALIZED || state == SPI_STATE_POWERUP)
    {
        spiInformation.spiState = state;
        rtn = true;
        chSysUnlock();
        return rtn;
    }

    else if (state == SPI_STATE_WRITE_PERMITTED)
    {
        if (spiInformation.spiState != SPI_STATE_WRITE_REQUESTED)
        {
            port_halt(); /* This should never, ever happen. Probably */
        }
        else
        {
            spiInformation.spiState = SPI_STATE_WRITE_PERMITTED;
            rtn = true;
        }
        chSysUnlock();
        return rtn;

    }

    while(1)
    {
        if (state == SPI_STATE_IDLE)
        {
            if (spiInformation.spiState == SPI_STATE_WRITE_PERMITTED ||
                spiInformation.spiState == SPI_STATE_READ ||
                spiInformation.spiState == SPI_STATE_INITIALIZED) /* TODO consider a first write state */
            {
                spiInformation.spiState = SPI_STATE_IDLE;
                rtn = true;
                chSysUnlock();
                break;
            }
        }
        else if (state == SPI_STATE_WRITE_REQUESTED)
        {
            if (spiInformation.spiState == SPI_STATE_IDLE)
            {
                spiInformation.spiState = SPI_STATE_WRITE_REQUESTED;
                rtn = true;
                chSysUnlock();
                break;
            }
        }
        chSysUnlock();
        chThdSleep(3);
        chSysLock();
    }

    return rtn;
#else 
    chSysLock();
    spiInformation.spiState = state;
    chSysUnlock();
    return true;
#endif
}

/** @brief Writes data over SPI to the CC3000.
 *  @param data Data to be sent.
 *  @param size Number of bytes to be sent. */
static void SpiWriteDataSynchronous(unsigned char *data, unsigned short size)
{
    spiSend(chSpiDriver, size, data);
}


/** @brief Preforms the first write to the CC3000.
 *  @details The CC3000 requires a 50 us wait after the first four bytes of the
 *          first command from a powerup.
 *  @param pUserBuffer Data to write.
 *  @param usLength Size of data to write. */
static void SpiFirstWrite(unsigned char *pUserBuffer, unsigned short usLength)
{
    selectCC3000();

    chThdSleep(US2ST(50));

    SpiWriteDataSynchronous(pUserBuffer, 4);

    chThdSleep(US2ST(50));

    SpiWriteDataSynchronous(pUserBuffer + 4, usLength - 4);

    setSpiState(SPI_STATE_IDLE);

    unselectCC3000();
}


/** @brief Reads SPI data from the CC3000 over SPI.
 *  @param data Pointer to the buffer to store the data.
 *  @param size Number of bytes to read. */
static void SpiReadDataSynchronous(unsigned char *data, unsigned short size)
{
    spiExchange(chSpiDriver,
                sizeof(spiReadCommand),
                spiReadCommand,
                data);
    spiReceive(chSpiDriver,
               size - sizeof(spiReadCommand),
               &data[sizeof(spiReadCommand)]);

    spiInformation.rxPacketLength += size;
}


/** @brief Responsible for calling into TI's host driver with received data. */
static void SpiTriggerRxProcessing(void)
{
    tSLInformation.WlanInterruptDisable();
 
    /** @todo TI Issue: This is where it is in their example.
     * Can we not just hold this low, until we are done? i.e. move it until 
     * just before we return from this function? This should mean the CC3000
     * won't produce another interrupt until we are done processing this one. */
    unselectCC3000(); 

    if (spi_buffer[CC3000_SPI_RX_MAGIC_INDEX] != CC3000_SPI_MAGIC_NUMBER)
    {
        CHIBIOS_CC3000_DBG_PRINT("Buffer overflow detected.", NULL);
        while(1);
    }

    setSpiState(SPI_STATE_IDLE);
    spiInformation.rxPacketLength = 0;
 

    /* In 1.11.1: SpiReceiveHandler cc3000_spi.c */
    spiInformation.rxHandlerCb(spiInformation.pRxPacket + SPI_HEADER_SIZE);
 


}

/** @brief Reads the SPI header from the CC3000. */
static void SpiReadHeader(void)
{
    SpiReadDataSynchronous(spiInformation.pRxPacket, CC3000_SPI_MIN_READ_B);
}

/** @brief Reads remaining data after the SPI header.
 *  @details Called after data returned fomr #SpiReadHeader() has been
 *  processed. */
static void SpiReadAfterHeader(void)
{
    long data_to_recv = 0;
    unsigned char *evnt_buff, type;

    /* Determine what type of packet we have */
    evnt_buff =  spiInformation.pRxPacket;
    STREAM_TO_UINT8((char *)(evnt_buff + SPI_HEADER_SIZE),
                    HCI_PACKET_TYPE_OFFSET, type);

    switch(type)
    {
        case HCI_TYPE_DATA:
        {
            /* We need to read the rest of data.. */
            STREAM_TO_UINT16((char *)(evnt_buff + SPI_HEADER_SIZE),
                             HCI_DATA_LENGTH_OFFSET, data_to_recv);

            if (!((CC3000_HEADERS_SIZE_EVNT + data_to_recv) & 1))
            {
                data_to_recv++;
            }

            if (data_to_recv)
            {
                SpiReadDataSynchronous(evnt_buff + CC3000_SPI_MIN_READ_B,
                                       data_to_recv);
            }
            break;
        }
        case HCI_TYPE_EVNT:
        {
            /* Calculate the rest length of the data*/
            STREAM_TO_UINT8((char *)(evnt_buff + SPI_HEADER_SIZE),
                            HCI_EVENT_LENGTH_OFFSET, data_to_recv);
            data_to_recv -= 1;

            /* Add padding byte if needed */
            if ((CC3000_HEADERS_SIZE_EVNT + data_to_recv) & 1)
            {
                data_to_recv++;
            }

            if (data_to_recv)
            {
                SpiReadDataSynchronous(evnt_buff + CC3000_SPI_MIN_READ_B,
                                       data_to_recv);
            }

            break;
        }
    }
}


/** @brief Triggers the handler for an interrupt.
 *  @details Responsible for waking the interrupt handler thread,
 *           #irqSignalHandlerThread().
 *  @param extp ChibiOS/RT passes back this driver information. Ignored.
 *  @param channel ChibiOS/RT passes back this channel information. Ignored. */
static void cc3000ExtCb(EXTDriver *extp, expchannel_t channel)
{
    (void)extp;
    (void)channel;

    chSysLockFromIsr();
    chSemSignalI(&irqSem);
    chSysUnlockFromIsr();
}


/** @brief Handlers an interrupt request from the CC3000.
 *  @param arg Unused.
 *  @return Always 0.*/
static msg_t irqSignalHandlerThread(void *arg)
{
    (void)arg;

#if CH_USE_REGISTRY == TRUE
    chRegSetThreadName(__FUNCTION__);
#endif

    while (1)
    {
        /* Wait here until the EXT interrupt signals that the IRQ line went
         * low. */
        CHIBIOS_CC3000_DBG_PRINT("IRQ waiting on semaphore.", NULL);

        chSemWait(&irqSem);

        CHIBIOS_CC3000_DBG_PRINT("IRQ waiting on pause.", NULL);

        while (spiPaused == true)
        {
            chThdSleep(5);
        }

        CHIBIOS_CC3000_DBG_PRINT("IRQ Running.", NULL);

        while (spiInformation.spiState != SPI_STATE_POWERUP &&
               spiInformation.spiState != SPI_STATE_IDLE &&
               spiInformation.spiState != SPI_STATE_WRITE_REQUESTED); /* XXX can this happen?? */

        if (spiInformation.spiState == SPI_STATE_POWERUP)
        {
            /* This means IRQ line was low call a callback of HCI Layer to inform on event */
            setSpiState(SPI_STATE_INITIALIZED);
        }

        else if (spiInformation.spiState == SPI_STATE_IDLE)
        {
            setSpiState(SPI_STATE_READ);

            /* IRQ line goes down - start reception */
            selectCC3000();

            SpiReadHeader();

            SpiReadAfterHeader();

            /** @todo TI Issue It seems there is a potential for a race 
             * condition here. We can enter processing before we can set what
             * we are expecting to receive in the host driver 
             * http://e2e.ti.com/support/low_power_rf/f/851/t/312391.aspx */
            chThdSleep(MS2ST(100));

            SpiTriggerRxProcessing();
        }

        else if (spiInformation.spiState == SPI_STATE_WRITE_REQUESTED)
        {
            setSpiState(SPI_STATE_WRITE_PERMITTED);

        }
    }

    return 0;
}


/** @brief Prepares for communications with CC3000.
 *  @details Responsible for readying SPI and interrupt.
 *  @param pfRxHandler Function the host driver wishes to be called when SPI
 *                     data is received. */
void SpiOpen(gcSpiHandleRx pfRxHandler)
{
    memset(spi_buffer, 0, CC3000_RX_BUFFER_SIZE);
    memset(wlan_tx_buffer, 0, CC3000_TX_BUFFER_SIZE);
    memset((void*)&cc3000AsyncData, 0, sizeof(cc3000AsyncData));

    spi_buffer[CC3000_SPI_RX_MAGIC_INDEX] = CC3000_SPI_MAGIC_NUMBER;
    wlan_tx_buffer[CC3000_SPI_TX_MAGIC_INDEX] = CC3000_SPI_MAGIC_NUMBER;

    setSpiState(SPI_STATE_POWERUP);
    spiInformation.rxHandlerCb = pfRxHandler;
    spiInformation.txPacketLength = 0;
    spiInformation.pTxPacket = NULL;
    spiInformation.pRxPacket = (unsigned char *)spi_buffer;
    spiInformation.rxPacketLength = 0;

#if CHIBIOS_CC3000_EXT_EXCLUSIVE == TRUE
    extStart(chExtDriver, chExtConfig);
#endif

    extChannelEnable(chExtDriver, CHIBIOS_CC3000_IRQ_PAD);

#if CHIBIOS_CC3000_SPI_EXCLUSIVE == TRUE
    spiStart(chSpiDriver, &chSpiConfig);
#endif

    tSLInformation.WlanInterruptEnable();
    
    chThdSleep(MS2ST(100));

}


/** @brief Cleans up when communications to CC3000 stopped.
 *  @details Responsible for stopping SPI and interrupt. */
void SpiClose(void)
{
    tSLInformation.WlanInterruptDisable();

    extChannelDisable(chExtDriver, CHIBIOS_CC3000_IRQ_PAD);

#if CHIBIOS_CC3000_EXT_EXCLUSIVE == TRUE
    extStop(chExtDriver);
#endif

#if CHIBIOS_CC3000_SPI_EXCLUSIVE == TRUE
    spiStop(chSpiDriver);
#endif

}


/** @brief Writes data to the CC3000 over SPI.
 *  @param pUserBuffer Pointer to the data to be written.
 *  @param usLength Data size. */
void SpiWrite(unsigned char *pUserBuffer, unsigned short usLength)
{
    /* If usLength is even, we need to add padding byte */
    if (!(usLength & 0x01))
    {
        usLength++;
    }

    /* @todo TI issue: Why can't altering pUserBuffer and usLength be done 
     *       in the host driver? */

    /* Fill in SPI header */
    pUserBuffer[CC3000_SPI_INDEX_OP] = CC3000_SPI_OP_WRITE;
    pUserBuffer[CC3000_SPI_INDEX_LEN_MSB] = ((usLength) & 0xFF00) >> 8;
    pUserBuffer[CC3000_SPI_INDEX_LEN_LSB] = ((usLength) & 0x00FF);
    pUserBuffer[CC3000_SPI_INDEX_BUSY_1] = CC3000_SPI_BUSY;
    pUserBuffer[CC3000_SPI_INDEX_BUSY_2] = CC3000_SPI_BUSY;

    usLength += SPI_HEADER_SIZE;

    if (wlan_tx_buffer[CC3000_SPI_TX_MAGIC_INDEX] != CC3000_SPI_MAGIC_NUMBER)
    {
        CHIBIOS_CC3000_DBG_PRINT("Buffer overflow detected.", NULL);
        while(1);
    }

    if (spiInformation.spiState == SPI_STATE_POWERUP)
    {
        while (spiInformation.spiState != SPI_STATE_INITIALIZED);
    }

    if (spiInformation.spiState == SPI_STATE_INITIALIZED)
    {
        SpiFirstWrite(pUserBuffer, usLength);
    }
    else
    {
        /* We need to prevent here race that can occur in case 2 back to back
         * packets are sent to the device, so the state will move to IDLE and
         * once again to not IDLE due to IRQ */
        tSLInformation.WlanInterruptDisable();

        while (spiInformation.spiState != SPI_STATE_IDLE);  /* TODO sleep */

        setSpiState(SPI_STATE_WRITE_REQUESTED);
        spiInformation.pTxPacket = pUserBuffer;
        spiInformation.txPacketLength = usLength;

        /* Assert the CS line and wait till SSI IRQ line is active and then
         * initialize write operation*/
        selectCC3000();

        /*Re-enable IRQ */
        tSLInformation.WlanInterruptEnable();
        chThdYield();

        while (spiInformation.spiState != SPI_STATE_WRITE_PERMITTED); /* TODO sleep */

        SpiWriteDataSynchronous(spiInformation.pTxPacket,
                                spiInformation.txPacketLength);

        setSpiState(SPI_STATE_IDLE);

        unselectCC3000(); 
    }

    /* Due to the fact that we are currently implementing a blocking situation
       here we will wait till end of transaction.*/
    while (SPI_STATE_IDLE != spiInformation.spiState);
}


/** @brief Registered callback to wlan_init() to read from IRQ pin.*/
static long cbReadWlanInterruptPin(void)
{
    return palReadPad(CHIBIOS_CC3000_IRQ_PORT, CHIBIOS_CC3000_IRQ_PAD);
}


/** @brief Registered callback to wlan_init() to write to WLAN EN pin.
 *  @param val Value to set Wlan pin. */
static void cbWriteWlanPin(unsigned char val)
{
    if (val)
    {
        palSetPad(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD);
    }
    else
    {
        palClearPad(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD);
    }
}


/** @brief Registered callback to wlan_init() to defer interrupt handling.
 *  @details This function is responsible for deferring the processing
 *           of any received interrupt from the CC3000 until #SpiResumeSpi()
 *           is called.
 *  @todo TI issue: This is called from the CC3000HostDriver, even though there
 *        is a perfectly usable function pointer registered. */
static void SpiPauseSpi(void)
{
#if 0
    if (spiPaused != false)
    {
        port_halt();
    }
#endif
    chSysLock();
    spiPaused = true;
    chSysUnlock();
}


/** @brief Registered callback to wlan_init() to enable the interrupt handling.
 *  @details This function will enable interrupt processing to occur for
 *           interrupts received between a call to #SpiPauseSpi() and this. */
void SpiResumeSpi(void)
{
#if 0
    if (spiPaused != true)
    {
        port_halt();
    }
#endif
    chSysLock();
    spiPaused = false;
    chSysUnlock();
}


/** @brief  To be used instead of wlan_init().
 *  @details Responsible for storing drivers and their configurations, then 
 *  calling wlan_init(). Hardware should be correctly configured before calling
 *  this function:
 *      * GPIO #CHIBIOS_CC3000_IRQ_PAD - input.
 *      * GPIO #CHIBIOS_CC3000_MISO_PAD - input.
 *      * GPIO #CHIBIOS_CC3000_NSS_PAD - output.
 *      * GPIO #CHIBIOS_CC3000_SCK_PAD - output.
 *      * GPIO #CHIBIOS_CC3000_MOSI_PAD - output.
 *      * GPIO #CHIBIOS_CC3000_WLAN_EN_PAD - output.
 *      * SPI driver - initialised (@p initialisedSpiDriver).
 *      * SPI config - desired hardware configuration set (@p configuredSpi).
 *      * EXT driver - initialised (@p initialisedExtDriver).
 *      * EXT config - no specifc configuration required (@p configuredExt).
 *
 *  See @ref hardware_setup_stm32l152.c for an example of the hardware setup
 *  on a STM32 platform.
 *
 *  @warning All pointers marked [in,out] are expected to remain in memory i.e.
 *           they should be global or similar.
 *  @warning Ensure configuration in cc3000_chibios_config.h is correct before
 *           calling this function.
 *
 *  @param[in,out] initialisedSpiDriver A pointer to an already initialised 
 *                 ChibiOS SPI Driver.
 *  @param[in] configuredSpi A ChibiOS SPIConfig structure that at a minimum has 
 *             any hardware registers correctly configured. This will be copied
 *             to an internal structure.
 *  @param[in,out] initialisedExtDriver A pointer to an already initialised 
 *                 ChibiOS EXT driver. It will be started in this function.
 *  @param[in,out] configuredExt A pointer to the existing EXT config that will
 *                 be updated with the IRQ channel.
 *  @param[in] sFWPatches See TI's documentation for wlan_init().
 *  @param[in] sDriverPatches See TI's documentation for wlan_init().
 *  @param[in] sBootLoaderPatches See TI's documentation for wlan_init().
 *  */
void cc3000ChibiosWlanInit(SPIDriver * initialisedSpiDriver,
                           SPIConfig * configuredSpi,
                           EXTDriver * initialisedExtDriver,
                           EXTConfig * configuredExt,
                           tFWPatches sFWPatches,
                           tDriverPatches sDriverPatches,
                           tBootLoaderPatches sBootLoaderPatches)
{
    /* Hold the SPI Driver to be used */
    chSpiDriver = initialisedSpiDriver;

    /* Copy the preconfigured spi config structure - ensures we get any
     * hardware dependant registers. */
    memcpy(&chSpiConfig, configuredSpi, sizeof(chSpiConfig));

    /* Store the EXT Driver */
    chExtDriver = initialisedExtDriver;

    /* Use configured SPI information. */
    chSpiConfig.end_cb = NULL;
    chSpiConfig.ssport = CHIBIOS_CC3000_PORT;
    chSpiConfig.sspad = CHIBIOS_CC3000_NSS_PAD;
    
    /* Setup EXT - only want to stop it once. */
    chExtConfig = configuredExt;
    extStop(chExtDriver);
    chExtConfig->channels[CHIBIOS_CC3000_IRQ_PAD].mode =
                                                 EXT_CH_MODE_FALLING_EDGE |
                                                 CHIBIOS_CC3000_IRQ_EXT_MODE;
    chExtConfig->channels[CHIBIOS_CC3000_IRQ_PAD].cb = cc3000ExtCb;
    extStart(chExtDriver, chExtConfig);
    
    chSemInit(&irqSem, 0);

    (void)chThdCreateStatic(irqSignalHandlerThreadWorkingArea,
                            sizeof(irqSignalHandlerThreadWorkingArea),
                            CHIBIOS_CC3000_IRQ_THD_PRIO,
                            irqSignalHandlerThread, NULL);

    /* Ensure the enable pin is low and CC3000 is off */
    palClearPad(CHIBIOS_CC3000_WLAN_EN_PORT, CHIBIOS_CC3000_WLAN_EN_PAD);
    chThdSleep(MS2ST(100));

    wlan_init(chibiosCc3000AsyncCb, sFWPatches, sDriverPatches, 
              sBootLoaderPatches, cbReadWlanInterruptPin, 
              SpiResumeSpi, SpiPauseSpi, cbWriteWlanPin);
}

