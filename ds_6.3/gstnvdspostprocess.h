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

#ifndef __GST_NVDSPOSTPROCESS_H__
#define __GST_NVDSPOSTPROCESS_H__

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gst-nvquery.h"


#include "nvtx3/nvToolsExt.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <functional>

#include <vector>
#include <cuda.h>
#include <cuda_runtime.h>
#include "nvbufsurface.h"
#include "nvbufsurftransform.h"
#include "gstnvdsmeta.h"
#include <gst/gst.h>
#include <gst/base/base.h>

#include "nvds_roi_meta.h"
#include "nvtx3/nvToolsExt.h"
#include <unordered_map>


/* Package and library details required for plugin_init */
#define PACKAGE "nvdsvideotemplate"
#define VERSION "1.0"
#define LICENSE "Proprietary"
#define DESCRIPTION "NVIDIA custom preprocessing plugin for integration with DeepStream on DGPU/Jetson"
#define BINARY_PACKAGE "NVIDIA DeepStream Preprocessing using custom algorithms for different streams"
#define URL "http://nvidia.com/"

G_BEGIN_DECLS
/* Standard boilerplate stuff */
typedef struct _GstNvDsPostProcess GstNvDsPostProcess;
typedef struct _GstNvDsPostProcessClass GstNvDsPostProcessClass;

/* Standard boilerplate stuff */
#define GST_TYPE_NVDSPOSTPROCESS (gst_nvdspostprocess_get_type())
#define GST_NVDSPOSTPROCESS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NVDSPOSTPROCESS,GstNvDsPostProcess))
#define GST_NVDSPOSTPROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NVDSPOSTPROCESS,GstNvDsPostProcessClass))
#define GST_NVDSPOSTPROCESS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_NVDSPOSTPROCESS, GstNvDsPostProcessClass))
#define GST_IS_NVDSPOSTPROCESS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NVDSPOSTPROCESS))
#define GST_IS_NVDSPOSTPROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NVDSPOSTPROCESS))
#define GST_NVDSPOSTPROCESS_CAST(obj)  ((GstNvDsPostProcess *)(obj))

/** per frame zone info */
typedef struct
{
  /** Point */
  uint64_t x,y;
} Point;


typedef  std::vector<Point> Points;
typedef  std::vector<gint> gintvec;
typedef  std::vector<gdouble> gdoublevec;


typedef struct
{
  /**src_id */
  guint64 src_id;

  /** total zones in a group */
  guint num_zones;

  /** custom transformation function name */
  gchar *custom_transform_function_name = NULL;

    
  /**Vector of zone Points */
  std::vector<Points> zone_pts;


  /**Fcm factor */
  gdouble fcm_factor;

  gboolean remove_uncounted;

  /**zonewise count approach */
  gintvec zone_approach;

  std::vector<gdoublevec> zone_color;
  
  gintvec zone_ids; 

  /** boolean indicating if processing on src or not */
  gboolean enable = 0;
  
  


} GstNvDsPostProcessGroup;





/**
 *  struct denoting properties set by config file
 */
typedef struct {
  /** for config param : enable*/
  gboolean enable;
  /** for config param : object_ids*/
  gboolean object_ids;
  /** for config param : custom-lib-path */
  gboolean custom_lib_path;
  /** for config param : custom-tensor-function-name */
  gboolean custom_tensor_function_name;
  /** for config param : zone_ids */
  gboolean zone_ids;
  /** for config param : fcm_factor */
  gboolean fcm_factor;
  /** for config param : zone_cords */
  gboolean zone_cords;
  /** for config param : zone_approach */
  gboolean zone_approach;
  /** for config param : remove_uncounted */
  gboolean remove_uncounted;
} NvDsPostProcessPropertySet;

/**
 * Strucuture containing Postprocess info
 */
struct _GstNvDsPostProcess
{
  /** Gst Base Transform */
  GstBaseTransform base_trans;
   
  /** Object ids */
  std::vector <gint> object_ids;


  /** group information as specified in config file */
  std::vector<GstNvDsPostProcessGroup*> nvdspostprocess_groups;

  /** struct denoting properties set by config file */
  NvDsPostProcessPropertySet property_set;

  /** pointer to the custom lib ctx */
  //CustomCtx* custom_lib_ctx;

  /** custom lib init params */
  //CustomInitParams custom_initparams;

  /** custom lib handle */
  void* custom_lib_handle;

  /** Custom Library Name */
  gchar* custom_lib_path;

  /** custom tensor function name */
  gchar* custom_tensor_function_name;

  /** wrapper to custom tensor function */
  //std::function <NvDsPreProcessStatus(CustomCtx *, NvDsPreProcessBatch *, NvDsPreProcessCustomBuf *&,
  //                                   CustomTensorParams &, NvDsPreProcessAcquirer *)> custom_tensor_function;


  
  /** Processing Queue and related synchronization structures. */
  /** Gmutex lock for against shared access in threads**/
  GMutex postprocess_lock;

  /** Queue to send data to output thread for processing**/
  GQueue *postprocess_queue;

  /** Gcondition for process queue**/
  GCond postprocess_cond;

  /** Output thread. */
  GThread *output_thread;

  /** Boolean to signal output thread to stop. */
  gboolean stop;

  /** Unique ID of the element. Used to identify metadata
   *  generated by this element. */
  guint unique_id;

  /** Frame number of the current input buffer */
  guint64 frame_num;

  
  /** GPU ID on which we expect to execute the task */
  guint gpu_id;

  /** if disabled plugin will work in passthrough mode */
  gboolean enable;

  /** Config file path for nvdspostprocess **/
  gchar *config_file_path;

  /** Config file parsing status **/
  gboolean config_file_parse_successful;

  
  /** Current batch number of the input batch. */
  gulong current_batch_num;

  /** GstFlowReturn returned by the latest buffer pad push. */
  GstFlowReturn last_flow_ret;

  

  /** NVTX Domain. */
  nvtxDomainHandle_t nvtx_domain;

  

};

/** Boiler plate stuff */
struct _GstNvDsPostProcessClass
{
  /** gst base transform class */
  GstBaseTransformClass parent_class;
};

GType gst_nvdspostprocess_get_type (void);

G_END_DECLS
#endif /* __GST_NVDSPREPROCESS_H__ */
