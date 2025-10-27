/****************************************************************************
*   KWS Neural Network header file
*   Based on vnn_voiceprintuint8.h structure
****************************************************************************/
#ifndef _VNN_KWS_H_
#define _VNN_KWS_H_

#include "vsi_nn_pub.h"

/*-------------------------------------------
        Version definitions
-------------------------------------------*/
#define VNN_VERSION_MAJOR 1
#define VNN_VERSION_MINOR 1
#define VNN_VERSION_PATCH 83

#define VNN_APP_VERSION \
    (VNN_VERSION_MAJOR * 10000 + VNN_VERSION_MINOR * 100 + VNN_VERSION_PATCH)

#if defined(__cplusplus)
extern "C"{
#endif

/**
 * Create KWS neural network graph
 *
 * @param data_file_name Path to the NBG model file
 * @param in_ctx Optional context (can be NULL)
 * @param pre_process_map Preprocessing configuration
 * @param pre_process_map_count Number of preprocessing elements
 * @param post_process_map Postprocessing configuration
 * @param post_process_map_count Number of postprocessing elements
 * @return Graph pointer or NULL on failure
 */
vsi_nn_graph_t * vnn_CreateKWS
    (
    const char * data_file_name,
    vsi_nn_context_t in_ctx,
    const vsi_nn_preprocess_map_element_t * pre_process_map,
    uint32_t pre_process_map_count,
    const vsi_nn_postprocess_map_element_t * post_process_map,
    uint32_t post_process_map_count
    );

/**
 * Release KWS neural network graph
 *
 * @param graph Graph to release
 * @param release_ctx Whether to release the context
 */
void vnn_ReleaseKWS
    (
    vsi_nn_graph_t * graph,
    vsi_bool release_ctx
    );

#if defined(__cplusplus)
}
#endif

#endif /* _VNN_KWS_H_ */
