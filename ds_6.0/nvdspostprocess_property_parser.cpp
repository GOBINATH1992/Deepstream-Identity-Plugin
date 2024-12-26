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

#include <iostream>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "nvdspostprocess_property_parser.h"

GST_DEBUG_CATEGORY (NVDSPOSTPROCESS_CFG_PARSER_CAT);

#define PARSE_ERROR(details_fmt,...) \
  G_STMT_START { \
    GST_CAT_ERROR (NVDSPOSTPROCESS_CFG_PARSER_CAT, \
        "Failed to parse config file %s: " details_fmt, \
        cfg_file_path, ##__VA_ARGS__); \
    GST_ELEMENT_ERROR (nvdspostprocess, LIBRARY, SETTINGS, \
        ("Failed to parse config file:%s", cfg_file_path), \
        (details_fmt, ##__VA_ARGS__)); \
    goto done; \
  } G_STMT_END

#define CHECK_IF_PRESENT(error, custom_err) \
  G_STMT_START { \
    if (error && error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) { \
      std::string errvalue = "Error while setting property, in group ";  \
      errvalue.append(custom_err); \
      PARSE_ERROR ("%s %s", errvalue.c_str(), error->message); \
    } \
  } G_STMT_END

#define CHECK_ERROR(error, custom_err) \
  G_STMT_START { \
    if (error) { \
      std::string errvalue = "Error while setting property, in group ";  \
      errvalue.append(custom_err); \
      PARSE_ERROR ("%s %s", errvalue.c_str(), error->message); \
    } \
  } G_STMT_END

#define CHECK_BOOLEAN_VALUE(prop_name,value) \
  G_STMT_START { \
    if ((gint) value < 0 || value > 1) { \
      PARSE_ERROR ("Boolean property '%s' can have values 0 or 1", prop_name); \
    } \
  } G_STMT_END

#define CHECK_INT_VALUE_NON_NEGATIVE(prop_name,value, group) \
  G_STMT_START { \
    if ((gint) value < 0) { \
      PARSE_ERROR ("Integer property '%s' in group '%s' can have value >=0", prop_name, group); \
    } \
  } G_STMT_END

#define CHECK_INT_VALUE_RANGE(prop_name,value, group, min, max) \
  G_STMT_START { \
    if ((gint) value < min || (gint)value > max) { \
      PARSE_ERROR ("Integer property '%s' in group '%s' can have value >=%d and <=%d", \
      prop_name, group, min, max); \
    } \
  } G_STMT_END

#define GET_BOOLEAN_PROPERTY(group, property, field) {\
  field = g_key_file_get_boolean(key_file, group, property, &error); \
  CHECK_ERROR(error, group); \
}

#define GET_UINT_PROPERTY(group, property, field) {\
  field = g_key_file_get_integer(key_file, group, property, &error); \
  CHECK_ERROR(error, group); \
  CHECK_INT_VALUE_NON_NEGATIVE(property,\
                               field, group);\
}

#define GET_STRING_PROPERTY(group, property, field) {\
  field = g_key_file_get_string(key_file, group, property, &error); \
  CHECK_ERROR(error, group); \
}

#define READ_UINT_PROPERTY(group, property, field) {\
  field = g_key_file_get_integer(key_file, group, property, &error); \
  CHECK_ERROR(error, group); \
  CHECK_INT_VALUE_NON_NEGATIVE(property,\
                               field, group);\
}

#define EXTRACT_STREAM_ID(for_key){\
      gchar **tmp; \
      gchar *endptr1; \
      source_index = 0; \
      tmp = g_strsplit(*for_key, "-", 5); \
      /*g_print("**** %s &&&&&&\n", tmp[g_strv_length(tmp)-1]);*/ \
      source_index = g_ascii_strtoull(tmp[g_strv_length(tmp)-1], &endptr1, 10); \
}

#define EXTRACT_ZONE_ID(for_key){\
      gchar **tmp; \
      gchar *endptr1; \
      zone_index = 0; \
      tmp = g_strsplit(*for_key, "-", 2); \
      /*g_print("**** %s &&&&&&\n", tmp[g_strv_length(tmp)-1]);*/ \
      zone_index = g_ascii_strtoull(tmp[g_strv_length(tmp)-1], &endptr1, 10); \
}


#define EXTRACT_GROUP_ID(for_group){\
      gchar *group1 = *group + sizeof (for_group) - 1; \
      gchar *endptr; \
      group_index = g_ascii_strtoull (group1, &endptr, 10); \
}

