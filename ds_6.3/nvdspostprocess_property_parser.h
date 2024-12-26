/**
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef NVDSPOSTPROCESS_PROPERTY_FILE_PARSER_H_
#define NVDSPOSTPROCESS_PROPERTY_FILE_PARSER_H_

#include <gst/gst.h>
#include "gstnvdspostprocess.h"

/**
 * This file describes the Macro defined for config file property parser.
 */

/** max string length */
#define _PATH_MAX 4096

#define NVDSPOSTPROCESS_PROPERTY "property"
#define NVDSPOSTPROCESS_PROPERTY_ENABLE "enable"
#define NVDSPOSTPROCESS_PROPERTY_OBJECT_IDS "object_ids"

#define NVDSPOSTPROCESS_PROPERTY_CUSTOM_LIB_NAME "custom-lib-path"
#define NVDSPOSTPROCESS_PROPERTY_TENSOR_PREPARATION_FUNCTION "custom-tensor-preparation-function"

#define NVDSPOSTPROCESS_USER_CONFIGS "user-configs1"

#define NVDSPOSTPROCESS_GROUP "source-"
#define NVDSPOSTPROCESS_GROUP_ZONE_IDS "zone_ids"
#define NVDSPOSTPROCESS_GROUP_FCM_FACTOR "fcm_factor"
#define NVDSPOSTPROCESS_GROUP_ZONE_CORDS "zone_cords-"
#define NVDSPOSTPROCESS_GROUP_ZONE_APPROACH "zone_approach-"
#define NVDSPOSTPROCESS_GROUP_REMOVE_UNCOUNTED "remove_uncounted"

#define NVDSPOSTPROCESS_GROUP_CUSTOM_INPUT_PREPROCESS_FUNCTION "custom-input-transformation-function"

/**
 
 *
 * @param nvdspostprocess pointer to GstNvDsPostProcess structure
 *
 * @param cfg_file_path config file path
 *
 * @return boolean denoting if successfully parsed config file
 */
gboolean
nvdspostprocess_parse_config_file (GstNvDsPostProcess *nvdspostprocess, gchar *cfg_file_path);

#endif /* NVDSPOSTPROCESS_PROPERTY_FILE_PARSER_H_ */
