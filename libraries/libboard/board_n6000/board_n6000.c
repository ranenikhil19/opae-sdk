// Copyright(c) 2021-2022, Intel Corporation
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


#include <limits.h>
#include <glob.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <opae/properties.h>
#include <opae/utils.h>
#include <opae/fpga.h>
#include <netinet/ether.h>
#include <net/ethernet.h>
#include <opae/uio.h>
#include "../board_common/board_common.h"
#include "board_event_log.h"
#include "board_n6000.h"
#include "mock/opae_std.h"

#define FPGA_VAR_BUF_LEN       256
#define MAC_BUF_LEN            19
#define UNUSED_PARAM(x) ((void)x)
#define FEATURE_DEV "/sys/bus/pci/devices/*%x*:*%x*:*%x*.*%x*/fpga_region"\
					"/region*/dfl-fme*/dfl_dev*"
// DFL SYSFS
#define DFL_SYSFS_BMCFW_VER                     "dfl*/bmcfw_version"
#define DFL_SYSFS_MAX10_VER                     "dfl*/bmc_version"

#define DFL_SYSFS_MACADDR_PATH                  "dfl*/mac_address"
#define DFL_SYSFS_MACCNT_PATH                   "dfl*/mac_count"

#define DFL_SEC_PMCI_GLOB "*dfl*/**/security/"
#define DFL_SEC_USER_FLASH_COUNT  DFL_SEC_PMCI_GLOB "*flash_count"
#define DFL_SEC_BMC_CANCEL        DFL_SEC_PMCI_GLOB "bmc_canceled_csks"
#define DFL_SEC_BMC_ROOT          DFL_SEC_PMCI_GLOB "bmc_root_entry_hash"
#define DFL_SEC_PR_CANCEL         DFL_SEC_PMCI_GLOB "pr_canceled_csks"
#define DFL_SEC_PR_ROOT           DFL_SEC_PMCI_GLOB "pr_root_entry_hash"
#define DFL_SEC_SR_CANCEL         DFL_SEC_PMCI_GLOB "sr_canceled_csks"
#define DFL_SEC_SR_ROOT           DFL_SEC_PMCI_GLOB "sr_root_entry_hash"
#define DFL_SEC_PR_SDM_CANCEL     DFL_SEC_PMCI_GLOB "pr_sdm_canceled_csks"
#define DFL_SEC_PR_SDM_ROOT       DFL_SEC_PMCI_GLOB "pr_sdm_root_entry_hash"
#define DFL_SEC_SR_SDM_CANCEL     DFL_SEC_PMCI_GLOB "sr_sdm_canceled_csks"
#define DFL_SEC_SR_SDM_ROOT       DFL_SEC_PMCI_GLOB "sr_sdm_root_entry_hash"


#define HSSI_FEATURE_ID                  0x15
#define HSSI_100G_PROFILE                       27
#define HSSI_25G_PROFILE                        21
#define HSSI_10_PROFILE                         20

#define HSSI_FEATURE_LIST                       0xC
#define HSSI_PORT_ATTRIBUTE                     0x10
#define HSSI_VERSION                            0x8
#define HSSI_PORT_STATUS                        0x818

// boot page info sysfs
#define DFL_SYSFS_BOOT_GLOB "*dfl*/**/fpga_boot_image"
#define BOOTPAGE_PATTERN "_([0-9a-zA-Z]+)"

// image info sysfs
#define DFL_SYSFS_IMAGE_INFO_GLOB "*dfl*/**/fpga_image_directory*/nvmem"
#define IMAGE_INFO_STRIDE 4096
#define IMAGE_INFO_SIZE     32
#define IMAGE_INFO_COUNT     3
#define GET_BIT(var, pos) ((var >> pos) & (1))

// event log
#define DFL_SYSFS_EVENT_LOG_GLOB "*dfl*/**/bmc_event_log*/nvmem"

// BOM info
#define DFL_SYSFS_BOM_INFO_GLOB "*dfl*/**/bom_info*/nvmem"
#define FPGA_BOM_INFO_BUF_LEN   0x2000

#define DFH_CSR_ADDR                  0x18
#define DFH_CSR_SIZE                  0x20

// hssi version
struct hssi_version {
	union {
		uint32_t csr;
		struct {
			uint32_t rsvd : 8;
			uint32_t minor : 8;
			uint32_t major : 16;
		};
	};
};