//sum total of ROIs of all the groups
gint sum_total_rois = 0;

static gboolean
nvdspostprocess_parse_property_group (GstNvDsPostProcess *nvdspostprocess,
    gchar *cfg_file_path, GKeyFile *key_file, gchar *group);

static gboolean
nvdspostprocess_parse_common_group (GstNvDsPostProcess *nvdspostprocess,
    gchar *cfg_file_path, GKeyFile *key_file, gchar *group, guint64 group_id);

static gboolean
nvdspostprocess_parse_user_configs(GstNvDsPostProcess *nvdspostprocess,
    gchar *cfg_file_path, GKeyFile *key_file, gchar *group);

/* Get the absolute path of a file mentioned in the config given a
 * file path absolute/relative to the config file. */
static gboolean
get_absolute_file_path (
    const gchar * cfg_file_path, const gchar * file_path,
    char *abs_path_str)
{
  gchar abs_cfg_path[_PATH_MAX + 1];
  gchar abs_real_file_path[_PATH_MAX + 1];
  gchar *abs_file_path;
  gchar *delim;

  /* Absolute path. No need to resolve further. */
  if (file_path[0] == '/') {
    /* Check if the file exists, return error if not. */
    if (!realpath (file_path, abs_real_file_path)) {
      return FALSE;
    }
    g_strlcpy (abs_path_str, abs_real_file_path, _PATH_MAX);
    return TRUE;
  }

  /* Get the absolute path of the config file. */
  if (!realpath (cfg_file_path, abs_cfg_path)) {
    return FALSE;
  }

  /* Remove the file name from the absolute path to get the directory of the
   * config file. */
  delim = g_strrstr (abs_cfg_path, "/");
  *(delim + 1) = '\0';

  /* Get the absolute file path from the config file's directory path and
   * relative file path. */
  abs_file_path = g_strconcat (abs_cfg_path, file_path, nullptr);

  /* Resolve the path.*/
  if (realpath (abs_file_path, abs_real_file_path) == nullptr) {
    /* Ignore error if file does not exist and use the unresolved path. */
    if (errno == ENOENT)
      g_strlcpy (abs_real_file_path, abs_file_path, _PATH_MAX);
    else
      return FALSE;
  }

  g_free (abs_file_path);

  g_strlcpy (abs_path_str, abs_real_file_path, _PATH_MAX);
  return TRUE;
}

static gboolean
nvdspostprocess_parse_property_group (GstNvDsPostProcess *nvdspostprocess,
    gchar *cfg_file_path, GKeyFile *key_file, gchar *group)
{
  g_autoptr(GError)error = nullptr;
  gboolean ret = FALSE;
  g_auto(GStrv)keys=nullptr;
  GStrv key=nullptr;
  gint *object_ids_list = nullptr;
  gsize object_ids_list_len = 0;

  keys = g_key_file_get_keys (key_file, group, nullptr, &error);
  CHECK_ERROR(error, group);

  for (key = keys; *key; key++){
    if (!g_strcmp0 (*key, NVDSPOSTPROCESS_PROPERTY_ENABLE)) {
      gboolean val = g_key_file_get_boolean(key_file, group,
          NVDSPOSTPROCESS_PROPERTY_ENABLE, &error);
      CHECK_ERROR(error, group);
      nvdspostprocess->enable = val;
    }
    
    else if (!g_strcmp0 (*key, NVDSPOSTPROCESS_PROPERTY_OBJECT_IDS)) {
      object_ids_list = g_key_file_get_integer_list (key_file, group,*key, &object_ids_list_len, &error);
      if (object_ids_list == nullptr) {
        CHECK_ERROR(error, group);
      }
      nvdspostprocess->object_ids.clear();
      for (gsize icnt = 0; icnt < object_ids_list_len; icnt++){
        nvdspostprocess->object_ids.push_back(object_ids_list[icnt]);
        GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed '%s=%d' in group '%s'\n",
          *key, object_ids_list[icnt], group);
      }
      g_free(object_ids_list);
      object_ids_list = nullptr;
      nvdspostprocess->property_set.object_ids = TRUE;
    }
    
    else if (!g_strcmp0(*key, NVDSPOSTPROCESS_PROPERTY_CUSTOM_LIB_NAME)) {
      gchar *str = g_key_file_get_string (key_file, group, *key, &error);
      nvdspostprocess->custom_lib_path = new gchar[_PATH_MAX];
      if (!get_absolute_file_path (cfg_file_path, str, nvdspostprocess->custom_lib_path)) {
        g_printerr ("Error: Could not parse custom lib path\n");
        g_free (str);
        ret = FALSE;
        delete[] nvdspostprocess->custom_lib_path;
        goto done;
      }
      g_free (str);
      GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed %s=%s in group '%s'\n",
          *key, nvdspostprocess->custom_lib_path, group);
      nvdspostprocess->property_set.custom_lib_path = TRUE;
    }
    else if (!g_strcmp0(*key, NVDSPOSTPROCESS_PROPERTY_TENSOR_PREPARATION_FUNCTION)) {
      GET_STRING_PROPERTY(group, *key, nvdspostprocess->custom_tensor_function_name);
      GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed %s=%s in group '%s'\n",
          *key, nvdspostprocess->custom_tensor_function_name, group);
      nvdspostprocess->property_set.custom_tensor_function_name = TRUE;
    }
  }

  if (!(nvdspostprocess->property_set.object_ids &&
      nvdspostprocess->property_set.custom_lib_path &&
      nvdspostprocess->property_set.custom_tensor_function_name)) {
    printf("ERROR: Some postprocess config properties not set\n");
    return FALSE;
  }

  
  GST_DEBUG_OBJECT (nvdspostprocess, "Custom Lib = %s\n Custom Tensor Preparation Function = %s\n",
          nvdspostprocess->custom_lib_path, nvdspostprocess->custom_tensor_function_name);

  ret = TRUE;

