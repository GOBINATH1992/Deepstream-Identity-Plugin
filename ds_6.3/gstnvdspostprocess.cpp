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

#include <string.h>
#include <string>
#include <sstream>
#include <iostream>
#include <ostream>
#include <fstream>
#include <functional>
#include "nvdspostprocess_property_parser.h"
#include "gstnvdspostprocess.h"
#include <cmath>



#include <sys/time.h>
#include <condition_variable>
#include <mutex>
#include <thread>

GST_DEBUG_CATEGORY_STATIC (gst_nvdspostprocess_debug);
#define GST_CAT_DEFAULT gst_nvdspostprocess_debug
#define USE_EGLIMAGE 1






/* Enum to identify properties */
enum
{
  PROP_0,
  PROP_UNIQUE_ID,
  PROP_ENABLE,
  PROP_PROCESSING_WIDTH,
  PROP_PROCESSING_HEIGHT,
  PROP_GPU_DEVICE_ID,
  PROP_CONFIG_FILE
};

#define CHECK_NVDS_MEMORY_AND_GPUID(object, surface)  \
  ({ int _errtype=0;\
   do {  \
    if ((surface->memType == NVBUF_MEM_DEFAULT || surface->memType == NVBUF_MEM_CUDA_DEVICE) && \
        (surface->gpuId != object->gpu_id))  { \
    GST_ELEMENT_ERROR (object, RESOURCE, FAILED, \
        ("Input surface gpu-id doesnt match with configured gpu-id for element," \
         " please allocate input using unified memory, or use same gpu-ids"),\
        ("surface-gpu-id=%d,%s-gpu-id=%d",surface->gpuId,GST_ELEMENT_NAME(object),\
         object->gpu_id)); \
    _errtype = 1;\
    } \
    } while(0); \
    _errtype; \
  })

/* Default values for properties */
#define DEFAULT_UNIQUE_ID 15
#define DEFAULT_PROCESSING_WIDTH 640
#define DEFAULT_PROCESSING_HEIGHT 480
#define DEFAULT_GPU_ID 0
#define DEFAULT_BATCH_SIZE 1
#define DEFAULT_CONFIG_FILE_PATH ""
#define DEFAULT_SCALING_POOL_COMPUTE_HW NvBufSurfTransformCompute_Default
#define DEFAULT_SCALING_BUF_POOL_SIZE 6 /** Inter Buffer Pool Size for Scale & Converted ROIs */
#define DEFAULT_TENSOR_BUF_POOL_SIZE 6 /** Tensor Buffer Pool Size */

#define RGB_BYTES_PER_PIXEL 3
#define RGBA_BYTES_PER_PIXEL 4
#define Y_BYTES_PER_PIXEL 1
#define UV_BYTES_PER_PIXEL 2

#define MIN_INPUT_OBJECT_WIDTH 16
#define MIN_INPUT_OBJECT_HEIGHT 16

#define MAX_DISPLAY_LEN 64

#define CHECK_NPP_STATUS(npp_status,error_str) do { \
  if ((npp_status) != NPP_SUCCESS) { \
    g_print ("Error: %s in %s at line %d: NPP Error %d\n", \
        error_str, __FILE__, __LINE__, npp_status); \
    goto error; \
  } \
} while (0)

#define CHECK_CUDA_STATUS(cuda_status,error_str) do { \
  if ((cuda_status) != cudaSuccess) { \
    g_print ("Error: %s in %s at line %d (%s)\n", \
        error_str, __FILE__, __LINE__, cudaGetErrorName(cuda_status)); \
    goto error; \
  } \
} while (0)

/* By default NVIDIA Hardware allocated memory flows through the pipeline. We
 * will be processing on this type of memory only. */
#define GST_CAPS_FEATURE_MEMORY_NVMM "memory:NVMM"
static GstStaticPadTemplate gst_nvdspostprocess_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ RGBA}")));

static GstStaticPadTemplate gst_nvdspostprocess_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{  RGBA }")));

/* Define our element type. Standard GObject/GStreamer boilerplate stuff */
#define gst_nvdspostprocess_parent_class parent_class
G_DEFINE_TYPE (GstNvDsPostProcess, gst_nvdspostprocess, GST_TYPE_BASE_TRANSFORM);

static void gst_nvdspostprocess_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nvdspostprocess_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_nvdspostprocess_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_nvdspostprocess_start (GstBaseTransform * btrans);
static gboolean gst_nvdspostprocess_stop (GstBaseTransform * btrans);

static GstFlowReturn
gst_nvdspostprocess_submit_input_buffer (GstBaseTransform * btrans,
    gboolean discont, GstBuffer * inbuf);