//Physical Port Enable
/*
[6] - Port 0 Enable
[7] - Port 1 Enable
:
[21] - Port 15 Enable
*/
#define PORT_ENABLE_COUNT 20

// hssi feature list CSR
struct hssi_feature_list {
	union {
		uint32_t csr;
		struct {
			uint32_t axi4_support : 1;
			uint32_t hssi_num : 5;
			uint32_t port_enable : 20;
			uint32_t reserved : 6;
		};
	};
};

// hssi port attribute CSR
//Interface Attribute Port X Parameters, X =0-15
//Byte Offset: 0x10 + X * 4
struct hssi_port_attribute {
	union {
		uint32_t csr;
		struct {
			uint32_t profile : 6;
			uint32_t ready_latency : 4;
			uint32_t data_bus_width : 3;
			uint32_t low_speed_mac : 2;
			uint32_t dynamic_pr : 1;
			uint32_t sub_profile : 5;
			uint32_t reserved : 11;
		};
	};
};

//HSSI Ethernet Port Status
//Byte Offset: 0x818
struct hssi_port_status {
	union {
		uint64_t csr;
		struct {
			uint64_t txplllocked : 16;
			uint64_t txlanestable : 16;
			uint64_t rxpcsready : 16;
			uint64_t reserved : 16;
		};
	};
};


struct dfh {
	union {
		uint64_t csr;
		struct {
			uint64_t id : 12;
			uint64_t feature_rev : 4;
			uint64_t next : 24;
			uint64_t eol : 1;
			uint64_t reserved41 : 7;
			uint64_t feature_minor_rev : 4;
			uint64_t dfh_version : 8;
			uint64_t type : 4;
		};
	};
};

struct dfh_csr_addr {
	union {
		uint32_t csr;
		struct {
			uint64_t rel : 1;
			uint64_t addr : 63;
		};
	};
};

struct dfh_csr_group {
	union {
		uint32_t csr;
		struct {
			uint64_t instance_id : 16;
			uint64_t grouping_id : 15;
			uint64_t has_params : 1;
			uint64_t csr_size : 32;
		};
	};
};



typedef struct hssi_port_profile {

	uint32_t port_index;
	char profile[FPGA_VAR_BUF_LEN];

} hssi_port_profile;

#define HSS_PORT_PROFILE_SIZE 34

hssi_port_profile hssi_port_profiles[] = {

	{.port_index = 0, .profile = "LL100G"},
	{.port_index = 1, .profile = "Ultra100G"},
	{.port_index = 2, .profile = "LL50G"},
	{.port_index = 3, .profile = "LL40G"},
	{.port_index = 4, .profile = "Ultra40G"},
	{.port_index = 5, .profile = "25_50G"},
	{.port_index = 6, .profile = "10_25G"},
	{.port_index = 7, .profile = "MRPHY"},
	{.port_index = 8, .profile = "LL10G"},
	{.port_index = 9, .profile = "TSE PCS"},
	{.port_index = 10, .profile = "TSE MAC"},
	{.port_index = 11, .profile = "Flex-E"},
	{.port_index = 12, .profile = "OTN"},
	{.port_index = 13, .profile = "General PCS-Direct"},
	{.port_index = 14, .profile = "General FEC-Direct"},
	{.port_index = 15, .profile = "General PMA-Direct"},
	{.port_index = 16, .profile = "MII"},
	{.port_index = 17, .profile = "Ethernet PCS-Direct"},
	{.port_index = 18, .profile = "Ethernet FEC-Direct"},
	{.port_index = 19, .profile = "Ethernet PMA-Direct"},
	{.port_index = 20, .profile = "10GbE"},
	{.port_index = 21, .profile = "25GbE"},
	{.port_index = 22, .profile = "40GCAUI-4"},
	{.port_index = 23, .profile = "50GAUI-2"},
	{.port_index = 24, .profile = "50GAUI-1"},
	{.port_index = 25, .profile = "100GAUI-1"},
	{.port_index = 26, .profile = "100GAUI-2"},
	{.port_index = 27, .profile = "100GCAUI-4"},
	{.port_index = 28, .profile = "200GAUI-2"},
	{.port_index = 29, .profile = "200GAUI-4"},
	{.port_index = 30, .profile = "200GAUI-8"},
	{.port_index = 31, .profile = "400GAUI-4"},
	{.port_index = 32, .profile = "400GAUI-8"},
	{.port_index = 33, .profile = "CPRI"}
 };