done:
  return ret;
}

static gboolean
nvdspostprocess_parse_common_group (GstNvDsPostProcess *nvdspostprocess,
    gchar *cfg_file_path, GKeyFile *key_file, gchar *group, guint64 group_id)
{
  g_autoptr(GError)error = nullptr;
  gboolean ret = FALSE;
  g_auto(GStrv)keys=nullptr;
  GStrv key=nullptr;
  guint64 zone_index=0;
  gint *roi_list = nullptr;
  gint *zone_list = nullptr;
  std::vector <gint> zone_ids;
  gsize roi_list_len = 0;
  gsize zone_list_len = 0;
  gint num_point_per_zone = 0;
  GstNvDsPostProcessGroup *postprocess_group;
  Points pts;
  std::vector <gdouble> zone_color;
  std::vector <gint> zone_approach;
  postprocess_group = new GstNvDsPostProcessGroup;
  //postprocess_group->points;
  postprocess_group->src_id = group_id;
  keys = g_key_file_get_keys (key_file, group, nullptr, &error);
  CHECK_ERROR(error, group);

  for (key = keys; *key; key++){
    
    if (!g_strcmp0(*key, NVDSPOSTPROCESS_GROUP_ZONE_IDS)) {
      zone_list = g_key_file_get_integer_list (key_file, group,*key, &zone_list_len, &error);
      if (zone_list == nullptr) {
        CHECK_ERROR(error, group);
      }
      zone_ids.clear();
      for (gsize icnt = 0; icnt < zone_list_len; icnt++){
        zone_ids.push_back(zone_list[icnt]);
        GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed '%s=%d' in group '%s'\n",
          *key, zone_list[icnt], group);
      }
      postprocess_group->zone_ids = zone_ids;
      nvdspostprocess->property_set.zone_ids = TRUE;
      g_free(zone_list);
      zone_list = nullptr;
    }
    else if (!g_strcmp0(*key, NVDSPOSTPROCESS_GROUP_CUSTOM_INPUT_PREPROCESS_FUNCTION)) {
      GET_STRING_PROPERTY(group, *key, postprocess_group->custom_transform_function_name);
      GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed %s=%s in group '%s'\n",
            NVDSPOSTPROCESS_GROUP_CUSTOM_INPUT_PREPROCESS_FUNCTION,
            postprocess_group->custom_transform_function_name, group);
      GST_DEBUG_OBJECT(nvdspostprocess, "Custom Transformation Function = %s\n",
            postprocess_group->custom_transform_function_name);
    }
    else  if (!g_strcmp0 (*key, NVDSPOSTPROCESS_PROPERTY_ENABLE)) {
      gboolean val = g_key_file_get_boolean(key_file, group, *key, &error);
      CHECK_ERROR(error, group);
      postprocess_group->enable = val;
      GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed %s=%d in group '%s'\n",
            *key, postprocess_group->enable, group);
      nvdspostprocess->property_set.enable = TRUE;
    }
    else if (!strncmp(*key, NVDSPOSTPROCESS_GROUP_ZONE_CORDS,
      sizeof(NVDSPOSTPROCESS_GROUP_ZONE_CORDS)-1) && postprocess_group->enable) {
        EXTRACT_ZONE_ID(key);
        roi_list = g_key_file_get_integer_list (key_file, group,*key, &roi_list_len, &error);
        if (roi_list == nullptr) {
          CHECK_ERROR(error, group);
        }
        /** check if multiple of 2 */
        if (((roi_list_len-3) & 1) == 0) {
          num_point_per_zone = (int)((roi_list_len-3)/2);
        } else {
          printf ("ERROR: %s : roi list length for zone %d is not a multiple of 2\n",
                __func__, (int)zone_index);
          goto done;
        }
        
        GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsing zone-cords zone_index = %ld num-point = %d roilistlen = %ld\n",
            zone_index, num_point_per_zone, roi_list_len);

        for (guint i = 0; i < roi_list_len-3; i=i+2) {
          Point pt;
          pt.x = roi_list[i];
          pt.y = roi_list[i+1];
          GST_DEBUG ("parsed Point x=%f y=%f \n",
            (double)pt.x, (double)pt.y);
          pts.push_back(pt);
        }
        zone_color.push_back(roi_list[roi_list_len-3]/255.0);
        zone_color.push_back(roi_list[roi_list_len-2]/255.0);
        zone_color.push_back(roi_list[roi_list_len-1]/255.0);

        if (zone_index < 0) {
          GST_ELEMENT_WARNING (nvdspostprocess, RESOURCE, FAILED,
            ("Only zone-0 will get used\n"), (nullptr));
        } 
        else {
          postprocess_group->zone_pts.push_back(pts) ; /*Push zone pts to global vector     */
          postprocess_group->zone_color.push_back(zone_color);  /*push zone color to global vector*/
        }

        GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed '%s' in group '%s'\n",
          NVDSPOSTPROCESS_GROUP_ZONE_CORDS, group);
        nvdspostprocess->property_set.zone_cords = TRUE;

        g_free(roi_list);
        roi_list = nullptr;
        pts.clear();
        zone_color.clear();
    }


    else if (!strncmp(*key, NVDSPOSTPROCESS_GROUP_ZONE_APPROACH,
      sizeof(NVDSPOSTPROCESS_GROUP_ZONE_APPROACH)-1) && postprocess_group->enable) {
        EXTRACT_ZONE_ID(key);
        gint approach = g_key_file_get_integer (key_file, group,*key, &error);
        if (approach <0) {
          CHECK_ERROR(error, group);
        }
        
        GST_DEBUG ("Parsing zone-approach zone_index = %ld approach = %d\n",
            zone_index, approach);
       
        if (zone_index < 0) {
          GST_ELEMENT_WARNING (nvdspostprocess, RESOURCE, FAILED,
            ("Only zone-0 will get used\n"), (nullptr));
        } 
        else {
          postprocess_group->zone_approach.push_back(approach) ;
          
        }

        GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed '%s' in group '%s'\n",
          NVDSPOSTPROCESS_GROUP_ZONE_APPROACH, group);
        nvdspostprocess->property_set.zone_approach = TRUE;

        
    }

    else  if (!g_strcmp0 (*key, NVDSPOSTPROCESS_GROUP_REMOVE_UNCOUNTED)) {
      gboolean val = g_key_file_get_boolean(key_file, group, *key, &error);
      CHECK_ERROR(error, group);
      postprocess_group->remove_uncounted = val;
      GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed %s=%d in group '%s'\n",
            *key, postprocess_group->enable, group);
      nvdspostprocess->property_set.remove_uncounted = TRUE;
    } 
    else  if (!g_strcmp0 (*key, NVDSPOSTPROCESS_GROUP_FCM_FACTOR)) {
      double val = g_key_file_get_double(key_file, group, *key, &error);
      CHECK_ERROR(error, group);
      postprocess_group->fcm_factor = val;
      GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Parsed %s=%d in group '%s'\n",
            *key, postprocess_group->enable, group);
      nvdspostprocess->property_set.fcm_factor = TRUE;
    } 







  }

  nvdspostprocess->nvdspostprocess_groups.push_back(postprocess_group);

  if (postprocess_group->enable) {
    if (!(nvdspostprocess->property_set.zone_ids &&
        nvdspostprocess->property_set.fcm_factor &&
        nvdspostprocess->property_set.zone_approach &&
        nvdspostprocess->property_set.remove_uncounted &&
        nvdspostprocess->property_set.zone_cords)) {
      printf("ERROR: Some postprocess group config properties not set\n");
      return FALSE;
    }
  }
  
  ret = TRUE;

