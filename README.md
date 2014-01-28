# ChibiOS/RT CC3000 SPI Driver

## About
This is a SPI driver to enable Texas Instruments' CC3000HostDriver to 
work with ChibiOS/RT. This was created as part of a final year project at
Queen's University Belfast.


## Setup
The CC3000HostDriver is not included with this repository. You can either:

1. Run prepare.sh to get the driver and fix some issues in the CC3000HostDriver.

or

1. Get the latest CC3000HostDriver and place it at the root of this repository.
2. Run prepare.sh to then fix some issues in the CC3000HostDriver.

See prepare.sh for more information on what it does. This is required if you 
don't have bash and need to execute these commands manually.


## Configuration
1. Copy ./config/cc3000_chibios_config.h into your project and ensure it is
   included in the build process.
2. Setup your hardware SPI configuration. This should be done before calling
   cc3000ChibiosWlanInit(). See the examples for an implementation of this 
   on a STM32 platform.
3. Edit cc3000_chibios_config.h to your requirements.
4. In your program you need to include cc3000_chibios_api.h and call
   cc3000ChibiosWlanInit() instead of wlan_init(). See the doxygen API 
   documentation for this library for more information on its use.


## Building
To use this, edit your project makefile (assuming the ChibiOS/RT skeleton 
Makefile used) with the following:

    #Set the path to library
    CC3000_CHIBIOS_DIR=<path_to_this_repo_from_your_project> 
    
    #Include the library makefile
    include $(CC3000_CHIBIOS_DIR)/cc3000.mk 
    
    #Append $(CC3000SRC) to CSRC list
    CSRC = $(PORTSRC) \   #... Existing CSRC list
           $(CC3000SRC) 
    
    #Append $(CC3000INC) to INCDIR list
    INCDIR = $(PORTINC) \ #... Existing INCDIR list
             $(CC3000INC)


## Compatibility Notes
This has been developed against ChibiOS/RT 2.6.x running on a STM32
(STM32L152RC). This library has been written in such a way that any hardware
dependant configuration should be made externally, prior to the call to 
cc3000ChibiosWlanInit().
This library depends however on HAL elements from ChibiOS/RT which may
not be available on all platforms. These are described below.


## ChibiOS/RT Features Used
* HAL EXT Driver
  * Interrupt Detect
* HAL SPI Driver
  * Communications
* HAL PAL Driver
  * Reading and writing to GPIO
* Static Thread
  * Processing interrupts
* Semaphore
  * Interrupt Signalling
* Mutex
  * Permit sharing of SPI driver (optional)


## Links
* [ChibiOS/RT CC3000 SPI Doxygen](http://alanbarr.github.io/ChibiOS_CC3000_SPI)
* [ChibiOS/RT CC3000 SPI Source](https://github.com/alanbarr/ChibiOS_CC3000_SPI)
* [TI's CC3000 Wiki](http://processors.wiki.ti.com/index.php/CC3000)
* [ChibiOS/RT Homepage](http://www.chibios.org/dokuwiki/doku.php)


## License
All files in this project are licensed under either the BSD 2-Clause license
or the BSD 3-Clause license.