// Parse firmware version
fpga_result parse_fw_ver(char *buf, char *fw_ver, size_t len)
{
	uint32_t  var = 0;
	fpga_result res = FPGA_OK;
	int retval = 0;
	char *endptr = NULL;

	if (buf == NULL || fw_ver == NULL) {
		OPAE_ERR("Invalid Input parameters");
		return FPGA_INVALID_PARAM;
	}

	/* BMC FW version format reading
	NIOS II Firmware Build 0x0 32 RW[23:0] 24 hFFFFFF Build version of NIOS II Firmware
	NIOS FW is up e.g. 1.0.1 for first release
	[31:24] 8hFF Firmware Support Revision - ASCII code
	0xFF is the default value without NIOS FW, will be changed after NIOS FW is up
	*/

	errno = 0;
	var = strtoul(buf, &endptr, 16);
	if (endptr != buf + strlen(buf)) {
		OPAE_ERR("Failed to convert buffer to integer: %s", strerror(errno));
		return FPGA_EXCEPTION;
	}

	retval = snprintf(fw_ver, len, "%u.%u.%u", (var >> 16) & 0xff, (var >> 8) & 0xff, var & 0xff);
	if (retval < 0) {
		OPAE_ERR("error in formatting version");
		return FPGA_EXCEPTION;
	}

	return res;
}

// Read BMC firmware version
fpga_result read_bmcfw_version(fpga_token token, char *bmcfw_ver, size_t len)
{
	fpga_result res = FPGA_OK;
	char buf[FPGA_VAR_BUF_LEN] = { 0 };

	if (bmcfw_ver == NULL) {
		OPAE_ERR("Invalid Input parameters");
		return FPGA_INVALID_PARAM;
	}

	res = read_sysfs(token, DFL_SYSFS_BMCFW_VER, buf, FPGA_VAR_BUF_LEN - 1);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to get read object");
		return res;
	}

	res = parse_fw_ver(buf, bmcfw_ver, len);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to parse version ");
	}

	return res;
}


// Read MAX10 firmware version
fpga_result read_max10fw_version(fpga_token token, char *max10fw_ver, size_t len)
{
	fpga_result res = FPGA_OK;
	char buf[FPGA_VAR_BUF_LEN] = { 0 };

	if (max10fw_ver == NULL) {
		OPAE_ERR("Invalid Input parameters");
		return FPGA_INVALID_PARAM;
	}

	res = read_sysfs(token, DFL_SYSFS_MAX10_VER, buf, FPGA_VAR_BUF_LEN - 1);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to get read object");
		return res;
	}

	res = parse_fw_ver(buf, max10fw_ver, len);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to parse version ");
	}

	return res;
}

// print mac information
fpga_result print_mac_info(fpga_token token)
{
	fpga_result res = FPGA_OK;
	char buf[MAC_BUF_LEN] = { 0 };
	char count[MAC_BUF_LEN] = { 0 };
	int n = 0;
	char *endptr = NULL;
	struct ether_addr mac_addr ;
	memset(&mac_addr, 0, sizeof(mac_addr));

	res = read_sysfs(token, DFL_SYSFS_MACADDR_PATH, (char *)buf, MAC_BUF_LEN - 1);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to read mac information");
		return res;
	}

	ether_aton_r(buf, &mac_addr);

	res = read_sysfs(token, DFL_SYSFS_MACCNT_PATH, (char *)count, MAC_BUF_LEN - 1);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to read mac information");
		return res;
	}

	errno = 0;
	n = strtol(count, &endptr, 10);
	if (endptr != count + strlen(count)) {
		OPAE_ERR("Failed to convert buffer to integer: %s", strerror(errno));
		return FPGA_EXCEPTION;
	}
	printf("%-32s : %d\n", "Number of MACs", n);

	if (n < 0 || n > 0xFFFF) {
		OPAE_ERR("Invalid mac count");
		return FPGA_EXCEPTION;
	}

	if ((mac_addr.ether_addr_octet[0] == 0xff) &&
		(mac_addr.ether_addr_octet[1] == 0xff) &&
		(mac_addr.ether_addr_octet[2] == 0xff) &&
		(mac_addr.ether_addr_octet[3] == 0xff) &&
		(mac_addr.ether_addr_octet[4] == 0xff) &&
		(mac_addr.ether_addr_octet[5] == 0xff)) {
		OPAE_ERR("Invalid MAC address");
		return FPGA_EXCEPTION;
	}

	print_mac_address(&mac_addr, n);

	return res;
}

