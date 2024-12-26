################################################################################
# Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#################################################################################

# compile with opencv to dump ROIs
WITH_OPENCV:=0

CUDA_VER?=
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER CUDA Version is not set")
endif

DS_VER?=
ifeq ($(DS_VER),)
  $(error "DS_VER Deepstream version is not set")
endif

TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)

CXX:= g++

SRCS:= gstnvdspostprocess.cpp nvdspostprocess_property_parser.cpp

INCS:= $(wildcard *.h)
LIB:=libnvdsgst_postprocess.so

NVDS_VERSION:=$DS_VER

CFLAGS+= -fPIC -DHAVE_CONFIG_H -std=c++17 -Wall -Werror -DDS_VERSION=\"$(DS_VER)\" \
	 -I /usr/local/cuda-$(CUDA_VER)/include \
	 -I include \
	 -I /opt/nvidia/deepstream/deepstream-$(DS_VER)/sources/includes \
	 -I /usr/local/include

GST_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(DS_VER)/lib/gst-plugins/
LIB_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(DS_VER)/lib/

LIBS := -shared -Wl,-no-undefined \
	-L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart -ldl \
	-L$(LIB_INSTALL_DIR) -lnvdsgst_helper -lnvdsgst_meta -lnvds_meta -lnvbufsurface -lnvbufsurftransform\
	-lcuda -Wl,-rpath,$(LIB_INSTALL_DIR)  
	


OBJS:= $(SRCS:.cpp=.o)

PKGS:= gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0



CFLAGS+=$(shell pkg-config --cflags $(PKGS))
LIBS+=$(shell pkg-config --libs $(PKGS))

all: $(LIB)

%.o: %.cpp $(INCS) Makefile
	@echo $(CFLAGS)
	$(CXX) -c -o $@ $(CFLAGS) $<

$(LIB): $(OBJS) $(DEP) Makefile
	@echo $(CFLAGS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

install: $(LIB)
	cp -rv $(LIB) $(GST_INSTALL_DIR)

clean:
	rm -rf $(OBJS) $(LIB)
