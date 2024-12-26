<!-- 
* @Author: Gobinath
* @Date: 2024-12-26 13:00:00
* @Description:  Deepstream Identity Plugin 
* @LastEditors: Gobinath
* @LastEditTime: 2024-12-26 13:00:00
-->
# Deepstream-Identity-Plugin
Identity Plugin using Deepstream

## Pre-requisites:
- GStreamer-1.0 Development package
- GStreamer-1.0 Base Plugins Development package
- Linux: Driver version 470.57.02 or higher
- CUDA 12.1 or 11.4
- Deepstream 6.3 or 6.0.1

## Description:
  1. This a implementation of Identity Plugin in Deepstream which accepts only RGBA Buffers. This plugin eliminates the need for tiler,nvosd pluigin at pipline end. 
  2. To convert Input buffer to  RGBA format a nvvideoconvert plugin should added before this plugin.
  
  
## Usage:
  1) ```cd ds_6.0``` for deepstream-6.0  ```cd ds_6.1``` for deepstream-6.0 
  2) Run ```install.sh``` to install and test plugin
  
  