// Read BOM Critical Components info from the FPGA
static fpga_result read_bom_info(
	const fpga_token token,
	char * const bom_info,
	const size_t len)
{
	if (bom_info == NULL)
		return FPGA_INVALID_PARAM;

	fpga_result resval = FPGA_OK;
	fpga_object fpga_object;

	fpga_result res = fpgaTokenGetObject(token, DFL_SYSFS_BOM_INFO_GLOB,
					     &fpga_object, FPGA_OBJECT_GLOB);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to get token Object");
		// Simulate reading of empty BOM info filled with 0xFF
		// so that FPGA with no BOM info produces no output.
		// Return FPGA_OK!
		memset(bom_info, 0xFF, len);
		return FPGA_OK;
	}

	res = fpgaObjectRead(fpga_object, (uint8_t *)bom_info, 0, len, FPGA_OBJECT_RAW);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to read BOM info");
		memset(bom_info, 0xFF, len); // Simulate reading of empty BOM info filled with 0xFF
		resval = res;
	}

	res = fpgaDestroyObject(&fpga_object);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to Destroy Object");
		if (resval == FPGA_OK)
			resval = res;
	}

	return resval;
}


// print BOM info
fpga_result print_bom_info(const fpga_token token)
{
	fpga_result resval = FPGA_OK;
	const size_t max_result_len = 2 * FPGA_BOM_INFO_BUF_LEN;
	char * const bom_info = (char *)opae_malloc(max_result_len);

	if (bom_info == NULL)
		return FPGA_NO_MEMORY;

	fpga_result res = read_bom_info(token, bom_info, FPGA_BOM_INFO_BUF_LEN);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to read BOM info");
		opae_free(bom_info);
		return res;
	}

	// Terminated by a null character '\0'
	bom_info[FPGA_BOM_INFO_BUF_LEN] = '\0';

	res = reformat_bom_info(bom_info, FPGA_BOM_INFO_BUF_LEN, max_result_len);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to reformat BOM info");
		if (resval == FPGA_OK)
			resval = res;
	}

	printf("%s", bom_info);

	opae_free(bom_info);

	return resval;
}

// print board information
fpga_result print_board_info(fpga_token token)
{
	fpga_result res = FPGA_OK;
	fpga_result resval = FPGA_OK;
	char bmc_ver[FPGA_VAR_BUF_LEN] = { 0 };
	char max10_ver[FPGA_VAR_BUF_LEN] = { 0 };

	res = read_bmcfw_version(token, bmc_ver, FPGA_VAR_BUF_LEN);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to read bmc version");
		resval = res;
	}

	res = read_max10fw_version(token, max10_ver, FPGA_VAR_BUF_LEN);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to read max10 version");
		resval = res;
	}

	printf("Board Management Controller NIOS FW version: %s \n", bmc_ver);
	printf("Board Management Controller Build version: %s \n", max10_ver);

	res = print_bom_info(token);
	if (res != FPGA_OK) {
		OPAE_ERR("Failed to print BOM info");
		if (resval == FPGA_OK)
			resval = res;
	}

	return resval;
}

// print phy group information
fpga_result print_phy_info(fpga_token token)
{
	fpga_result res = FPGA_OK;
	struct opae_uio uio;
	char feature_dev[SYSFS_PATH_MAX] = { 0 };
	uint8_t *mmap_ptr = NULL;

	res = find_dev_feature(token, HSSI_FEATURE_ID, feature_dev);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to find feature HSSI");
		return res;
	}

	res = opae_uio_open(&uio, feature_dev);
	if (res) {
		OPAE_ERR("Failed to open uio");
		return res;
	}

	res = opae_uio_region_get(&uio, 0, (uint8_t **)&mmap_ptr, NULL);
	if (res) {
		OPAE_ERR("Failed to get uio region");
		opae_uio_close(&uio);
		return res;
	}

	res = print_hssi_port_status(mmap_ptr);
	if (res) {
		OPAE_ERR("Failed to read hssi port status");
	}

	opae_uio_close(&uio);
	return res;
}

