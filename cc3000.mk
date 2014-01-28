# CC3000_CHIBIOS_DIR - to be defined externally in main makefile. Path to 
# the directory containing this file.

# Name of directory containing the CC3000 Host Driver
CC3000_HOST_DIR=CC3000HostDriver

# Append to CSRC
CC3000SRC=$(CC3000_CHIBIOS_DIR)/src/cc3000_spi.c \
		  $(CC3000_CHIBIOS_DIR)/src/async_handler.c \
		  $(wildcard $(CC3000_CHIBIOS_DIR)/$(CC3000_HOST_DIR)/*.c) 


# Append to INCDIR
CC3000INC=$(CC3000_CHIBIOS_DIR)/src \
		  $(CC3000_CHIBIOS_DIR)/api \
		  $(CC3000_CHIBIOS_DIR)/$(CC3000_HOST_DIR)

