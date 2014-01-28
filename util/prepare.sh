#! /usr/bin/env bash

################################################################################
# Copyright (c) 2014, Alan Barr
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# 
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
################################################################################
#
# This file sets prepares the host driver for use with this repository. It 
# should only need to be executed once, from the root of the repository.
# It:
# * Gets the CC3000HostDriver from TI (if it is not already located in the root 
#   directory of the repository).
# * Fixes a problem with CC3000HostDriver/socket.h by including the
#   fix_defines.h header.
# * Renames all includes of spi.h to cc3000_spi.h within the host driver.
REPO=ChibiOS_CC3000_SPI
CC3000_HOST_DRIVER_DIR=CC3000HostDriver
CC3000_HOST_DRIVER_URL="http://www.ti.com/litv/zip/swrc263C"
CC3000_HOST_DRIVER_ZIP="temp.zip"

# Ensure this script is running from the correct location.
currentDir=${PWD##*/}
echo ${currentDir}
if [ ${currentDir} != "util" ] && [ ${currentDir} != ${REPO} ]; then
    echo "Please run this script from either: ${REPO} or ${REPO}/util."
    exit
fi
if [ ${currentDir} = "util" ]; then
    cd ../
fi

# If the directory doesn't exist go ahead and fetch it
if [ ! -d ${CC3000_HOST_DRIVER_DIR} ]; then
    echo "Directory called ${CC3000_HOST_DRIVER_DIR} doesn't exist."
    echo "Retrieving from ${CC3000_HOST_DRIVER_URL}..."
    wget --quiet --user-agent=Mozilla ${CC3000_HOST_DRIVER_URL} -O ${CC3000_HOST_DRIVER_ZIP}
    if [ $? -ne 0 ]; then
        echo "wget from $CC3000_HOST_DRIVER_URL failed."
        exit
    fi
    unzip -q ${CC3000_HOST_DRIVER_ZIP}
    rm ${CC3000_HOST_DRIVER_ZIP}
fi

# Fix problem with redefining definitions for sockets.h by including fix_defines.h
grep -q fix_defines ${CC3000_HOST_DRIVER_DIR}/socket.h
if [ $? -eq 0 ]; then
    echo "Has this script previously been ran? Exiting."
    exit
else
    echo "Adding #include \"fix_defines.h\" to socket.h"
    sed -i 's/\(\#define __SOCKET_H__\)/\1\n\#include \"fix_defines\.h\"/' ${CC3000_HOST_DRIVER_DIR}/socket.h
fi

# Rename includes of spi.h to cc3000_spi.h
echo "Changing spi.h include to cc3000_spi.h"
sed -i 's/\#include \"spi\.h\"/\#include \"cc3000_spi\.h\"/' ${CC3000_HOST_DRIVER_DIR}/*