// Sec info
fpga_result print_sec_info(fpga_token token)
{
	fpga_result res = FPGA_OK;
	fpga_result resval = FPGA_OK;
	fpga_object tcm_object;
	char name[SYSFS_PATH_MAX] = { 0 };

	res = fpgaTokenGetObject(token, DFL_SEC_PMCI_GLOB, &tcm_object,
		FPGA_OBJECT_GLOB);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to get token Object");
		return res;
	}
	printf("********** SEC Info START ************ \n");

	// BMC Keys
	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_BMC_ROOT, name, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "BMC root entry hash", name);
	} else {
		OPAE_MSG("Failed to Read TCM BMC root entry hash");
		printf("%-32s : %s\n", "BMC root entry hash", "None");
		resval = res;
	}

	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_BMC_CANCEL, name, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "BMC CSK IDs canceled",
				strlen(name) > 0 ? name : "None");
	} else {
		OPAE_MSG("Failed to Read BMC CSK IDs canceled");
		printf("%-32s : %s\n", "BMC CSK IDs canceled", "None");
		resval = res;
	}

	// PR Keys
	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_PR_ROOT, name, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "PR root entry hash", name);
	} else {
		OPAE_MSG("Failed to Read PR root entry hash");
		printf("%-32s : %s\n", "PR root entry hash", "None");
		resval = res;
	}

	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_PR_CANCEL, name, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "AFU/PR CSK IDs canceled",
			strlen(name) > 0 ? name : "None");
	} else {
		OPAE_MSG("Failed to Read AFU CSK/PR IDs canceled");
		printf("%-32s : %s\n", "AFU/PR CSK IDs canceled", "None");
		resval = res;
	}

	// SR Keys
	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_SR_ROOT, name, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "FIM root entry hash", name);
	} else {
		OPAE_MSG("Failed to Read FIM root entry hash");
		printf("%-32s : %s\n", "FIM root entry hash", "None");
		resval = res;
	}

	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_SR_CANCEL, name, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "FIM CSK IDs canceled",
			strlen(name) > 0 ? name : "None");
	} else {
		OPAE_MSG("Failed to Read FIM CSK IDs canceled");
		printf("%-32s : %s\n", "FIM CSK IDs canceled", "None");
		resval = res;
	}

	// User flash count
	memset(name, 0, sizeof(name));
	res = read_sysfs(token, DFL_SEC_USER_FLASH_COUNT, name,
		SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "User flash update counter", name);
	} else {
		OPAE_MSG("Failed to Read User flash update counter");
		printf("%-32s : %s\n", "User flash update counter", "None");
		resval = res;
	}

	// PR SDM Keys
	res = read_sysfs(token, DFL_SEC_PR_SDM_CANCEL, name,
		SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "PR SDM CSK IDs canceled", name);
	} else {
		OPAE_MSG("Failed to PR SDM CSK IDs canceled");
		printf("%-32s : %s\n", "PR SDM CSK IDs canceled", "None");
		resval = res;
	}

	res = read_sysfs(token, DFL_SEC_PR_SDM_ROOT, name,
		SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "PR SDM root entry hash", name);
	} else {
		OPAE_MSG("Failed to PR SDM root entry hash");
		printf("%-32s : %s\n", "PR SDM root entry hash", "None");
		resval = res;
	}

	// SR SDM Keys
	res = read_sysfs(token, DFL_SEC_SR_SDM_CANCEL, name,
		SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "SR SDM CSK IDs canceled", name);
	} else {
		OPAE_MSG("Failed to SR SDM CSK IDs canceled");
		printf("%-32s : %s\n", "SR SDM CSK IDs canceled", "None");
		resval = res;
	}

	res = read_sysfs(token, DFL_SEC_SR_SDM_ROOT, name,
		SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {
		printf("%-32s : %s\n", "SR SDM root entry hash", name);
	} else {
		OPAE_MSG("Failed to SR SDM root entry hash");
		printf("%-32s : %s\n", "SR SDM root entry hash", "None");
		resval = res;
	}

	res = fpgaDestroyObject(&tcm_object);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to Destroy Object");
		resval = res;
	}

	printf("********** SEC Info END ************ \n");

	return resval;
}