static GstFlowReturn
gst_nvdspostprocess_generate_output (GstBaseTransform * btrans, GstBuffer ** outbuf);
//static gpointer gst_nvdspostprocess_output_loop (gpointer data);

template<class T>
  T* dlsym_ptr(void* handle, char const* name) {
    return reinterpret_cast<T*>(dlsym(handle, name));
}

/* Install properties, set sink and src pad capabilities, override the required
 * functions of the base class, These are common to all instances of the
 * element.
 */
static void
gst_nvdspostprocess_class_init (GstNvDsPostProcessClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  // Indicates we want to use DS buf api
  g_setenv ("DS_NEW_BUFAPI", "1", TRUE);

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  /* Overide base class functions */
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_nvdspostprocess_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_nvdspostprocess_get_property);

  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_nvdspostprocess_set_caps);
  gstbasetransform_class->start = GST_DEBUG_FUNCPTR (gst_nvdspostprocess_start);
  gstbasetransform_class->stop = GST_DEBUG_FUNCPTR (gst_nvdspostprocess_stop);

  gstbasetransform_class->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_nvdspostprocess_submit_input_buffer);
  gstbasetransform_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_nvdspostprocess_generate_output);

  /* Install properties */
  g_object_class_install_property (gobject_class, PROP_UNIQUE_ID,
      g_param_spec_uint ("unique-id",
          "Unique ID",
          "Unique ID for the element. Can be used to identify output of the"
          " element", 0, G_MAXUINT, DEFAULT_UNIQUE_ID, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ENABLE,
      g_param_spec_boolean ("enable", "Enable",
          "Enable gst-nvdspostprocess plugin, or set in passthrough mode",
          TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id",
          "Set GPU Device ID",
          "Set GPU Device ID", 0,
          G_MAXUINT, 0,
          GParamFlags
          (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property (gobject_class, PROP_CONFIG_FILE,
      g_param_spec_string ("config-file", "Preprocess Config File",
          "Preprocess Config File",
          DEFAULT_CONFIG_FILE_PATH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /* Set sink and src pad capabilities */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_nvdspostprocess_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_nvdspostprocess_sink_template));

  /* Set metadata describing the element */
  gst_element_class_set_details_simple (gstelement_class,
      "gst-nvdspostprocess plugin",
      "gst-nvdspostprocess plugin",
      "Preprocessing using custom algorithms for different streams",
      "NVIDIA Corporation. Post on Deepstream for Tesla forum for any queries "
      "@ https://devtalk.nvidia.com/default/board/209/");
}

static void
gst_nvdspostprocess_init (GstNvDsPostProcess * nvdspostprocess)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (nvdspostprocess);

  /* We will not be generating a new buffer. Just adding / updating
   * metadata. */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  /* We do not want to change the input caps. Set to passthrough. transform_ip
   * is still called. */
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);

  /* Initialize all property variables to default values */
  nvdspostprocess->unique_id = DEFAULT_UNIQUE_ID;
  nvdspostprocess->enable = TRUE;
  nvdspostprocess->gpu_id = DEFAULT_GPU_ID;
  nvdspostprocess->config_file_path = g_strdup (DEFAULT_CONFIG_FILE_PATH);
  nvdspostprocess->config_file_parse_successful = FALSE;
  
  
}

/* Function called when a property of the element is set. Standard boilerplate.
 */
static void
gst_nvdspostprocess_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (object);
  switch (prop_id) {
    case PROP_UNIQUE_ID:
      nvdspostprocess->unique_id = g_value_get_uint (value);
      break;
    case PROP_ENABLE:
      nvdspostprocess->enable = g_value_get_boolean (value);
      break;
    case PROP_GPU_DEVICE_ID:
      nvdspostprocess->gpu_id = g_value_get_uint (value);
      break;
    case PROP_CONFIG_FILE:
          {
        g_mutex_lock (&nvdspostprocess->postprocess_lock);
        g_free (nvdspostprocess->config_file_path);
        nvdspostprocess->config_file_path = g_value_dup_string (value);
        /* Parse the initialization parameters from the config file. This function
         * gives preference to values set through the set_property function over
         * the values set in the config file. */
        nvdspostprocess->config_file_parse_successful = TRUE;
          
        if (nvdspostprocess->config_file_parse_successful) {
          GST_DEBUG_OBJECT (nvdspostprocess, "Successfully Parsed Config file\n");
        }
        g_mutex_unlock (&nvdspostprocess->postprocess_lock);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Function called when a property of the element is requested. Standard
 * boilerplate.
 */
static void
gst_nvdspostprocess_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (object);

  switch (prop_id) {
    case PROP_UNIQUE_ID:
      g_value_set_uint (value, nvdspostprocess->unique_id);
      break;
    case PROP_ENABLE:
      g_value_set_boolean (value, nvdspostprocess->enable);
      break;
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, nvdspostprocess->gpu_id);
      break;
    case PROP_CONFIG_FILE:
      g_value_set_string (value, nvdspostprocess->config_file_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}




/**
 * Initialize all resources and start the process thread
 */
static gboolean
gst_nvdspostprocess_start (GstBaseTransform * btrans)
{
  GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (btrans);
  std::string nvtx_str;
  
  

  if (!nvdspostprocess->config_file_path || strlen (nvdspostprocess->config_file_path) == 0) {
    GST_ELEMENT_ERROR (nvdspostprocess, LIBRARY, SETTINGS,
        ("Configuration file not provided"), (nullptr));
    return FALSE;
  }

  if (nvdspostprocess->config_file_parse_successful == FALSE) {
    GST_ELEMENT_ERROR (nvdspostprocess, LIBRARY, SETTINGS,
        ("Configuration file parsing failed"),
        ("Config file path: %s", nvdspostprocess->config_file_path));
    return FALSE;
  }

  nvtx_str = "GstNvDsPostProcess: UID=" + std::to_string(nvdspostprocess->unique_id);
  auto nvtx_deleter = [](nvtxDomainHandle_t d) { nvtxDomainDestroy (d); };
  std::unique_ptr<nvtxDomainRegistration, decltype(nvtx_deleter)> nvtx_domain_ptr (
      nvtxDomainCreate(nvtx_str.c_str()), nvtx_deleter);


  GST_DEBUG_OBJECT (nvdspostprocess, "Initialized Custom Library Context\n");

  

  nvdspostprocess->nvtx_domain = nvtx_domain_ptr.release ();

  /* Create process queue to transfer data between threads.
   * We will be using this queue to maintain the list of frames/objects
   * currently given to the algorithm for processing. */
  nvdspostprocess->postprocess_queue = g_queue_new ();

 
  guint num_groups = 0;
  num_groups = nvdspostprocess->nvdspostprocess_groups.size();
  for (guint gcnt = 0; gcnt < num_groups; gcnt ++) {
    GstNvDsPostProcessGroup *& postprocess_group = nvdspostprocess->nvdspostprocess_groups[gcnt];
    if (!postprocess_group->enable) {
        continue;
      }
    


  }

  
  
  return TRUE;


}

/**
 * Stop the process thread and free up all the resources
 */
static gboolean
gst_nvdspostprocess_stop (GstBaseTransform * btrans)
{
  GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (btrans);

  //g_mutex_lock (&nvdspostprocess->postprocess_lock);

  /* Wait till all the items in the queue are handled. */
 // while (!g_queue_is_empty (nvdspostprocess->postprocess_queue)) {
 //   g_cond_wait (&nvdspostprocess->postprocess_cond, &nvdspostprocess->postprocess_lock);
  //}

  nvdspostprocess->stop = TRUE;

  //g_cond_broadcast (&nvdspostprocess->postprocess_cond);
  //g_mutex_unlock (&nvdspostprocess->postprocess_lock);

  //g_thread_join (nvdspostprocess->output_thread);

  

  g_queue_free (nvdspostprocess->postprocess_queue);

  if (nvdspostprocess->config_file_path) {
    g_free (nvdspostprocess->config_file_path);
    nvdspostprocess->config_file_path = NULL;
  }

  /* delete the heap allocated memory */
  for (auto &group : nvdspostprocess->nvdspostprocess_groups) {
    
    
    delete group;
    group = NULL;
  }
  
  /* Clean up the global context */
  
  return TRUE;
}

/**
 * Called when source / sink pad capabilities have been negotiated.
 */
static gboolean
gst_nvdspostprocess_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  //GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (btrans);
  /* Save the input video information, since this will be required later. */
  //gst_video_info_from_caps (&nvdspostprocess->video_info, incaps);

  
  return TRUE;


}




/* Process entire frames in the batched buffer. */
static GstFlowReturn
gst_nvdspostprocess_on_frame (GstNvDsPostProcess * nvdspostprocess, GstBuffer * inbuf,
    NvBufSurface * in_surf)
{
  //GstFlowReturn flow_ret = GST_FLOW_ERROR;
  std::string nvtx_str;

  NvDsBatchMeta *batch_meta = NULL;
  
  
  batch_meta = gst_buffer_get_nvds_batch_meta (inbuf);
  if (batch_meta == nullptr) {
    GST_ELEMENT_ERROR (nvdspostprocess, STREAM, FAILED,
        ("NvDsBatchMeta not found for input buffer."), (NULL));
    return GST_FLOW_ERROR;
  }

  
  
  

  return GST_FLOW_OK;
}

/**
 * Called when element recieves an input buffer from upstream element.
 */
static GstFlowReturn
gst_nvdspostprocess_submit_input_buffer (GstBaseTransform * btrans,
    gboolean discont, GstBuffer * inbuf)
{
  GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (btrans);
  GstMapInfo in_map_info;
  NvBufSurface *in_surf;
  GstFlowReturn flow_ret = GST_FLOW_ERROR;
  std::string nvtx_str;

  nvdspostprocess->current_batch_num++;

  nvtxEventAttributes_t eventAttrib = {0};
  eventAttrib.version = NVTX_VERSION;
  eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
  eventAttrib.colorType = NVTX_COLOR_ARGB;
  eventAttrib.color = 0xFFFF0000;
  eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
  nvtx_str = "buffer_process batch_num=" + std::to_string(nvdspostprocess->current_batch_num);
  eventAttrib.message.ascii = nvtx_str.c_str();
  nvtxRangeId_t buf_process_range = nvtxDomainRangeStartEx(nvdspostprocess->nvtx_domain, &eventAttrib);

  if (FALSE == nvdspostprocess->config_file_parse_successful) {
    GST_ELEMENT_ERROR (nvdspostprocess, LIBRARY, SETTINGS,
        ("Configuration file parsing failed\n"),
        ("Config file path: %s\n", nvdspostprocess->config_file_path));
    return flow_ret;
  }

  if (FALSE == nvdspostprocess->enable){
    GST_DEBUG_OBJECT (nvdspostprocess, "nvdspostprocess in passthrough mode\n");
    flow_ret = gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD (nvdspostprocess), inbuf);
    return flow_ret;
  }

  memset (&in_map_info, 0, sizeof (in_map_info));

  /* Map the buffer contents and get the pointer to NvBufSurface. */
  if (!gst_buffer_map (inbuf, &in_map_info, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (nvdspostprocess, STREAM, FAILED,
        ("%s:gst buffer map to get pointer to NvBufSurface failed", __func__), (NULL));
    return GST_FLOW_ERROR;
  }
  in_surf = (NvBufSurface *) in_map_info.data;

  nvds_set_input_system_timestamp (inbuf, GST_ELEMENT_NAME (nvdspostprocess));

  /** Preprocess on Frames */
  flow_ret = gst_nvdspostprocess_on_frame (nvdspostprocess, inbuf, in_surf);
  flow_ret =gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (nvdspostprocess),inbuf);
  if ((nvdspostprocess->current_batch_num>1) && (nvdspostprocess->last_flow_ret != flow_ret) ) {
    switch (flow_ret) {
     /* Signal the application for pad push errors by posting a error message
      * on the pipeline bus. */
      case GST_FLOW_ERROR:
      case GST_FLOW_NOT_LINKED:
      case GST_FLOW_NOT_NEGOTIATED:
        GST_ELEMENT_ERROR (nvdspostprocess, STREAM, FAILED,
                ("Internal data stream error."),
                ("streaming stopped, reason %s (%d)",
                    gst_flow_get_name (flow_ret), flow_ret));
        break;
      default:
        break;
        }
      }
      nvdspostprocess->last_flow_ret = flow_ret;
  
  if (flow_ret != GST_FLOW_OK)
    goto error;

  nvtxDomainRangeEnd(nvdspostprocess->nvtx_domain, buf_process_range);

  //g_mutex_lock (&nvdspostprocess->postprocess_lock);
  /* Check if this is a push buffer or event marker batch. If yes, no need to
   * queue the input for inferencing. */
  
  //g_mutex_unlock (&nvdspostprocess->postprocess_lock);

  flow_ret = GST_FLOW_OK;

error:
  gst_buffer_unmap (inbuf, &in_map_info);
  return flow_ret;
}

/**
 * If submit_input_buffer is implemented, it is mandatory to implement
 * generate_output. Buffers are not pushed to the downstream element from here.
 * Return the GstFlowReturn value of the latest pad push so that any error might
 * be caught by the application.
 */
static GstFlowReturn
gst_nvdspostprocess_generate_output (GstBaseTransform * btrans, GstBuffer ** outbuf)
{
  GstNvDsPostProcess *nvdspostprocess = GST_NVDSPOSTPROCESS (btrans);
  return nvdspostprocess->last_flow_ret;
}




/**
 * Boiler plate for registering a plugin and an element.
 */
static gboolean
nvdspostprocess_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_nvdspostprocess_debug, "nvdspostprocess", 0,
      "postprocess plugin");

  return gst_element_register (plugin, "nvdspostprocess", GST_RANK_PRIMARY,
      GST_TYPE_NVDSPOSTPROCESS);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvdsgst_postprocess,
    DESCRIPTION, nvdspostprocess_plugin_init, "6.3", LICENSE, BINARY_PACKAGE,
    URL)
