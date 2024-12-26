#!/bin/bash
###
 # @Author: Gobinath
 # @Date: 2024-12-26 13:00:00
 # @Description: 
 # @LastEditors: Gobinath
 # @LastEditTime: 2024-12-26 13:00:00
### 
set -eE 
red='\033[0;31m'
clear='\033[0m'
# Color variables
red='\033[0;31m'
green='\033[0;32m'
yellow='\033[0;33m'
blue='\033[0;34m'
magenta='\033[0;35m'
cyan='\033[0;36m'
# Clear the color after that
clear='\033[0m'
BOLD='\033[1m'
UNDERLINE='\033[4m'
#trap 'if [[ $? -eq 139 ]]; then echo "segfault !"; fi' CHLD

log() {
    local message="$1"
   
    current_time=$(date +"%Y-%m-%d %H:%M:%S")
    echo -e "${BOLD}[${current_time}] $message"
}



CUR_DIR=$PWD
DS_VER=6.3
NTHREAD=4
dir=$PWD
cuda_version=$(nvcc --version | grep -oP 'V\K\d+\.\d+')
log "[INFO]CUDA Version: $cuda_version" $green


export CUDA_VER=$cuda_version
export DS_VER=$DS_VER

make -j $NTHREAD
make install 


log "[INFO]Pluign Install : Sucess"
set +eE 


log "[INFO] Test Sucesss "

   