// prints fpga boot page info
fpga_result fpga_boot_info(fpga_token token)
{
	char boot[SYSFS_PATH_MAX] = { 0 };
	char page[SYSFS_PATH_MAX] = { 0 };
	fpga_result res           = FPGA_OK;
	int reg_res               = 0;
	char err[128]           = { 0 };
	regex_t re;
	regmatch_t matches[3];

	// boot page
	memset(boot, 0, sizeof(boot));
	res = read_sysfs(token, DFL_SYSFS_BOOT_GLOB, boot, SYSFS_PATH_MAX - 1);
	if (res == FPGA_OK) {

		reg_res = regcomp(&re, BOOTPAGE_PATTERN, REG_EXTENDED | REG_ICASE);
		if (reg_res) {
			OPAE_ERR("Error compiling regex");
			return FPGA_EXCEPTION;
		}

		reg_res = regexec(&re, boot, 3, matches, 0);
		if (reg_res) {
			regerror(reg_res, &re, err, sizeof(err));
			OPAE_MSG("Error executing regex: %s", err);
			regfree(&re);
			return FPGA_EXCEPTION;
		}
		memcpy(page, boot + matches[0].rm_so + 1,
			matches[0].rm_eo - (matches[0].rm_so + 1));
		page[matches[0].rm_eo - (matches[0].rm_so + 1)] = '\0';

		printf("%-32s : %s\n", "Boot Page", page);
		regfree(&re);
	} else {
		OPAE_MSG("Failed to Read Boot Page");
		printf("%-32s : %s\n", "Boot Page", "N/A");
	}

	return res;
}

// prints fpga image info
fpga_result fpga_image_info(fpga_token token)
{
	const char *image_info_label[IMAGE_INFO_COUNT] = {
		"Factory Image Info",
		"User1 Image Info",
		"User2 Image Info",
	};
	fpga_object fpga_object;
	fpga_result res;
	size_t i;

	res = fpgaTokenGetObject(token, DFL_SYSFS_IMAGE_INFO_GLOB,
			&fpga_object, FPGA_OBJECT_GLOB);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to get token Object");
		return res;
	}

	for (i = 0; i < IMAGE_INFO_COUNT; i++) {
		size_t offset = IMAGE_INFO_STRIDE * i;
		uint8_t data[IMAGE_INFO_SIZE + 1] = { 0 };
		char *image_info = (char *)data;
		size_t p;

		printf("%-32s : ", image_info_label[i]);

		res = fpgaObjectRead(fpga_object, data, offset,
				IMAGE_INFO_SIZE, FPGA_OBJECT_RAW);
		if (res != FPGA_OK) {
			printf("N/A\n");
			continue;
		}

		for (p = 0; p < IMAGE_INFO_SIZE; p++)
			if (data[p] != 0xff)
				break;

		if (p >= IMAGE_INFO_SIZE) {
			printf("None\n");
			continue;
		}

		if (strlen(image_info) == 0) {
			printf("Empty\n");
			continue;
		}

		printf("%s\n", image_info);
	}

	if (fpgaDestroyObject(&fpga_object) != FPGA_OK)
		OPAE_ERR("Failed to Destroy Object");

	return res;
}

fpga_result fpga_event_log(fpga_token token, uint32_t first, uint32_t last,
	bool print_list, bool print_sensors, bool print_bits)
{
	fpga_object fpga_object;
	struct bel_event event;
	uint32_t count = last;
	uint32_t i = first;
	fpga_result res;
	uint32_t ptr;

	memset(&event, 0, sizeof(event));

	if (first > bel_ptr_count()) {
		fprintf(stderr, "invalid --boot value: %u\n", first);
		return FPGA_INVALID_PARAM;
	}

	if (last > bel_ptr_count()) {
		fprintf(stderr, "invalid --boot + --count value: %u\n", last);
		return FPGA_INVALID_PARAM;
	}

	res = fpgaTokenGetObject(token, DFL_SYSFS_EVENT_LOG_GLOB,
			&fpga_object, FPGA_OBJECT_GLOB);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to get token Object");
		return res;
	}

	/* Special case when all events requested */
	if (first == last) {
		count = bel_ptr_count();
		i = 0;
	}

	/* Get index to latest log event in flash */
	res = bel_ptr(fpga_object, &ptr);
	if (res != FPGA_OK) {
		OPAE_MSG("Failed to read log pointer");
		goto out;
	}

	/* Fast forward to the requested event */
	while (first--)
		ptr = bel_ptr_next(ptr);

	/* Read and print the requested number of events */
	while (i++ < count) {
		res = bel_read(fpga_object, ptr, &event);
		if (res != FPGA_OK)
			goto out;

		if (print_list) {
			bel_timespan(&event, i - 1);
		} else if (bel_empty(&event)) {
			printf("Boot %i: Empty\n", i - 1);
		} else {
			printf("Boot %i\n", i - 1);
			bel_print(&event, print_sensors, print_bits);
		}

		ptr = bel_ptr_next(ptr);
	}

