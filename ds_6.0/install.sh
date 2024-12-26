#!/bin/bash
###
 # @Author: Gobinath
 # @Date: 2024-12-26 13:00:00
 # @Description: 
 # @LastEditors: Gobinath
 # @LastEditTime: 2024-12-26 13:00:00
### 
set -eE 

#trap 'if [[ $? -eq 139 ]]; then echo "segfault !"; fi' CHLD

log() {
    local message="$1"
   
    current_time=$(date +"%Y-%m-%d %H:%M:%S")
    echo -e "${BOLD}[${current_time}] $message"
}



CUR_DIR=$PWD
DS_VER=6.0
NTHREAD=4
dir=$PWD
cuda_version=$(nvcc --version | grep -oP 'V\K\d+\.\d+')
log "[INFO]CUDA Version: $cuda_version" 


export CUDA_VER=$cuda_version
export DS_VER=$DS_VER

make -j $NTHREAD
make install 


log "[INFO]Pluign Install : Sucess"
set +eE 

cp test.py  /opt/nvidia/deepstream/deepstream-$DS_VER/sources/deepstream_python_apps/apps/deepstream-test1
cp config_postprocess.txt  /opt/nvidia/deepstream/deepstream-$DS_VER/sources/deepstream_python_apps/apps/deepstream-test1
cd /opt/nvidia/deepstream/deepstream-$DS_VER/sources/deepstream_python_apps/apps/deepstream-test1
python3 test.py /opt/nvidia/deepstream/deepstream-$DS_VER/samples/streams/sample_720p.h264


log "[INFO] Test Sucesss "

   