done:
  return ret;
}

static gboolean
nvdspostprocess_parse_user_configs(GstNvDsPostProcess *nvdspostprocess,
    gchar *cfg_file_path, GKeyFile *key_file, gchar *group)
{
  g_autoptr(GError)error = nullptr;
  gboolean ret = FALSE;
  g_auto(GStrv)keys=nullptr;
  GStrv key=nullptr;
  std::unordered_map<std::string, std::string> user_configs;

  keys = g_key_file_get_keys (key_file, group, nullptr, &error);
  CHECK_ERROR(error, group);

  for (key = keys; *key; key++){
    std::string val = g_key_file_get_string(key_file, group, *key, &error);
    GST_DEBUG_OBJECT ("parsed user-config key = %s value = %s\n", *key, val.c_str());
    CHECK_ERROR(error, group);
    //user_configs.emplace(std::string(*key), val);
  }
  //nvdspostprocess->custom_initparams.user_configs = user_configs;
  ret = TRUE;

done:
  return ret;
}

/* Parse the nvdspostprocess config file. Returns FALSE in case of an error. */
gboolean
nvdspostprocess_parse_config_file (GstNvDsPostProcess * nvdspostprocess, gchar * cfg_file_path)
{
  g_autoptr(GError)error = nullptr;
  gboolean ret = FALSE;
  g_auto(GStrv)groups=nullptr;
  GStrv group;
  g_autoptr(GKeyFile) cfg_file = g_key_file_new ();
  guint64 group_index = 0;

  if (!NVDSPOSTPROCESS_CFG_PARSER_CAT) {
    GstDebugLevel  level;
    GST_DEBUG_CATEGORY_INIT (NVDSPOSTPROCESS_CFG_PARSER_CAT, "nvdspostprocess", 0,
        NULL);
    level = gst_debug_category_get_threshold (NVDSPOSTPROCESS_CFG_PARSER_CAT);
    if (level < GST_LEVEL_ERROR )
      gst_debug_category_set_threshold (NVDSPOSTPROCESS_CFG_PARSER_CAT, GST_LEVEL_ERROR);
  }

  if (!g_key_file_load_from_file (cfg_file, cfg_file_path, G_KEY_FILE_NONE,
          &error)) {
    PARSE_ERROR ("%s", error->message);
  }

  // Check if 'property' group present
  if (!g_key_file_has_group (cfg_file, NVDSPOSTPROCESS_PROPERTY)) {
    PARSE_ERROR ("Group 'property' not specified");
  }

  g_key_file_set_list_separator (cfg_file,';');

  groups = g_key_file_get_groups (cfg_file, nullptr);

  for (group = groups; *group; group++) {
    GST_CAT_INFO (NVDSPOSTPROCESS_CFG_PARSER_CAT, "Group found %s \n", *group);
    if (!strcmp(*group, NVDSPOSTPROCESS_PROPERTY)){
      ret = nvdspostprocess_parse_property_group(nvdspostprocess,
          cfg_file_path, cfg_file, *group);
      if (!ret){
        g_print("NVDSPOSTPROCESS_CFG_PARSER: Group '%s' parse failed\n", *group);
        goto done;
      }
    }
    else if (!strncmp(*group, NVDSPOSTPROCESS_GROUP,
            sizeof(NVDSPOSTPROCESS_GROUP)-1)){
      EXTRACT_GROUP_ID(NVDSPOSTPROCESS_GROUP);
      GST_DEBUG("parsing group index = %lu\n", group_index);
      ret = nvdspostprocess_parse_common_group (nvdspostprocess,
                cfg_file_path, cfg_file, *group, group_index);
      if (!ret){
        g_print("NVDSPOSTPROCESS_CFG_PARSER: Group '%s' parse failed\n", *group);
        goto done;
      }
    }
    else if (!strcmp(*group, NVDSPOSTPROCESS_USER_CONFIGS)){
      GST_DEBUG ("Parsing User Configs\n");
      ret = nvdspostprocess_parse_user_configs (nvdspostprocess,
                cfg_file_path, cfg_file, *group);
      if (!ret){
        g_print("NVDSPOSTPROCESS_CFG_PARSER: Group '%s' parse failed\n", *group);
        goto done;
      }
    }
    else {
      g_print("NVDSPOSTPROCESS_CFG_PARSER: Group '%s' ignored\n", *group);
    }
  }


done:
  return ret;
}