out:
	if (fpgaDestroyObject(&fpga_object) != FPGA_OK)
		OPAE_ERR("Failed to Destroy Object");

	return FPGA_OK;
}

fpga_result print_hssi_port_status(uint8_t *uio_ptr)
{
	uint32_t i                     = 0;
	uint32_t k                     = 0;
	uint32_t ver_offset            = 0;
	uint32_t feature_list_offset   = 0;
	uint32_t port_sts_offset       = 0;
	uint32_t port_attr_offset      = 0;
	struct dfh dfh_csr;
	struct dfh_csr_addr csr_addr;
	struct hssi_port_attribute port_profile;
	struct hssi_feature_list  feature_list;
	struct hssi_version  hssi_ver;
	struct hssi_port_status port_status;

	if (uio_ptr == NULL) {
		OPAE_ERR("Invalid Input parameters");
		return FPGA_INVALID_PARAM;
	}

	dfh_csr.csr = *((uint64_t *)(uio_ptr + 0x0));
	// dfhv0
	if ((dfh_csr.feature_rev == 0) ||
		(dfh_csr.feature_rev == 0x1)) {
		ver_offset = HSSI_VERSION;
		feature_list_offset = HSSI_FEATURE_LIST;
		port_sts_offset = HSSI_PORT_STATUS;
		port_attr_offset = HSSI_PORT_ATTRIBUTE;
	} else if (dfh_csr.feature_rev == 0x2) { // dfhv0.5
		csr_addr.csr = *((uint64_t *)(uio_ptr + DFH_CSR_ADDR));
		ver_offset = csr_addr.addr;
		feature_list_offset = csr_addr.addr + 0x4;
		port_sts_offset = HSSI_PORT_STATUS;
		port_attr_offset = csr_addr.addr + 0x8;

	} else {
		printf("DFH veriosn not supported:%x \n", dfh_csr.feature_rev);
		return FPGA_NOT_SUPPORTED;
	}

	feature_list.csr = *((uint32_t *)(uio_ptr + feature_list_offset));
	hssi_ver.csr = *((uint32_t *)(uio_ptr + ver_offset));
	port_status.csr = *((volatile uint64_t *)(uio_ptr
		+ port_sts_offset));

	printf("//****** HSSI information ******//\n");
	printf("%-32s : %d.%d  \n", "HSSI version", hssi_ver.major, hssi_ver.minor);
	printf("%-32s : %d  \n", "Number of ports", feature_list.hssi_num);

	for (i = 0; i < PORT_ENABLE_COUNT; i++) {

		// prints only active/enabled ports
		if ((GET_BIT(feature_list.port_enable, i) == 0)) {
			continue;
		}

		port_profile.csr = *((volatile uint32_t *)(uio_ptr +
			port_attr_offset + i * 4));

		if (port_profile.profile > HSS_PORT_PROFILE_SIZE) {
			printf("Port%-28d :%s\n", i, "N/A");
			continue;
		}

		for (int j = 0; j < HSS_PORT_PROFILE_SIZE; j++) {
			if (hssi_port_profiles[j].port_index == port_profile.profile) {
				// lock, tx, rx bits set - link status UP
				// lock, tx, rx bits not set - link status DOWN
				if ((GET_BIT(port_status.txplllocked, k) == 1) &&
					(GET_BIT(port_status.txlanestable, k) == 1) &&
					(GET_BIT(port_status.rxpcsready, k) == 1)) {
					printf("Port%-28d :%-12s %s\n", i,
						hssi_port_profiles[j].profile, "UP");
				} else {
					printf("Port%-28d :%-12s %s\n", i,
						hssi_port_profiles[j].profile, "DOWN");
				}
				k++;
				break;
			}
		}
	}

	return FPGA_OK;
}
