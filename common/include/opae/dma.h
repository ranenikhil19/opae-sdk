// Copyright(c) 2017, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/**
 * @file dma.h
 * @brief APIs for DMA feature
 *
 * User should use the feature API to discover features which are DMA type.
 * With the feature token from the enumeration user can open the feature.
 * With the feature handle user can access and transfer data using the DMA.
 */

#ifndef __FPGA_DMA_H__
#define __FPGA_DMA_H__

#include <opae/types.h>
#include <opae/feature.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DMA properties
 *
 * Common properties for all DMA engines.
 * Output of fpgaDMAPropertiesGet.
 */
typedef struct {uint64_t    maxChannelNum;  /**< Max number of channels that the DMA engine support */
                uint64_t    maxRingSize;    /**< Max number of buffers that the DMA can hold */
                uint64_t    maxBufferSize;  /**< Max size of one buffer */
                uint64_t    addrAlignmentForDMA;    /**< The DMA will be used only when reaching this alignment */
                uint64_t    minimumXferSizeForDMA;  /**< DMA will be used only at a multiplication of this size */
                uint64_t    capabilities_mask;      /**< Bit mask of fpga_dma_transfer_type */
                uint64_t    reserved [32];
                fpga_guid   *txEndPGuid;     /**< Pointer to a table of Guid of the connected IP to a Tx channels */
                fpga_guid   *rxEndPGuid;     /**< Pointer to a table of Guid of the connected IP to a Rx channels */
}fpgaDMAProperties;

/**
 * Get DMA feature properties.
 *
 * Get DMA properties from a feature token (DMA feature type)
 *
 * @param[in]   token   Feature token
 * @param[out]  prop    pre allocated fpgaDMAProperties structure to write information into
 * @param[in]   max_ch  Entry number in the Tx/Rx end point GUID array
 *
 * @returns FPGA_OK on success.
 */

fpga_result
fpgaDMAPropertiesGet(fpga_feature_token token, fpgaDMAProperties *prop, int max_ch);

/**
 * DMA transfer
 *
 * Holds a DMA transaction information
 *
 * @note N/A fields should be set to 0xFF (-1) to indicate that they are not valid.
 */
typedef struct {    void        *priv_data; /**< Private data for internal use - must be first */
                    uint64_t    src;        /**< Source address */
                    uint64_t    dst;        /**< Destination address */
                    uint64_t    len;        /**< Transactions  length */
                    uint64_t    wsid;       /**< wsid of the host memory if it was allocated with prepareBuffer */
                    fpga_dma_transfer_type  type;   /**< Direction and streaming or memory */
                    int         ch_index;   /**< in case of multi channel DMA, which channel to use */
                    void        *meta_data; /**< Tx stream - user meta data for the receiving IP */
                    uint64_t    meta_data_len; /**< Tx stream - length of the meta data */
                    uint64_t    *rx_len;    /**< Rx stream - length of Rx data */
                    bool        *rx_eop;    /**< Rx stream - Set is end of packet was received */
                    uint64_t    reserved[8];
}fpga_dma_transfer;

/**
 * Start a blocking transfer.
 *
 * Start a sync transfer and return only all the data was copied.
 *
 * @param[in]   dma_handle      as populated by fpgaFeatureOpen()
 * @param[in]   dma_xfer        transfer information
 *
 * @returns FPGA_OK on success.
 */
fpga_result
fpgaDMATransferSync(fpga_feature_handle dma_h, fpga_dma_transfer *dma_xfer);

/**
 * Start a none blocking transfer (callback).
 *
 * Start an Async transfer (Return immediately)
 * Callback will be invoke when the transfer is completed.
 *
 * @param[in]   dma_handle      as populated by fpgaFeatureOpen()
 * @param[in]   dma_xfer        transfer information
 * @param[in]   cb              Call back function to call when the transfer is completed
 * @param[in]   context         argument to pass to the callback function
 *
 * @note For posting receive buffers to the DMA in Rx streaming mode,
 *       call this function with NULL back.
 *
 * @returns FPGA_OK on success.
 */
fpga_result
fpgaDMATransferCB(fpga_feature_handle dma_h,
                fpga_dma_transfer *dma_xfer,
                fpga_cb cb,
                void *context);



#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // __FPGA_DMA_H__

