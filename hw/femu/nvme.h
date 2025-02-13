#ifndef __FEMU_NVME_H
#define __FEMU_NVME_H

#include "qemu/osdep.h"
#include "qemu/uuid.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include "hw/pci/msix.h"
#include "hw/pci/msi.h"
#include "hw/virtio/vhost.h"
#include "qapi/error.h"
#include "sysemu/kvm.h"

#include "backend/dram.h"
#include "inc/rte_ring.h"
#include "inc/pqueue.h"
#include "nand/nand.h"
#include "timing-model/timing.h"

#define NVME_ID_NS_LBADS(ns)                                                  \
    ((ns)->id_ns.lbaf[NVME_ID_NS_FLBAS_INDEX((ns)->id_ns.flbas)].lbads)

#define NVME_ID_NS_LBADS_BYTES(ns) (1 << NVME_ID_NS_LBADS(ns))

#define NVME_ID_NS_MS(ns)                                                     \
    le16_to_cpu(                                                              \
        ((ns)->id_ns.lbaf[NVME_ID_NS_FLBAS_INDEX((ns)->id_ns.flbas)].ms)      \
    )

#define NVME_ID_NS_LBAF_DS(ns, lba_index) (ns->id_ns.lbaf[lba_index].lbads)
#define NVME_ID_NS_LBAF_MS(ns, lba_index) (ns->id_ns.lbaf[lba_index].ms)
/**
 * @brief 
 * Advance Channel latency emulating
 * inhoinno 
 */
#define ADVANCE_PER_CH_ENDTIME 1
#define SK_HYNIX_VALIDATION 0
#define MK_ZONE_CONVENTIONAL 5
#define NVME_PRIORITY_SCHED_MODE 1      //future feature for ConfZNS

#define PCIe_TIME_SIMULATION 1
#define Interface_PCIeGen3x4_bwmb (4034 * MiB) //MB.s
#define Interface_PCIeGen3x4_bw 4034
typedef struct _PCIe_Gen3_x4 {
    //lock
    uint64_t bw;
    uint64_t stime;
    uint64_t ntime; 
    bool busy;
}PCIe_Gen3_x4; //FOR real zns



/**
 * solesie: Controller Properties start
 */

/**
 * solesie: Controller Properties
 */
typedef struct NvmeBar {
	uint64_t    cap;        // Controller Capabilities
	uint32_t    vs;         // Version
	uint32_t    intms;      // Interrupt Mask Set
	uint32_t    intmc;      // Interrupt Mask Clear
	uint32_t    cc;         // Controller Configuration
	uint32_t    rsvd1;      // reserved
	uint32_t    csts;       // Controller Status
	uint32_t    nssrc;		// NVM Subsystem Reset Control
	uint32_t    aqa;        // Admin Queue Attributes
	uint64_t    asq;        // Admin Submission Queue Base Address
	uint64_t    acq;        // Admin Completion Queue Base Address
	uint32_t    cmbloc; 	// Controller Memory Buffer Location
	uint32_t    cmbsz; 		// Controller Memory Buffer Size
} NvmeBar;

enum NvmeCapShift {
    CAP_MQES_SHIFT      = 0,
    CAP_CQR_SHIFT       = 16,
    CAP_AMS_SHIFT       = 17,
    CAP_TO_SHIFT        = 24,
    CAP_DSTRD_SHIFT     = 32,
    CAP_NSSRS_SHIFT     = 33,
    CAP_CSS_SHIFT       = 37,
    CAP_OC_SHIFT        = 44,
    CAP_MPSMIN_SHIFT    = 48,
    CAP_MPSMAX_SHIFT    = 52,
};

enum NvmeCapMask {
    CAP_MQES_MASK      = 0xffff,
    CAP_CQR_MASK       = 0x1,
    CAP_AMS_MASK       = 0x3,
    CAP_TO_MASK        = 0xff,
    CAP_DSTRD_MASK     = 0xf,
    CAP_NSSRS_MASK     = 0x1,
    CAP_CSS_MASK       = 0xff,
    CAP_OC_MASK        = 0x1,
    CAP_MPSMIN_MASK    = 0xf,
    CAP_MPSMAX_MASK    = 0xf,
};

#define NVME_MAX_QS PCI_MSIX_FLAGS_QSIZE
#define NVME_MAX_QUEUE_ENTRIES  0xffff
#define NVME_MAX_STRIDE         12
#define NVME_MAX_NUM_NAMESPACES 256
#define NVME_MAX_QUEUE_ES       0xf
#define NVME_MIN_CQUEUE_ES      0x4
#define NVME_MIN_SQUEUE_ES      0x6
#define NVME_SPARE_THRESHOLD    20
#define NVME_TEMPERATURE        0x143
#define NVME_OP_ABORTED         0xff

/**
 * solesie: Define macros for NvmeBar.cap 64bit format
 */
#define NVME_CAP_MQES(cap)  (((cap) >> CAP_MQES_SHIFT)   & CAP_MQES_MASK)   // Maximum Queue Entries Supported
#define NVME_CAP_CQR(cap)   (((cap) >> CAP_CQR_SHIFT)    & CAP_CQR_MASK)    // Contiguous Queues Required
#define NVME_CAP_AMS(cap)   (((cap) >> CAP_AMS_SHIFT)    & CAP_AMS_MASK)    // Arbitration Mechanism Supported
#define NVME_CAP_TO(cap)    (((cap) >> CAP_TO_SHIFT)     & CAP_TO_MASK)     // Timeout
#define NVME_CAP_DSTRD(cap) (((cap) >> CAP_DSTRD_SHIFT)  & CAP_DSTRD_MASK)  // Doorbell Stride
#define NVME_CAP_NSSRS(cap) (((cap) >> CAP_NSSRS_SHIFT)  & CAP_NSSRS_MASK)  // NVM Subsystem Reset Supported
#define NVME_CAP_CSS(cap)   (((cap) >> CAP_CSS_SHIFT)    & CAP_CSS_MASK)    // Command Sets Supported
#define NVME_CAP_OC12(cap)  (((cap) >> CAP_Oc12SHIFT) & CAP_Oc12MASK)
#define NVME_CAP_MPSMIN(cap)(((cap) >> CAP_MPSMIN_SHIFT) & CAP_MPSMIN_MASK) // Memory Page Size Minimum
#define NVME_CAP_MPSMAX(cap)(((cap) >> CAP_MPSMAX_SHIFT) & CAP_MPSMAX_MASK) // Memory Page Size Maximum

#define NVME_CAP_SET_MQES(cap, val)   (cap |= (uint64_t)(val & CAP_MQES_MASK)  \
                                                           << CAP_MQES_SHIFT)
#define NVME_CAP_SET_CQR(cap, val)    (cap |= (uint64_t)(val & CAP_CQR_MASK)   \
                                                           << CAP_CQR_SHIFT)
#define NVME_CAP_SET_AMS(cap, val)    (cap |= (uint64_t)(val & CAP_AMS_MASK)   \
                                                           << CAP_AMS_SHIFT)
#define NVME_CAP_SET_TO(cap, val)     (cap |= (uint64_t)(val & CAP_TO_MASK)    \
                                                           << CAP_TO_SHIFT)
#define NVME_CAP_SET_DSTRD(cap, val)  (cap |= (uint64_t)(val & CAP_DSTRD_MASK) \
                                                           << CAP_DSTRD_SHIFT)
#define NVME_CAP_SET_NSSRS(cap, val)  (cap |= (uint64_t)(val & CAP_NSSRS_MASK) \
                                                           << CAP_NSSRS_SHIFT)
#define NVME_CAP_SET_CSS(cap, val)    (cap |= (uint64_t)(val & CAP_CSS_MASK)   \
                                                           << CAP_CSS_SHIFT)
#define NVME_CAP_SET_OC(cap, val)   (cap |= (uint64_t)(val & CAP_OC_MASK)\
                                                            << CAP_OC_SHIFT)
#define NVME_CAP_SET_MPSMIN(cap, val) (cap |= (uint64_t)(val & CAP_MPSMIN_MASK)\
                                                           << CAP_MPSMIN_SHIFT)
#define NVME_CAP_SET_MPSMAX(cap, val) (cap |= (uint64_t)(val & CAP_MPSMAX_MASK)\
                                                            << CAP_MPSMAX_SHIFT)

/**
 * solesie: Command Set Identifier. 
 * This field is CNS(Controller or Namespace Structure, Identity command) value specific.
 */
enum NvmeCsi {
    NVME_CSI_NVM                = 0x00,
    NVME_CSI_ZONED              = 0x02,
};

enum NvmeCapCss {
    NVME_CAP_CSS_NVM        = 1 << 0,
    NVME_CAP_CSS_CSI_SUPP   = 1 << 6,
    NVME_CAP_CSS_ADMIN_ONLY = 1 << 7,
};

enum NvmeCcShift {
    CC_EN_SHIFT     = 0,
    CC_CSS_SHIFT    = 4,
    CC_MPS_SHIFT    = 7,
    CC_AMS_SHIFT    = 11,
    CC_SHN_SHIFT    = 14,
    CC_IOSQES_SHIFT = 16,
    CC_IOCQES_SHIFT = 20,
};

enum NvmeCcMask {
    CC_EN_MASK      = 0x1,
    CC_CSS_MASK     = 0x7,
    CC_MPS_MASK     = 0xf,
    CC_AMS_MASK     = 0x7,
    CC_SHN_MASK     = 0x3,
    CC_IOSQES_MASK  = 0xf,
    CC_IOCQES_MASK  = 0xf,
};

/**
 * solesie: Define macros for NvmeBar.cc 32bit format
 */
#define NVME_CC_EN(cc)     ((cc >> CC_EN_SHIFT)     & CC_EN_MASK)       // Enable
#define NVME_CC_CSS(cc)    ((cc >> CC_CSS_SHIFT)    & CC_CSS_MASK)      // I/O Command Set Selected
#define NVME_CC_MPS(cc)    ((cc >> CC_MPS_SHIFT)    & CC_MPS_MASK)      // Memory Page Size
#define NVME_CC_AMS(cc)    ((cc >> CC_AMS_SHIFT)    & CC_AMS_MASK)      // Arbitration Mechanism Selected
#define NVME_CC_SHN(cc)    ((cc >> CC_SHN_SHIFT)    & CC_SHN_MASK)      // Shutdown Notification
#define NVME_CC_IOSQES(cc) ((cc >> CC_IOSQES_SHIFT) & CC_IOSQES_MASK)   // I/O Submission Queue Entry Size
#define NVME_CC_IOCQES(cc) ((cc >> CC_IOCQES_SHIFT) & CC_IOCQES_MASK)   // I/O Completion Queue Entry Size (IOCQES)

enum NvmeCcCss {
    NVME_CC_CSS_NVM        = 0x0,
    NVME_CC_CSS_CSI        = 0x6,
    NVME_CC_CSS_ADMIN_ONLY = 0x7,
};

enum NvmeCstsShift {
    CSTS_RDY_SHIFT      = 0,
    CSTS_CFS_SHIFT      = 1,
    CSTS_SHST_SHIFT     = 2,
    CSTS_NSSRO_SHIFT    = 4,
};

enum NvmeCstsMask {
    CSTS_RDY_MASK   = 0x1,
    CSTS_CFS_MASK   = 0x1,
    CSTS_SHST_MASK  = 0x3,
    CSTS_NSSRO_MASK = 0x1,
};

enum NvmeCsts {
    NVME_CSTS_READY         = 1 << CSTS_RDY_SHIFT,
    NVME_CSTS_FAILED        = 1 << CSTS_CFS_SHIFT,
    NVME_CSTS_SHST_NORMAL   = 0 << CSTS_SHST_SHIFT,
    NVME_CSTS_SHST_PROGRESS = 1 << CSTS_SHST_SHIFT,     
    NVME_CSTS_SHST_COMPLETE = 2 << CSTS_SHST_SHIFT,
    NVME_CSTS_NSSRO         = 1 << CSTS_NSSRO_SHIFT,
};

/**
 * solesie: Define macros for NvmeBar.csts 32bit format.
 * Processing Paused (PP), Shutdown Type (ST) are not implemented now.
 */
#define NVME_CSTS_RDY(csts)     ((csts >> CSTS_RDY_SHIFT)   & CSTS_RDY_MASK)    // Ready
#define NVME_CSTS_CFS(csts)     ((csts >> CSTS_CFS_SHIFT)   & CSTS_CFS_MASK)    // Controller Fatal Status
#define NVME_CSTS_SHST(csts)    ((csts >> CSTS_SHST_SHIFT)  & CSTS_SHST_MASK)   // Shutdown Status
#define NVME_CSTS_NSSRO(csts)   ((csts >> CSTS_NSSRO_SHIFT) & CSTS_NSSRO_MASK)  // NVM Subsystem Reset Occurred

enum NvmeAqaShift {
    AQA_ASQS_SHIFT  = 0,
    AQA_ACQS_SHIFT  = 16,
};

enum NvmeAqaMask {
    AQA_ASQS_MASK   = 0xfff,
    AQA_ACQS_MASK   = 0xfff,
};

/**
 * solesie: Define macros for NvmeBar.aqa 32bit format.
 */
#define NVME_AQA_ASQS(aqa) ((aqa >> AQA_ASQS_SHIFT) & AQA_ASQS_MASK)    // Admin Submission Queue Size
#define NVME_AQA_ACQS(aqa) ((aqa >> AQA_ACQS_SHIFT) & AQA_ACQS_MASK)    // Admin Completion Queue Size

enum NvmeCmblocShift {
    CMBLOC_BIR_SHIFT  = 0,
    CMBLOC_OFST_SHIFT = 12,
};

enum NvmeCmblocMask {
    CMBLOC_BIR_MASK  = 0x7,
    CMBLOC_OFST_MASK = 0xfffff,
};

/**
 * solesie: Define macros for NvmeBar.CMBLOC 32bit format.
 */
#define NVME_CMBLOC_BIR(cmbloc) ((cmbloc >> CMBLOC_BIR_SHIFT)  & CMBLOC_BIR_MASK)           // Base Indicator Register
#define NVME_CMBLOC_OFST(cmbloc)((cmbloc >> CMBLOC_OFST_SHIFT) & CMBLOC_OFST_MASK)          // Offset

#define NVME_CMBLOC_SET_BIR(cmbloc, val)   (cmbloc |= (uint64_t)(val & CMBLOC_BIR_MASK)  \
                                                                   << CMBLOC_BIR_SHIFT)
#define NVME_CMBLOC_SET_OFST(cmbloc, val)  (cmbloc |= (uint64_t)(val & CMBLOC_OFST_MASK)  \
                                                                   << CMBLOC_OFST_SHIFT)

enum NvmeCmbszShift {
    CMBSZ_SQS_SHIFT   = 0,
    CMBSZ_CQS_SHIFT   = 1,
    CMBSZ_LISTS_SHIFT = 2,
    CMBSZ_RDS_SHIFT   = 3,
    CMBSZ_WDS_SHIFT   = 4,
    CMBSZ_SZU_SHIFT   = 8,
    CMBSZ_SZ_SHIFT    = 12,
};

enum NvmeCmbszMask {
    CMBSZ_SQS_MASK   = 0x1,
    CMBSZ_CQS_MASK   = 0x1,
    CMBSZ_LISTS_MASK = 0x1,
    CMBSZ_RDS_MASK   = 0x1,
    CMBSZ_WDS_MASK   = 0x1,
    CMBSZ_SZU_MASK   = 0xf,
    CMBSZ_SZ_MASK    = 0xfffff,
};

/**
 * solesie: Define macros for NvmeBar.CMBSZ 32bit format.
 * Maybe not use in FEMU.
 */
#define NVME_CMBSZ_SQS(cmbsz)  ((cmbsz >> CMBSZ_SQS_SHIFT)   & CMBSZ_SQS_MASK)      // Submission Queue Support
#define NVME_CMBSZ_CQS(cmbsz)  ((cmbsz >> CMBSZ_CQS_SHIFT)   & CMBSZ_CQS_MASK)      // Completion Queue Support
#define NVME_CMBSZ_LISTS(cmbsz)((cmbsz >> CMBSZ_LISTS_SHIFT) & CMBSZ_LISTS_MASK)    // PRP SGL List Support
#define NVME_CMBSZ_RDS(cmbsz)  ((cmbsz >> CMBSZ_RDS_SHIFT)   & CMBSZ_RDS_MASK)      // Read Data Support
#define NVME_CMBSZ_WDS(cmbsz)  ((cmbsz >> CMBSZ_WDS_SHIFT)   & CMBSZ_WDS_MASK)      // Write Data Support
#define NVME_CMBSZ_SZU(cmbsz)  ((cmbsz >> CMBSZ_SZU_SHIFT)   & CMBSZ_SZU_MASK)      // Size Units
#define NVME_CMBSZ_SZ(cmbsz)   ((cmbsz >> CMBSZ_SZ_SHIFT)    & CMBSZ_SZ_MASK)       // Size

#define NVME_CMBSZ_SET_SQS(cmbsz, val)   (cmbsz |= (uint64_t)(val & CMBSZ_SQS_MASK)  \
                                                                << CMBSZ_SQS_SHIFT)
#define NVME_CMBSZ_SET_CQS(cmbsz, val)   (cmbsz |= (uint64_t)(val & CMBSZ_CQS_MASK)  \
                                                                 << CMBSZ_CQS_SHIFT)
#define NVME_CMBSZ_SET_LISTS(cmbsz, val) (cmbsz |= (uint64_t)(val & CMBSZ_LISTS_MASK)  \
                                                                << CMBSZ_LISTS_SHIFT)
#define NVME_CMBSZ_SET_RDS(cmbsz, val)   (cmbsz |= (uint64_t)(val & CMBSZ_RDS_MASK)  \
                                                                << CMBSZ_RDS_SHIFT)
#define NVME_CMBSZ_SET_WDS(cmbsz, val)   (cmbsz |= (uint64_t)(val & CMBSZ_WDS_MASK)  \
                                                                << CMBSZ_WDS_SHIFT)
#define NVME_CMBSZ_SET_SZU(cmbsz, val)   (cmbsz |= (uint64_t)(val & CMBSZ_SZU_MASK)  \
                                                                << CMBSZ_SZU_SHIFT)
#define NVME_CMBSZ_SET_SZ(cmbsz, val)    (cmbsz |= (uint64_t)(val & CMBSZ_SZ_MASK)  \
                                                                << CMBSZ_SZ_SHIFT)

#define NVME_CMBSZ_GETSIZE(cmbsz) (NVME_CMBSZ_SZ(cmbsz) * (1<<(12+4*NVME_CMBSZ_SZU(cmbsz))))

/**
 * solesie: Controller Properties end
 */



/**
 * solesie: SQE, CQE Data Structures start
 */

/**
 * solesie: SGL Data Block Descriptor 16bytes format.
 */
typedef struct QEMU_PACKED NvmeSglDescriptor {
    uint64_t addr;
    uint32_t len;
    uint8_t  rsvd[3];
    uint8_t  type;
} NvmeSglDescriptor;

#define NVME_SGL_TYPE(type)     ((type >> 4) & 0xf)
#define NVME_SGL_SUBTYPE(type)  (type & 0xf)

/**
 * solesie: DPTR of Common Command Format
 */
typedef union NvmeCmdDptr {
    struct {
        uint64_t    prp1;
        uint64_t    prp2;
    };

    NvmeSglDescriptor sgl;
} NvmeCmdDptr;

/**
 * solesie: PRP or SGL for Data Transfer
 */
enum NvmePsdt {
    NVME_PSDT_PRP                 = 0x0,
    NVME_PSDT_SGL_MPTR_CONTIGUOUS = 0x1,
    NVME_PSDT_SGL_MPTR_SGL        = 0x2,
};

typedef struct nvme_zone_info_entry {
	uint8_t		zone_condition_rsvd : 4;
	uint8_t		rsvd0 : 4;
	uint8_t		rsvd1 : 4;
	uint8_t		zone_condition : 4;
	uint8_t		rsvd8[6];
	uint64_t		zone_capacity;
	uint64_t		zone_start_lba;
	uint64_t		write_pointer;
	uint64_t		cnt_read;
	uint64_t		cnt_write;
	uint32_t		cnt_reset;
	uint8_t		rsvd56[12];
}nvme_zone_info_entry;

typedef struct nvme_passthru_cmd {
	uint8_t	opcode;
	uint8_t	flags;
	uint16_t	rsvd1;
	uint32_t	nsid;
	uint32_t	cdw2;
	uint32_t	cdw3;
	uint64_t	metadata;
	uint64_t	addr;
	uint32_t	metadata_len;
	uint32_t	data_len;
	uint32_t	cdw10;
	uint32_t	cdw11;
	uint32_t	cdw12;
	uint32_t	cdw13;
	uint32_t	cdw14;
	uint32_t	cdw15;
	uint32_t	timeout_ms;
	uint32_t	result;
}nvme_passthru_cmd;

/**
 * solesie: Common Command Format
 */
typedef struct NvmeCmd {
    uint16_t    opcode : 8;     // Common Dword 0
    uint16_t    fuse   : 2;     // cdw 0
    uint16_t    res1   : 4;     // cdw 0
    uint16_t    psdt   : 2;     // cdw 0
    uint16_t    cid;            // Command Identifier of cdw 0
    uint32_t    nsid;
    uint64_t    res2;
    uint64_t    mptr;
    NvmeCmdDptr dptr;
    uint32_t    cdw10;
    uint32_t    cdw11;
    uint32_t    cdw12;
    uint32_t    cdw13;
    uint32_t    cdw14;
    uint32_t    cdw15;
} NvmeCmd;

#define NVME_CMD_FLAGS_FUSE(flags) (flags & 0x3)
#define NVME_CMD_FLAGS_PSDT(flags) ((flags >> 6) & 0x3)

/**
 * solesie: Opcodes for Admin Commands
 */
enum NvmeAdminCommands {
    NVME_ADM_CMD_DELETE_SQ      = 0x00,
    NVME_ADM_CMD_CREATE_SQ      = 0x01,
    NVME_ADM_CMD_GET_LOG_PAGE   = 0x02,
    NVME_ADM_CMD_DELETE_CQ      = 0x04,
    NVME_ADM_CMD_CREATE_CQ      = 0x05,
    NVME_ADM_CMD_IDENTIFY       = 0x06,
    NVME_ADM_CMD_ABORT          = 0x08,
    NVME_ADM_CMD_SET_FEATURES   = 0x09,
    NVME_ADM_CMD_GET_FEATURES   = 0x0a,
    NVME_ADM_CMD_ASYNC_EV_REQ   = 0x0c,
    NVME_ADM_CMD_ACTIVATE_FW    = 0x10,
    NVME_ADM_CMD_DOWNLOAD_FW    = 0x11,
    NVME_ADM_CMD_SET_DB_MEMORY  = 0x7c,     // solesie: Doorbell Buffer Config
    NVME_ADM_CMD_FORMAT_NVM     = 0x80,
    NVME_ADM_CMD_SECURITY_SEND  = 0x81,
    NVME_ADM_CMD_SECURITY_RECV  = 0x82,

    NVME_ADM_CMD_CONF_DEBUG     = 0xec,
    NVME_ADM_CMD_FEMU_DEBUG     = 0xee,
    NVME_ADM_CMD_FEMU_FLIP      = 0xef,

    NVME_ADM_CMD_NS_MGMT        = 0x0d,     // solesie: TODO: Not implemented in FEMU.
    NVME_AMD_CMD_NS_ATTACH      = 0x15,     // solesie: TODO: Not implemented in FEMU.
    NVME_ADM_CMD_CAPA_MANAGE    = 0x20,     // solesie: TODO: Not implemented in FEMU.
};

/**
 * solesie: Opcodes for IO Commands
 */
enum NvmeIoCommands {
    NVME_CMD_FLUSH              = 0x00,
    NVME_CMD_WRITE              = 0x01,
    NVME_CMD_READ               = 0x02,
    NVME_CMD_WRITE_UNCOR        = 0x04,
    NVME_CMD_COMPARE            = 0x05,
    NVME_CMD_WRITE_ZEROES       = 0x08,
    NVME_CMD_DSM                = 0x09,     // Dataset Management
    NVME_CMD_ZONE_MGMT_SEND     = 0x79,
    NVME_CMD_ZONE_MGMT_RECV     = 0x7a,
    NVME_CMD_ZONE_APPEND        = 0x7d,
    NVME_CMD_OC_ERASE           = 0x90,
    NVME_CMD_OC_WRITE           = 0x91,
    NVME_CMD_OC_READ            = 0x92,

    NVME_CMD_IO_MGMT_SEND       = 0x1d,     // solesie: TODO: Not implemented in FEMU.
    NVME_CMD_IO_MGMT_RECV       = 0x12      // solesie: TODO: Not implemented in FEMU.
};

/**
 * solesie: Delete SQ/CQ Command format (Admin Command)
 */
typedef struct NvmeDeleteQ {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    rsvd1[9];
    uint16_t    qid;        // Queue Identifier
    uint16_t    rsvd10;
    uint32_t    rsvd11[5];
} NvmeDeleteQ;

/**
 * solesie: Create CQ Command format (Admin Command)
 */
typedef struct NvmeCreateCq {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    rsvd1[5];
    uint64_t    prp1;
    uint64_t    rsvd8;
    uint16_t    cqid;       // Queue Identifier
    uint16_t    qsize;      // Queue Size
    uint16_t    cq_flags;   // Interrupts Enabled (IEN) and Physically Contiguous (PC) of Dword 11
    uint16_t    irq_vector; // Interrupt Vector (IV)
    uint32_t    rsvd12[4];
} NvmeCreateCq;

#define NVME_CQ_FLAGS_PC(cq_flags)  (cq_flags & 0x1)
#define NVME_CQ_FLAGS_IEN(cq_flags) ((cq_flags >> 1) & 0x1)

/**
 * solesie: Create SQ Command format (Admin Command)
 */
typedef struct NvmeCreateSq {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    rsvd1[5];
    uint64_t    prp1;
    uint64_t    rsvd8;
    uint16_t    sqid;
    uint16_t    qsize;
    uint16_t    sq_flags;   // Queue Priority (QPRIO) and Physically Contiguous (PC) of Dword 11
    uint16_t    cqid;
    uint32_t    rsvd12[4];
} NvmeCreateSq;

#define NVME_SQ_FLAGS_PC(sq_flags)      (sq_flags & 0x1)
#define NVME_SQ_FLAGS_QPRIO(sq_flags)   ((sq_flags >> 1) & 0x3)

/**
 * solesie: NVMeCreateSq.sq_flags.
 */
enum NvmeQueueFlags {
    NVME_Q_PC           = 1,
    NVME_Q_PRIO_URGENT  = 0,
    NVME_Q_PRIO_HIGH    = 1,
    NVME_Q_PRIO_NORMAL  = 2,
    NVME_Q_PRIO_LOW     = 3,
};

/**
 * solesie: Identify Command format (Admin Command)
 */
typedef struct NvmeIdentity {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint64_t    rsvd2[2];
    uint64_t    prp1;
    uint64_t    prp2;
    uint32_t    cns;        // Controller or Namespace Structure
    uint16_t    nvmsetid;   // CNS Specific Identifier (CNSSID)
    uint8_t     rsvd11;
    uint8_t     csi;        // Command Set Identifier
    uint32_t    rsvd12[4];
} NvmeIdentify;

typedef struct NvmeRwCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint64_t    rsvd2;
    uint64_t    mptr;
    uint64_t    prp1;
    uint64_t    prp2;
    uint64_t    slba;
    uint16_t    nlb;
    uint16_t    control;
    uint8_t     dsmgmt;
    uint8_t     rsvd;
    uint16_t    dspec;
    uint32_t    reftag;
    uint16_t    apptag;
    uint16_t    appmask;
} NvmeRwCmd;

/**
 * solesie: NvmeRwCmd has various formats depending on the situation. 
 * To accommodate this, the following enum is defined.
 */
enum {
    NVME_RW_LR                  = 1 << 15,      // Limited Retry
    NVME_RW_FUA                 = 1 << 14,      // Force Unit Access
    NVME_RW_DSM_FREQ_UNSPEC     = 0,
    NVME_RW_DSM_FREQ_TYPICAL    = 1,
    NVME_RW_DSM_FREQ_RARE       = 2,
    NVME_RW_DSM_FREQ_READS      = 3,
    NVME_RW_DSM_FREQ_WRITES     = 4,
    NVME_RW_DSM_FREQ_RW         = 5,
    NVME_RW_DSM_FREQ_ONCE       = 6,
    NVME_RW_DSM_FREQ_PREFETCH   = 7,
    NVME_RW_DSM_FREQ_TEMP       = 8,
    NVME_RW_DSM_LATENCY_NONE    = 0 << 4,
    NVME_RW_DSM_LATENCY_IDLE    = 1 << 4,
    NVME_RW_DSM_LATENCY_NORM    = 2 << 4,
    NVME_RW_DSM_LATENCY_LOW     = 3 << 4,
    NVME_RW_DSM_SEQ_REQ         = 1 << 6,
    NVME_RW_DSM_COMPRESSED      = 1 << 7,
    NVME_RW_PRINFO_PRACT        = 1 << 13,
    NVME_RW_PRINFO_PRCHK_GUARD  = 1 << 12,
    NVME_RW_PRINFO_PRCHK_APP    = 1 << 11,
    NVME_RW_PRINFO_PRCHK_REF    = 1 << 10,
};

/**
 * solesie: Dataset Management Command format (IO Command)
 */
typedef struct NvmeDsmCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint64_t    rsvd2[2];
    uint64_t    prp1;
    uint64_t    prp2;
    uint32_t    nr;
    uint32_t    attributes;
    uint32_t    rsvd12[4];
} NvmeDsmCmd;

enum {
    NVME_DSMGMT_IDR = 1 << 0,
    NVME_DSMGMT_IDW = 1 << 1,
    NVME_DSMGMT_AD  = 1 << 2,   // Deallocate
};

typedef struct NvmeDsmRange {
    uint32_t    cattr;
    uint32_t    nlb;
    uint64_t    slba;
} NvmeDsmRange;

enum NvmeAsyncEventRequest {
    // Asynchronous Event Type (AET)

    NVME_AER_TYPE_ERROR                     = 0,
    NVME_AER_TYPE_SMART                     = 1,
    NVME_AER_TYPE_IO_SPECIFIC               = 6,
    NVME_AER_TYPE_VENDOR_SPECIFIC           = 7,

    // Asynchronous Event Information – Error Status

    NVME_AER_INFO_ERR_INVALID_SQ            = 0,
    NVME_AER_INFO_ERR_INVALID_DB            = 1,
    NVME_AER_INFO_ERR_DIAG_FAIL             = 2,
    NVME_AER_INFO_ERR_PERS_INTERNAL_ERR     = 3,
    NVME_AER_INFO_ERR_TRANS_INTERNAL_ERR    = 4,
    NVME_AER_INFO_ERR_FW_IMG_LOAD_ERR       = 5,

    // Asynchronous Event Information – SMART / Health Status

    NVME_AER_INFO_SMART_RELIABILITY         = 0,
    NVME_AER_INFO_SMART_TEMP_THRESH         = 1,
    NVME_AER_INFO_SMART_SPARE_THRESH        = 2,
};

/**
 * solesie: Asynchronous Event Request – Completion Queue Entry Dword 0
 */
typedef struct NvmeAerResult {
    uint8_t event_type;
    uint8_t event_info;
    uint8_t log_page;
    uint8_t resv;
} NvmeAerResult;

typedef struct NvmeCqe {
    union {
        struct {
            uint32_t    result;
            uint32_t    rsvd;
        } n;
        uint64_t res64;
    };
    uint16_t    sq_head;
    uint16_t    sq_id;
    uint16_t    cid;
    uint16_t    status;
} NvmeCqe;

/**
 * solesie: A of 0xABCD indicates DNR(Do Not Retry) or M(More) of CQE,
 * B indicates SCT(Status Code Type) and 
 * CD indicates SC(Status Code).
 */
enum NvmeStatusCodes {
    // solesie: Generic Command Status (B = 0) start

    NVME_SUCCESS                = 0x0000,
    NVME_INVALID_OPCODE         = 0x0001,
    NVME_INVALID_FIELD          = 0x0002,
    NVME_CID_CONFLICT           = 0x0003,
    NVME_DATA_TRAS_ERROR        = 0x0004,
    NVME_POWER_LOSS_ABORT       = 0x0005,
    NVME_INTERNAL_DEV_ERROR     = 0x0006,
    NVME_CMD_ABORT_REQ          = 0x0007,
    NVME_CMD_ABORT_SQ_DEL       = 0x0008,
    NVME_CMD_ABORT_FAILED_FUSE  = 0x0009,
    NVME_CMD_ABORT_MISSING_FUSE = 0x000a,
    NVME_INVALID_NSID           = 0x000b,
    NVME_CMD_SEQ_ERROR          = 0x000c,
    NVME_FDP_DISABLED           = 0x0029,   // solesie: FDP
    NVME_INVALID_PHNDL          = 0x002a,   // solesie: FDP
    NVME_LBA_RANGE              = 0x0080,
    NVME_CAP_EXCEEDED           = 0x0081,
    NVME_NS_NOT_READY           = 0x0082,
    NVME_NS_RESV_CONFLICT       = 0x0083,

    // solesie: Command Specific Status (B = 1) start

    NVME_INVALID_CQID           = 0x0100,
    NVME_INVALID_QID            = 0x0101,
    NVME_MAX_QSIZE_EXCEEDED     = 0x0102,
    NVME_ACL_EXCEEDED           = 0x0103,
    NVME_RESERVED               = 0x0104,
    NVME_AER_LIMIT_EXCEEDED     = 0x0105,
    NVME_INVALID_FW_SLOT        = 0x0106,
    NVME_INVALID_FW_IMAGE       = 0x0107,
    NVME_INVALID_IRQ_VECTOR     = 0x0108,
    NVME_INVALID_LOG_ID         = 0x0109,
    NVME_INVALID_FORMAT         = 0x010a,
    NVME_FW_REQ_RESET           = 0x010b,
    NVME_INVALID_QUEUE_DEL      = 0x010c,
    NVME_FID_NOT_SAVEABLE       = 0x010d,
    NVME_FID_NOT_NSID_SPEC      = 0x010f,
    NVME_FW_REQ_SUSYSTEM_RESET  = 0x0110,
    NVME_INVALID_CMD_SET        = 0x012c,   // solesie: 기존 FEMU의 0x002c 에서 변경됨.
    NVME_CONFLICTING_ATTRS      = 0x0180,
    NVME_INVALID_PROT_INFO      = 0x0181,
    NVME_WRITE_TO_RO            = 0x0182,
    NVME_ZONE_BOUNDARY_ERROR    = 0x01b8,
    NVME_ZONE_FULL              = 0x01b9,
    NVME_ZONE_READ_ONLY         = 0x01ba,
    NVME_ZONE_OFFLINE           = 0x01bb,
    NVME_ZONE_INVALID_WRITE     = 0x01bc,
    NVME_ZONE_TOO_MANY_ACTIVE   = 0x01bd,
    NVME_ZONE_TOO_MANY_OPEN     = 0x01be,
    NVME_ZONE_INVAL_TRANSITION  = 0x01bf,
    NVME_INVALID_MEMORY_ADDRESS = 0x01C0,

    // solesie: Media and Data Integrity Errors (B = 2) start

    NVME_WRITE_FAULT            = 0x0280,
    NVME_UNRECOVERED_READ       = 0x0281,
    NVME_E2E_GUARD_ERROR        = 0x0282,
    NVME_E2E_APP_ERROR          = 0x0283,
    NVME_E2E_REF_ERROR          = 0x0284,
    NVME_CMP_FAILURE            = 0x0285,
    NVME_ACCESS_DENIED          = 0x0286,
    NVME_DULB                   = 0x0287,

    // solesie: DNR(Do Not Retry) or M(More) start

    NVME_MORE                   = 0x2000,
    NVME_DNR                    = 0x4000,
    NVME_NO_COMPLETE            = 0xffff,
};

/**
 * solesie: SQE, CQE Data Structures end
 */



/**
 * solesie: Admin Command Set start
 */

/**
 * solesie: Define macros for Identify I/O Command Set data structure (CNS 1Ch)
 */
#define NVME_SET_CSI(vec, csi) (vec |= (uint8_t)(1 << (csi)))

enum NvmeDirectiveTypes {
    NVME_DIRECTIVE_IDENTIFY       = 0x0,
    NVME_DIRECTIVE_DATA_PLACEMENT = 0x2,
};

/**
 * solesie: Firmware Slot Information (Log Page Identifier 03h)
 */
typedef struct NvmeFwSlotInfoLog {
    uint8_t     afi;            // Active Firmware Info
    uint8_t     reserved1[7];
    uint8_t     frs1[8];        // Firmware Revision for Slot 1
    uint8_t     frs2[8];
    uint8_t     frs3[8];
    uint8_t     frs4[8];
    uint8_t     frs5[8];
    uint8_t     frs6[8];
    uint8_t     frs7[8];
    uint8_t     reserved2[448];
} NvmeFwSlotInfoLog;

/**
 * solesie: Error Information (Log Page Identifier 01h)
 */
typedef struct NvmeErrorLog {
    uint64_t    error_count;
    uint16_t    sqid;                   // Submission Queue ID
    uint16_t    cid;                    // Command ID
    uint16_t    status_field;
    uint16_t    param_error_location;
    uint64_t    lba;
    uint32_t    nsid;
    uint8_t     vs;
    uint8_t     resv[35];
} NvmeErrorLog;

/**
 * solesie: SMART / Health Information (Log Page Identifier 02h)
 */
typedef struct NvmeSmartLog {
    uint8_t     critical_warning;
    uint8_t     temperature[2];
    uint8_t     available_spare;            // Contains a normalized percentage (0% to 100%) of the remaining spare capacity available.
    uint8_t     available_spare_threshold;
    uint8_t     percentage_used;
    uint8_t     reserved1[26];
    uint64_t    data_units_read[2];
    uint64_t    data_units_written[2];
    uint64_t    host_read_commands[2];
    uint64_t    host_write_commands[2];
    uint64_t    controller_busy_time[2];
    uint64_t    power_cycles[2];
    uint64_t    power_on_hours[2];
    uint64_t    unsafe_shutdowns[2];
    uint64_t    media_errors[2];
    uint64_t    number_of_error_log_entries[2];
    uint8_t     reserved2[320];
} NvmeSmartLog;

/**
 * solesie: NvmeSmartLog.critical_warning
 */
enum NvmeSmartWarn {
    NVME_SMART_SPARE                  = 1 << 0,
    NVME_SMART_TEMPERATURE            = 1 << 1,
    NVME_SMART_RELIABILITY            = 1 << 2,
    NVME_SMART_MEDIA_READ_ONLY        = 1 << 3,
    NVME_SMART_FAILED_VOLATILE_MEDIA  = 1 << 4,
};

/**
 * solesie: Commands Supported and Effects (Log Page Identifier 05h)
 */
typedef struct NvmeEffectsLog {
    uint32_t    acs[256];       // Admin Command Supported
    uint32_t    iocs[256];      // I/O Command Supported
    uint8_t     resv[2048];
} NvmeEffectsLog;

/**
 * solesie: Endurance Group Information (Log Page Identifier 09h)
 */
typedef struct QEMU_PACKED NvmeEndGrpLog {
    uint8_t  critical_warning;
    uint8_t  rsvd[2];
    uint8_t  avail_spare;
    uint8_t  avail_spare_thres;
    uint8_t  percet_used;
    uint8_t  rsvd1[26];
    uint64_t end_estimate[2];               // Endurance Estimate
    uint64_t data_units_read[2];
    uint64_t data_units_written[2];
    uint64_t media_units_written[2];        // include GC
    uint64_t host_read_commands[2];
    uint64_t host_write_commands[2];
    uint64_t media_integrity_errors[2];
    uint64_t no_err_info_log_entries[2];
    uint8_t rsvd2[352];
} NvmeEndGrpLog;

enum {
    NVME_CMD_EFF_CSUPP      = 1 << 0,   // Command Supported
    NVME_CMD_EFF_LBCC       = 1 << 1,   // Logical Block Content Change
    NVME_CMD_EFF_NCC        = 1 << 2,   // Namespace Capability Change
    NVME_CMD_EFF_NIC        = 1 << 3,   // Namespace Inventory Change
    NVME_CMD_EFF_CCC        = 1 << 4,   // Controller Capability Change
    NVME_CMD_EFF_CSE_MASK   = 3 << 16,  // Command Submission and Execution
    NVME_CMD_EFF_UUID_SEL   = 1 << 19,  // UUID Selection Supported
};

enum LogIdentifier {
    NVME_LOG_ERROR_INFO     = 0x01,
    NVME_LOG_SMART_INFO     = 0x02,
    NVME_LOG_FW_SLOT_INFO   = 0x03,
    NVME_LOG_CMD_EFFECTS    = 0x05,

    NVME_LOG_ENDGRP         = 0x09,
};

/**
 * solesie: Power State Descriptor Data Structure
 */
typedef struct NvmePSD {
    uint16_t    mp;
    uint16_t    reserved;
    uint32_t    enlat;
    uint32_t    exlat;
    uint8_t     rrt;
    uint8_t     rrl;
    uint8_t     rwt;
    uint8_t     rwl;
    uint8_t     resv[16];
} NvmePSD;

#define NVME_CONTROLLER_LIST_SIZE 2048
#define NVME_IDENTIFY_DATA_SIZE 4096

/**
 * solesie: Identify - CNS Values
 */
enum NvmeIdCns {
    NVME_ID_CNS_NS                    = 0x00,
    NVME_ID_CNS_CTRL                  = 0x01,
    NVME_ID_CNS_NS_ACTIVE_LIST        = 0x02,
    NVME_ID_CNS_NS_DESCR_LIST         = 0x03,
    NVME_ID_CNS_CS_NS                 = 0x05,
    NVME_ID_CNS_CS_CTRL               = 0x06,
    NVME_ID_CNS_CS_NS_ACTIVE_LIST     = 0x07,
    NVME_ID_CNS_NS_PRESENT_LIST       = 0x10,
    NVME_ID_CNS_NS_PRESENT            = 0x11,
    NVME_ID_CNS_CS_NS_PRESENT_LIST    = 0x1a,
    NVME_ID_CNS_CS_NS_PRESENT         = 0x1b,
    NVME_ID_CNS_IO_COMMAND_SET        = 0x1c,

    NVME_ID_CNS_ENDURANCE_GROUP_LIST  = 0x19,
};

/**
 * solesie: Identify Controller Data Structure (CNS 01h)
 */
typedef struct QEMU_PACKED NvmeIdCtrl {
    // Controller Capabilities and Features start

    uint16_t    vid;            // vendor id
    uint16_t    ssvid;          // PCI Subsystem Vendor ID
    uint8_t     sn[20];         // Serial Number
    uint8_t     mn[40];         // Model Number
    uint8_t     fr[8];          // Firmware Revision
    uint8_t     rab;            // Recommended Arbitration Burst
    uint8_t     ieee[3];        // IEEE OUI Identifier
    uint8_t     cmic;           // Controller Multi-Path I/O and Namespace Sharing Capabilities
    uint8_t     mdts;           // Maximum Data Transfer Size
    uint16_t    cntlid;         // Controller ID
    uint32_t    ver;            // Version
    uint32_t    rtd3r;
    uint32_t    rtd3e;
    uint32_t    oaes;
    uint32_t    ctratt;         // Controller Attributes
    uint8_t     rsvd100[12];
    uint8_t     fguid[16];
    uint8_t     rsvd128[128];
    uint16_t    oacs;           // Optional Admin Command Support
    uint8_t     acl;            // Abort Command Limit
    uint8_t     aerl;           // Asynchronous Event Request Limit
    uint8_t     frmw;           // Firmware Updates
    uint8_t     lpa;            // Log Page Attributes
    uint8_t     elpe;           // Error Log Page Entries
    uint8_t     npss;           // Number of Power States Support
    uint8_t     avscc;          // Admin Vendor Specific Command Configuration
    uint8_t     apsta;          // Autonomous Power State Transition Attributes
    uint16_t    wctemp;         // Warning Composite Temperature Threshold
    uint16_t    cctemp;         // Critical Composite Temperature Threshold
    uint16_t    mtfa;           // Maximum Time for Firmware Activation
    uint32_t    hmpre;          // Host Memory Buffer Preferred Size
    uint32_t    hmmin;          // Host Memory Buffer Minimum Size
    uint8_t     tnvmcap[16];
    uint8_t     unvmcap[16];
    uint32_t    rpmbs;
    uint16_t    edstt;
    uint8_t     dsto;
    uint8_t     fwug;
    uint16_t    kas;
    uint16_t    hctma;
    uint16_t    mntmt;
    uint16_t    mxtmt;
    uint32_t    sanicap;
    uint8_t     rsvd332[8];
    uint16_t    endgidmax;
    uint8_t     rsvd342[170];

    // NVM Command Set Attributes start

    uint8_t     sqes;           // Submission Queue Entry Size
    uint8_t     cqes;           // Completion Queue Entry Size
    uint16_t    maxcmd;
    uint32_t    nn;             // Number of Namespaces
    uint16_t    oncs;           // Optional NVM Command Support
    uint16_t    fuses;          // Fused Operation Support
    uint8_t     fna;            // Format NVM Attributes
    uint8_t     vwc;            // Volatile Write Cache
    uint16_t    awun;           // Atomic Write Unit Normal
    uint16_t    awupf;          // Atomic Write Unit Power Fail
    uint8_t     nvscc;          // I/O Command Set Vendor Specific Command Configuration
    uint8_t     rsvd531;
    uint16_t    acwu;           // Atomic Compare & Write Unit
    uint8_t     rsvd534[2];
    uint32_t    sgls;           // SGL Support
    uint8_t     rsvd540[228];
    uint8_t     subnqn[256];    // NVM Subsystem NVMe Qualified Name
    uint8_t     rsvd1024[1024];

    // Power State Descriptors start

    NvmePSD     psd[32];

    // Vendor Specific start

    uint8_t     vs[1024];
} NvmeIdCtrl;

enum NvmeIdCtrlCtratt {
    NVME_CTRATT_ENDGRPS = 1 <<  4,
    NVME_CTRATT_FDPS    = 1 << 19,
};

/**
 * solesie: NvmeIdCtrl.oacs
 */
enum NvmeIdCtrlOacs {
    NVME_OACS_SECURITY      = 1 << 0,
    NVME_OACS_FORMAT        = 1 << 1,
    NVME_OACS_FW            = 1 << 2,
    NVME_OACS_Oc12DEV       = 1 << 3,
    NVME_OACS_DBBUF         = 1 << 8,
};

/**
 * solesie: NvmeIdCtrl.oncs
 */
enum NvmeIdCtrlOncs {
    NVME_ONCS_COMPARE       = 1 << 0,
    NVME_ONCS_WRITE_UNCORR  = 1 << 1,
    NVME_ONCS_DSM           = 1 << 2,
    NVME_ONCS_WRITE_ZEROS   = 1 << 3,
    NVME_ONCS_FEATURES      = 1 << 4,
    NVME_ONCS_RESRVATIONS   = 1 << 5,
};

enum NvmeIdCtrlFrmw {
    NVME_FRMW_SLOT1_RO = 1 << 0,
};

enum NvmeIdCtrlLpa {
    NVME_LPA_NS_SMART = 1 << 0,
    NVME_LPA_CSE      = 1 << 1,
    NVME_LPA_EXTENDED = 1 << 2,
};

#define NVME_CTRL_SQES_MIN(sqes) ((sqes) & 0xf)
#define NVME_CTRL_SQES_MAX(sqes) (((sqes) >> 4) & 0xf)
#define NVME_CTRL_CQES_MIN(cqes) ((cqes) & 0xf)
#define NVME_CTRL_CQES_MAX(cqes) (((cqes) >> 4) & 0xf)

/**
 * solesie: Set Features
 */
typedef struct NvmeFeatureVal {
    uint32_t    arbitration;
    uint32_t    power_mgmt;
    uint32_t    temp_thresh;
    uint32_t    err_rec;
    uint32_t    volatile_wc;
    uint32_t    num_io_queues;
    uint32_t    int_coalescing;
    uint32_t    *int_vector_config;
    uint32_t    write_atomicity;
    uint32_t    async_config;
    uint32_t    sw_prog_marker;
} NvmeFeatureVal;

/**
 * solesie: NvmeFeatureVal.arbitration
 */
#define NVME_ARB_AB(arb)        (arb & 0x7)             // Arbitration Burst
#define NVME_ARB_LPW(arb)       ((arb >> 8) & 0xff)     // Low Priority Weight
#define NVME_ARB_MPW(arb)       ((arb >> 16) & 0xff)    // Medium Priority Weight
#define NVME_ARB_HPW(arb)       ((arb >> 24) & 0xff)    // High Priority Weight

#define NVME_INTC_THR(intc)     (intc & 0xff)
#define NVME_INTC_TIME(intc)    ((intc >> 8) & 0xff)

/**
 * solesie: NvmeFeatureVal.err_rec
 */
#define NVME_ERR_REC_DULBE(err_rec) (err_rec & 0x10000) // Deallocated or Unwritten Logical Block Error Enable

/**
 * solesie: Set Features – Feature Identifiers
 */
enum NvmeFeatureIds {
    NVME_ARBITRATION                = 0x1,
    NVME_POWER_MANAGEMENT           = 0x2,
    NVME_LBA_RANGE_TYPE             = 0x3,
    NVME_TEMPERATURE_THRESHOLD      = 0x4,
    NVME_ERROR_RECOVERY             = 0x5,
    NVME_VOLATILE_WRITE_CACHE       = 0x6,
    NVME_NUMBER_OF_QUEUES           = 0x7,
    NVME_INTERRUPT_COALESCING       = 0x8,
    NVME_INTERRUPT_VECTOR_CONF      = 0x9,
    NVME_WRITE_ATOMICITY            = 0xa,
    NVME_ASYNCHRONOUS_EVENT_CONF    = 0xb,
    NVME_TIMESTAMP                  = 0xe,

    NVME_FDP                        = 0x1d,
    NVME_FDP_EVENT                  = 0x1e,

    NVME_SOFTWARE_PROGRESS_MARKER   = 0x80,
    NVME_FID_MAX                    = 0x100
};

/**
 * solesie: Get Features Command Completion
 */
typedef enum NvmeFeatureCap {
    NVME_FEAT_CAP_SAVE      = 1 << 0,
    NVME_FEAT_CAP_NS        = 1 << 1,
    NVME_FEAT_CAP_CHANGE    = 1 << 2,
} NvmeFeatureCap;

/**
 * solesie: Get Features Select field
 */
typedef enum NvmeGetFeatureSelect {
    NVME_GETFEAT_SELECT_CURRENT = 0x0,
    NVME_GETFEAT_SELECT_DEFAULT = 0x1,
    NVME_GETFEAT_SELECT_SAVED   = 0x2,
    NVME_GETFEAT_SELECT_CAP     = 0x3,
} NvmeGetFeatureSelect;

/**
 * solesie: Get/Set Features LBA Range Type – Data Structure Entry
 */
typedef struct NvmeRangeType {
    uint8_t     type;
    uint8_t     attributes;
    uint8_t     rsvd2[14];
    uint64_t    slba;
    uint64_t    nlb;
    uint8_t     guid[16];
    uint8_t     rsvd48[16];
} NvmeRangeType;

/**
 * solesie: LBA Format Data Structure, NVM Command Set Specific
 */
typedef struct NvmeLBAF {
    uint16_t    ms;     // Metadata Size
    uint8_t     lbads;  // LBA Data Size
    uint8_t     rp;     // Relative Performance
} NvmeLBAF;

#define NVME_NSID_BROADCAST 0xffffffff

/**
 * solesie: NVM Command Set Identify Namespace Data Structure (CNS 00h)
 */
typedef struct NvmeIdNs {
    uint64_t    nsze;
    uint64_t    ncap;
    uint64_t    nuse;           // Namespace Utilization
    uint8_t     nsfeat;
    uint8_t     nlbaf;          // Number of LBA Formats 
    uint8_t     flbas;          // Formatted LBA Size
    uint8_t     mc;             // Metadata Capabilities
    uint8_t     dpc;            // End-to-end Data Protection Capabilities
    uint8_t     dps;            // End-to-end Data Protection Type Settings
    uint8_t     nmic;           // Namespace Multi-path I/O and Namespace Sharing Capabilities
    uint8_t     rescap;         // Reservation Capabilities
    uint8_t     fpi;            // Format Progress Indicator
    uint8_t     dlfeat;         // Deallocate Logical Block Features
    uint16_t    nawun;          // Namespace Atomic Write Unit Norma
    uint16_t    nawupf;         // Namespace Atomic Write Unit Power Fail
    uint16_t    nacwu;          // Namespace Atomic Compare & Write Unit
    uint16_t    nabsn;          // Namespace Atomic Boundary Size Normal
    uint16_t    nabo;           // Namespace Atomic Boundary Offset
    uint16_t    nabspf;         // Namespace Atomic Boundary Size Power Fail
    uint16_t    noiob;          // Namespace Optimal I/O Boundary
    uint8_t     nvmcap[16];     // NVM Capacity
    uint16_t    npwg;           // Namespace Preferred Write Granularity
    uint16_t    npwa;           // Namespace Preferred Write Alignment
    uint16_t    npdg;           // Namespace Preferred Deallocate Granularity
    uint16_t    npda;           // Namespace Preferred Deallocate Alignment
    uint16_t    nows;           // Namespace Optimal Write Size
    uint8_t     rsvd74[30];
    uint8_t     nguid[16];      // Namespace Globally Unique Identifier
    uint64_t    eui64;          // IEEE Extended Unique Identifier
    NvmeLBAF    lbaf[16];       // LBA Formats
    uint8_t     rsvd192[192];
    uint8_t     vs[3712];
} NvmeIdNs;

/**
 * solesie: Identify – Namespace Identification Descriptor
 */
typedef struct QEMU_PACKED NvmeIdNsDescr {
    uint8_t nidt;
    uint8_t nidl;
    uint8_t rsvd2[2];
} NvmeIdNsDescr;

enum NvmeNsIdentifierLength {
    NVME_NIDL_EUI64             = 8,
    NVME_NIDL_NGUID             = 16,
    NVME_NIDL_UUID              = 16,
    NVME_NIDL_CSI               = 1,
};

enum NvmeNsIdentifierType {
    NVME_NIDT_EUI64             = 0x01,
    NVME_NIDT_NGUID             = 0x02,
    NVME_NIDT_UUID              = 0x03,
    NVME_NIDT_CSI               = 0x04,
};

#define NVME_ID_NS_NSFEAT_THIN(nsfeat)      ((nsfeat & 0x1))
#define NVME_ID_NS_FLBAS_EXTENDED(flbas)    ((flbas >> 4) & 0x1)
#define NVME_ID_NS_FLBAS_INDEX(flbas)       ((flbas & 0xf))
#define NVME_ID_NS_MC_SEPARATE(mc)          ((mc >> 1) & 0x1)
#define NVME_ID_NS_MC_EXTENDED(mc)          ((mc & 0x1))
#define NVME_ID_NS_DPC_LAST_EIGHT(dpc)      ((dpc >> 4) & 0x1)
#define NVME_ID_NS_DPC_FIRST_EIGHT(dpc)     ((dpc >> 3) & 0x1)
#define NVME_ID_NS_DPC_TYPE_3(dpc)          ((dpc >> 2) & 0x1)
#define NVME_ID_NS_DPC_TYPE_2(dpc)          ((dpc >> 1) & 0x1)
#define NVME_ID_NS_DPC_TYPE_1(dpc)          ((dpc & 0x1))
#define NVME_ID_NS_DPC_TYPE_MASK            0x7

enum NvmeIdNsDps {
    DPS_TYPE_NONE   = 0,
    DPS_TYPE_1      = 1,
    DPS_TYPE_2      = 2,
    DPS_TYPE_3      = 3,
    DPS_TYPE_MASK   = 0x7,
    DPS_FIRST_EIGHT = 8,
};

/**
 * Admin Command Set end
 */



static inline void nvme_check_size(void)
{
    QEMU_BUILD_BUG_ON(sizeof(NvmeAerResult) != 4);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCqe) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDsmRange) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDeleteQ) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCreateCq) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCreateSq) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdentify) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeRwCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDsmCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeRangeType) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeErrorLog) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeFwSlotInfoLog) != 512);
    QEMU_BUILD_BUG_ON(sizeof(NvmeSmartLog) != 512);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdCtrl) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdNs) != 4096);

    /* Coperd: FIXME, check FEMU OC structure size */
    //oc12_check_size();
}

typedef struct NvmeAsyncEvent {
    QSIMPLEQ_ENTRY(NvmeAsyncEvent) entry;
    NvmeAerResult result;
} NvmeAsyncEvent;

typedef struct NvmeRequest {
    struct NvmeSQueue       *sq;
    struct NvmeCQueue       *cq;
    struct NvmeNamespace    *ns;
    uint16_t                status;
    uint64_t                slba;
    uint16_t                is_write;
    uint16_t                nlb;
    uint16_t                ctrl;
    uint64_t                meta_size;
    uint64_t                mptr;
    void                    *meta_buf;
    uint64_t                oc12_slba;
    uint64_t                *oc12_ppa_list;
    NvmeCmd                 cmd;
    NvmeCqe                 cqe;
    uint8_t                 cmd_opcode;
    QEMUSGList              qsg;
    QEMUIOVector            iov;
    QTAILQ_ENTRY(NvmeRequest)entry;
    int64_t                 stime;
    int64_t                 reqlat;
    int64_t                 gcrt;
    int64_t                 expire_time;

    /* OC2.0: sector offset relative to slba where reads become invalid */
    uint64_t predef;

    /* ZNS */
    void                    *opaque;

    /* position in the priority queue for delay emulation */
    size_t                  pos;

    struct {
        uint16_t ruh_id;
        uint16_t rg_id;
    } fdp;
} NvmeRequest;

typedef struct DMAOff {
    QEMUSGList *qsg;
    int ndx;
    dma_addr_t ptr;
    dma_addr_t len;
} DMAOff;

typedef struct NvmeSQueue {
    struct FemuCtrl *ctrl;
    uint8_t     phys_contig;
    uint8_t     arb_burst;
    uint16_t    sqid;
    uint16_t    cqid;
    uint32_t    head;
    uint32_t    tail;
    uint32_t    size;
    uint64_t    dma_addr;
    uint64_t    dma_addr_hva;
    uint64_t    completed;
    uint64_t    *prp_list;
    NvmeRequest *io_req;
    QTAILQ_HEAD(sq_req_list, NvmeRequest) req_list;
    QTAILQ_HEAD(out_req_list, NvmeRequest) out_req_list;
    QTAILQ_ENTRY(NvmeSQueue) entry;

    uint64_t    db_addr;
    uint64_t    db_addr_hva;
    uint64_t    eventidx_addr;
    uint64_t    eventidx_addr_hva;
    bool        is_active;
} NvmeSQueue;

typedef struct NvmeCQueue {
    struct FemuCtrl *ctrl;
    uint8_t     phys_contig;
    uint8_t     phase;
    uint16_t    cqid;
    uint16_t    irq_enabled;
    uint32_t    head;
    uint32_t    tail;
    int32_t     virq;
    uint32_t    vector;
    uint32_t    size;
    uint64_t    dma_addr;
    uint64_t    dma_addr_hva;
    uint64_t    *prp_list;
    EventNotifier guest_notifier;
    QEMUTimer   *timer;
    QTAILQ_HEAD(sq_list, NvmeSQueue) sq_list;
    QTAILQ_HEAD(cq_req_list, NvmeRequest) req_list;
    uint64_t    db_addr;
    uint64_t    db_addr_hva;
    uint64_t    eventidx_addr;
    uint64_t    eventidx_addr_hva;
    bool        is_active;
} NvmeCQueue;

typedef struct Oc12Bbt Oc12Bbt;
typedef struct Oc12Ctrl Oc12Ctrl;

typedef struct NvmeIdNsZoned NvmeIdNsZoned;
typedef struct NvmeZone NvmeZone;






typedef struct NvmeReclaimUnit {
    uint64_t ruamw; // solesie: Reclaim Unit Available Media Writes(in # of logical blocks)
} NvmeReclaimUnit;

typedef struct NvmeRuHandle {
    uint8_t  ruht;
    uint8_t  ruha;
    uint64_t event_filter;
    uint8_t  lbafi;
    uint64_t ruamw;

    /* reclaim units indexed by reclaim group */
    NvmeReclaimUnit *rus;
} NvmeRuHandle;

#define NVME_FDP_MAX_EVENTS 63

typedef struct QEMU_PACKED NvmeFdpEvent {
    uint8_t  type;
    uint8_t  flags;
    uint16_t pid;
    uint64_t timestamp;
    uint32_t nsid;
    uint64_t type_specific[2];
    uint16_t rgid;
    uint8_t  ruhid;
    uint8_t  rsvd35[5];
    uint64_t vendor[3];
} NvmeFdpEvent;

typedef struct NvmeFdpEventBuffer {
    NvmeFdpEvent     events[NVME_FDP_MAX_EVENTS];
    unsigned int     nelems;
    unsigned int     start;
    unsigned int     next;
} NvmeFdpEventBuffer;

typedef struct NvmeEnduranceGroup {
    uint8_t event_conf;
    struct FemuCtrl *ctrl;
    struct NvmeNamespace *namespaces;

    struct {
        NvmeFdpEventBuffer host_events, ctrl_events;

        uint16_t nruh;
        uint16_t nrg;
        uint8_t  rgif;
        uint64_t runs;

        uint64_t hbmw;                  // Host Bytes with Metadata Written (not include GC)
        uint64_t mbmw;                  // Media Bytes with Metadata Written
        uint64_t mbe;                   // Media Bytes Erased

        bool enabled;

        NvmeRuHandle *ruhs;
    } fdp;
} NvmeEnduranceGroup;

enum NvmeIomr2Mo {
    NVME_IOMR_MO_NOP = 0x0,
    NVME_IOMR_MO_RUH_STATUS = 0x1,
    NVME_IOMR_MO_VENDOR_SPECIFIC = 0x255,
};

enum NvmeRuhType {
    NVME_RUHT_INITIALLY_ISOLATED = 1,
    NVME_RUHT_PERSISTENTLY_ISOLATED = 2,
};

enum NvmeRuhAttributes {
    NVME_RUHA_UNUSED = 0,
    NVME_RUHA_HOST = 1,
    NVME_RUHA_CTRL = 2,
};

typedef struct QEMU_PACKED NvmeRuhStatus {
    uint8_t  rsvd0[14];
    uint16_t nruhsd;
} NvmeRuhStatus;

typedef struct QEMU_PACKED NvmeRuhStatusDescr {
    uint16_t pid;
    uint16_t ruhid;
    uint32_t earutr;
    uint64_t ruamw;
    uint8_t  rsvd16[16];
} NvmeRuhStatusDescr;




typedef struct NvmeNamespace {
    struct FemuCtrl *ctrl;
    NvmeIdNs        id_ns;
    NvmeRangeType   lba_range[64];
    unsigned long   *util;
    unsigned long   *uncorrectable;
    uint32_t        id;
    uint64_t        size; /* Coperd: for ZNS, FIXME */
    uint64_t        ns_blks;
    uint64_t        start_block;
    uint64_t        meta_start_offset;
    uint64_t        tbl_dsk_start_offset;
    uint32_t        tbl_entries;
    uint64_t        *tbl;
    Oc12Bbt   **bbtbl;

    /* Coperd: OC20 */
    struct {
        uint64_t begin;
        uint64_t predef;
        uint64_t data;
        uint64_t meta;
    } blk;
    void *state;

    /* solesie: FDP */
    NvmeEnduranceGroup *endgrp;
    struct {
        uint16_t nphs;
        /* reclaim unit handle identifiers indexed by placement handle */
        uint16_t *phs;
    } fdp;
} NvmeNamespace;




#define TYPE_NVME "femu"
#define FEMU(obj) OBJECT_CHECK(FemuCtrl, (obj), TYPE_NVME)

/* Coperd: OC2.0 */
typedef struct Oc20Params {
    /* qemu configurable device characteristics */
    uint32_t mccap;
    uint32_t ws_min;
    uint32_t ws_opt;
    uint32_t mw_cunits;

    uint8_t debug;
    uint8_t early_reset;
    uint8_t	sgl_lbal;

    char *chunkstate_fname;
    char *resetfail_fname;
    char *writefail_fname;
} Oc20Params;

typedef struct FdpParams {
    uint64_t runs;
    int nrg;
    int chs_per_rg;
    int ways_per_rg;
    int nruh;
} FdpParams;

typedef struct ConfParams {
    int secsz;
	int secs_per_pg;
	int pgs_per_blk;
	int pls_per_lun;
	int luns_per_ch;
	int nchs;

	int pg_rd_lat;
	int pg_wr_lat;
	int blk_er_lat;
	int ch_xfer_lat;

	int gc_thres_pcent;
	int gc_thres_pcent_high;
} ConfParams;

typedef struct NvmeParams {
    char     *serial;
    uint32_t num_namespaces;
    uint32_t num_queues;
    uint32_t max_q_ents;
    uint8_t  max_sqes;
    uint8_t  max_cqes;
    uint8_t  db_stride;
    uint8_t  aerl;
    uint8_t  acl;
    uint8_t  elpe;
    uint8_t  mdts;
    uint8_t  cqr;
    uint8_t  vwc;
    uint8_t  dpc;
    uint8_t  dps;
    uint8_t  intc;
    uint8_t  intc_thresh;
    uint8_t  intc_time;
    uint8_t  extended;
    uint8_t  mpsmin;
    uint8_t  mpsmax;
    uint8_t  ms;
    uint8_t  ms_max;
    uint8_t  mc;
    uint16_t vid;
    uint16_t did;
    uint8_t  dlfeat;
    uint32_t cmb_size_mb;
    uint8_t  dialect;
    uint16_t oacs;
    uint16_t oncs;

    /* dialects */
    Oc20Params oc20;
} NvmeParams;

/* do NOT go beyound 256 (uint8_t) */
#define FEMU_MAX_NUM_CHNLS (32)
#define FEMU_MAX_NUM_CHIPS (128)

typedef struct OcCtrlParams {
    uint16_t sec_size;
    uint8_t  secs_per_pg;
    uint16_t pgs_per_blk;
    uint8_t  max_sec_per_rq;
    uint8_t  num_ch;
    uint8_t  num_lun;
    uint8_t  num_pln;
    uint16_t sos;
} OcCtrlParams;

struct FemuCtrl;
typedef struct FemuExtCtrlOps {
    void     *state;
    void     (*init)(struct FemuCtrl *, Error **);
    void     (*exit)(struct FemuCtrl *);
    uint16_t (*rw_check_req)(struct FemuCtrl *, NvmeCmd *, NvmeRequest *);
    int      (*start_ctrl)(struct FemuCtrl *);
    uint16_t (*admin_cmd)(struct FemuCtrl *, NvmeCmd *);
    uint16_t (*io_cmd)(struct FemuCtrl *, NvmeNamespace *, NvmeCmd *, NvmeRequest *);
    uint16_t (*get_log)(struct FemuCtrl *, NvmeCmd *);
} FemuExtCtrlOps;

typedef struct FemuCtrl {
    PCIDevice       parent_obj;
    MemoryRegion    iomem;
    MemoryRegion    ctrl_mem;
    NvmeBar         bar;

    bool            wrr_enable;

    /* Coperd: ZNS FIXME */
    struct zns      *zns;   // for ZNS Latency emualting, Inhoinno
    QemuUUID        uuid;
    uint32_t        zasl_bs;
    uint8_t         zasl;
    bool            zoned;
    bool            cross_zone_read;
    uint64_t        zone_size_bs;
    bool            zone_cap_bs;
    uint32_t        max_active_zones;
    uint32_t        max_open_zones;
    uint32_t        zd_extension_size;
    PCIe_Gen3_x4    *pci_simulation;
    pthread_spinlock_t pci_lock;

    const uint32_t  *iocs;
    uint8_t         csi;
    NvmeIdNsZoned   *id_ns_zoned;
    NvmeZone        *zone_array;
    QTAILQ_HEAD(, NvmeZone) exp_open_zones;
    QTAILQ_HEAD(, NvmeZone) imp_open_zones;
    QTAILQ_HEAD(, NvmeZone) closed_zones;
    QTAILQ_HEAD(, NvmeZone) full_zones;
    uint32_t        num_zones;
    uint64_t        zone_size;
    uint64_t        zone_capacity;
    uint32_t        zone_size_log2;
    uint8_t         *zd_extensions;
    int32_t         nr_open_zones;
    int32_t         nr_active_zones;

    /* Coperd: OC2.0 FIXME */
    NvmeParams  params;
    FemuExtCtrlOps ext_ops;

    time_t      start_time;
    uint16_t    temperature;
    uint16_t    page_size;                  // in bytes
    uint16_t    page_bits;                  // in bits
    uint16_t    max_prp_ents;               // max PRP List
    uint16_t    cqe_size;
    uint16_t    sqe_size;
    uint16_t    oacs;                       // Optional Admin Command Support
    uint16_t    oncs;                       // Optional NVM Command Support
    uint32_t    reg_size;                   // dma register size in bytes
    uint32_t    num_namespaces;
    uint32_t    num_io_queues;
    uint32_t    max_q_ents;                 // Maximum Queue Entries Supported
    uint64_t    ns_size;
    uint8_t     db_stride;                  // doorbell stride
    uint8_t     aerl;                       // Asynchronous Event Request Limit
    uint8_t     acl;                        // Abort Command Limit
    uint8_t     elpe;                       // Error Log Page Entries
    uint8_t     elp_index;
    uint8_t     error_count;
    uint8_t     mdts;                       // Maximum Data Transfer Size
    uint8_t     cqr;                        // Contiguous Queues Required
    uint8_t     max_sqes;
    uint8_t     max_cqes;
    uint8_t     meta;
    uint8_t     vwc;                        // Volatile Write Cache
    uint8_t     mc;                         // Metadata Capabilities
    uint8_t     dpc;                        // End-to-end Data Protection Capabilities
    uint8_t     dps;                        // End-to-end Data Protection Type Settings
    uint8_t     nlbaf;                      // Number of LBA Formats 
    uint8_t     extended;
    uint8_t     lba_index;
    uint8_t     mpsmin;                     // Memory Page Size Minimum
    uint8_t     mpsmax;                     // Memory Page Size Maximum
    uint8_t     ms;                         // Metadata Size
    uint8_t     ms_max;
    uint8_t     intc;                       // Interrupt Coalescing
    uint8_t     intc_thresh;                // intc Aggregation Threshold
    uint8_t     intc_time;                  // intc Aggregation Time
    uint8_t     outstanding_aers;
    uint8_t     temp_warn_issued;
    uint8_t     num_errors;                 // SMART - Number of Error Information Log Entries
    uint8_t     cqes_pending;
    uint16_t    vid;
    uint16_t    did;
    uint8_t     dlfeat;                     // Deallocate Logical Block Features
    uint32_t    cmbsz;                      // Controller Memory Buffer Size
    uint32_t    cmbloc;                     // Controller Memory Buffer Location
    uint8_t     *cmbuf;                     // Controller Memory Buffer

    QemuThread  *poller;
    bool        dataplane_started;
    bool        vector_poll_started;

    char            *serial;
    NvmeErrorLog    *elpes;
    NvmeRequest     **aer_reqs;
    NvmeNamespace   *namespaces;
    NvmeSQueue      **sq;
    NvmeCQueue      **cq;

    NvmeSQueue      **psched_q; //inhoinno : for the WRR sched
    //NvmeCQueue      **psched_cq;

    NvmeSQueue      admin_sq;
    NvmeCQueue      admin_cq;
    NvmeFeatureVal  features;
    NvmeIdCtrl      id_ctrl;

    /* solesie: FDP */
    NvmeEnduranceGroup *endgrp;
    FdpParams           fdp_params;
    ConfParams          conf_params;
    struct fdpssd       *fdpssd;

    QSIMPLEQ_HEAD(aer_queue, NvmeAsyncEvent) aer_queue;
    QEMUTimer       *aer_timer;
    uint8_t         aer_mask;

	uint64_t		dbs_addr;               // Shadow Doorbell
	uint64_t		eis_addr;               // EventIdx
    uint64_t        dbs_addr_hva;           // Host Virtual Address
    uint64_t        eis_addr_hva;

    uint8_t         femu_mode;
    uint8_t         lver; /* Coperd: OCSSD version, 0x1 -> OC1.2, 0x2 -> OC2.0 */
    uint32_t        memsz;                  // devsz_mb
    OcCtrlParams    oc_params;

    Oc12Ctrl  *oc12_ctrl;
    volatile int64_t chip_next_avail_time[FEMU_MAX_NUM_CHIPS];
    pthread_spinlock_t chip_locks[FEMU_MAX_NUM_CHIPS];
    volatile int64_t chnl_next_avail_time[FEMU_MAX_NUM_CHNLS];
    pthread_spinlock_t chnl_locks[FEMU_MAX_NUM_CHNLS];

    /* Latency numbers for whitebox-mode only */
    int64_t upg_rd_lat_ns; /* upper page in MLC/TLC/QLC */
    int64_t cpg_rd_lat_ns; /* center page in TLC */
    int64_t cupg_rd_lat_ns; /* center-upper page in QLC */
    int64_t clpg_rd_lat_ns; /* center-lower page in QLC */
    int64_t lpg_rd_lat_ns; /* lower page in MLC/TLC/QLC */
    int64_t upg_wr_lat_ns;
    int64_t cpg_wr_lat_ns;
    int64_t cupg_wr_lat_ns;
    int64_t clpg_wr_lat_ns;
    int64_t lpg_wr_lat_ns;
    int64_t blk_er_lat_ns;
    int64_t chnl_pg_xfer_lat_ns;

    struct ssd      *ssd;
    SsdDramBackend  *mbe;
    int             completed;

    char            devname[64];
    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    pqueue_t        **pq;
    bool            *should_isr;            // Interrupt Service Routine
    bool            poller_on;

    int64_t         nr_tt_ios;
    int64_t         nr_tt_late_ios;
    bool            print_log;

    uint8_t         multipoller_enabled;
    uint32_t        num_poller;

    /* Nand Flash Type: SLC/MLC/TLC/QLC/PLC */
    uint8_t         flash_type;
} FemuCtrl;

typedef struct NvmePollerThreadArgument {
    FemuCtrl        *n;
    int             index;
} NvmePollerThreadArgument;

typedef struct NvmeDifTuple {
    uint16_t guard_tag;
    uint16_t app_tag;
    uint32_t ref_tag;
} NvmeDifTuple;

#define SQ_POLLING_PERIOD_NS	(5000)
#define CQ_POLLING_PERIOD_NS	(5000)
#define FEMU_MAX_INF_REQS       (65536)

enum {
    FEMU_OCSSD_MODE = 0,
    FEMU_BBSSD_MODE = 1,
    FEMU_NOSSD_MODE = 2,
    FEMU_ZNSSD_MODE = 3,
    FEMU_SMARTSSD_MODE = 4,
    FEMU_KVSSD_MODE = 5,
    FEMU_FDPSSD_MODE = 6
};

enum {
    OCSSD12 = 0x1,
    OCSSD20 = 0x2,
};

enum OC20AdminCommands {
    OC20_ADM_CMD_IDENTIFY       = 0xe2,
    OC20_ADM_CMD_SET_LOG_PAGE   = 0xc1,
};

static inline bool OCSSD(FemuCtrl *n)
{
    return (n->femu_mode == FEMU_OCSSD_MODE);
}

static inline bool BBSSD(FemuCtrl *n)
{
    return (n->femu_mode == FEMU_BBSSD_MODE);
}

static inline bool NOSSD(FemuCtrl *n)
{
    return (n->femu_mode == FEMU_NOSSD_MODE);
}

static inline bool ZNSSD(FemuCtrl *n)
{
    return (n->femu_mode == FEMU_ZNSSD_MODE);
}

static inline bool FDPSSD(FemuCtrl * n)
{
    return (n->femu_mode == FEMU_FDPSSD_MODE);
}

/* Basic NVMe Queue Pair operation APIs from nvme-util.c */
int nvme_check_sqid(FemuCtrl *n, uint16_t sqid);
int nvme_check_cqid(FemuCtrl *n, uint16_t cqid);
void nvme_inc_cq_tail(NvmeCQueue *cq);
void nvme_inc_sq_head(NvmeSQueue *sq);
void nvme_update_cq_head(NvmeCQueue *cq);
uint8_t nvme_cq_full(NvmeCQueue *cq);
uint8_t nvme_sq_empty(NvmeSQueue *sq);
void nvme_update_sq_tail(NvmeSQueue *sq);
uint16_t nvme_init_sq(NvmeSQueue *sq, FemuCtrl *n, uint64_t dma_addr, uint16_t
                      sqid, uint16_t cqid, uint16_t size, enum NvmeQueueFlags
                      prio, int contig);
void nvme_free_sq(NvmeSQueue *sq, FemuCtrl *n);
void nvme_free_cq(NvmeCQueue *cq, FemuCtrl *n);
uint16_t nvme_init_cq(NvmeCQueue *cq, FemuCtrl *n, uint64_t dma_addr, uint16_t
                      cqid, uint16_t vector, uint16_t size, uint16_t
                      irq_enabled, int contig);
void nvme_set_ctrl_name(FemuCtrl *n, const char *mn, const char *sn, int *dev_id);

/* Public APIs from intr.c for interrupt operations */
void nvme_isr_notify_admin(void *opaque);
void nvme_isr_notify_io(void *opaque);
int nvme_setup_virq(FemuCtrl *n, NvmeCQueue *cq);
int nvme_clear_virq(FemuCtrl *n);

/* Public DMA APIs from dma.c */
void     nvme_addr_read(FemuCtrl *n, hwaddr addr, void *buf, int size);
void     nvme_addr_write(FemuCtrl *n, hwaddr addr, void *buf, int size);
uint16_t nvme_map_prp(QEMUSGList *qsg, QEMUIOVector *iov, uint64_t prp1,
                      uint64_t prp2, uint32_t len, FemuCtrl *n);
uint16_t dma_write_prp(FemuCtrl *n, uint8_t *ptr, uint32_t len, uint64_t
                            prp1, uint64_t prp2);
uint16_t dma_read_prp(FemuCtrl *n, uint8_t *ptr, uint32_t len, uint64_t
                           prp1, uint64_t prp2);


/* Misc */
uint64_t *nvme_setup_discontig(FemuCtrl *n, uint64_t prp_addr, uint16_t
                               queue_depth, uint16_t entry_size);
void nvme_set_error_page(FemuCtrl *n, uint16_t sqid, uint16_t cid, uint16_t
                         status, uint16_t location, uint64_t lba, uint32_t
                         nsid);
uint16_t femu_nvme_rw_check_req(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                                NvmeRequest *req, uint64_t slba, uint64_t elba,
                                uint32_t nlb, uint16_t ctrl, uint64_t data_size,
                                uint64_t meta_size);


void nvme_process_sq_admin(void *opaque);
void nvme_post_cqes_io(void *opaque);
void nvme_create_poller(FemuCtrl *n);

void nvme_init_endgrp(FemuCtrl *n);

/* NVMe I/O */
uint16_t nvme_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

int nvme_register_ocssd12(FemuCtrl *n);
int nvme_register_ocssd20(FemuCtrl *n);
int nvme_register_nossd(FemuCtrl *n);
int nvme_register_bbssd(FemuCtrl *n);
int nvme_register_znssd(FemuCtrl *n);
int nvme_register_fdpssd(FemuCtrl *n);

/**
 * solesie: get num of lba blks of ns
 */
static inline uint64_t ns_blks(NvmeNamespace *ns, uint8_t lba_idx)
{
    FemuCtrl *n = ns->ctrl;
    NvmeIdNs *id_ns = &ns->id_ns;
    uint64_t ns_size = n->ns_size;

    uint32_t lba_ds = (1 << id_ns->lbaf[lba_idx].lbads);
    uint32_t lba_sz = lba_ds + n->meta;

    return ns_size / lba_sz;
}

/**
 * solesie: calculate discontiguous DMA address of queue_idx
 */
static inline hwaddr nvme_discontig(uint64_t *dma_addr, uint16_t queue_idx,
    uint16_t page_size, uint16_t entry_size)
{
    uint16_t entries_per_page = page_size / entry_size;
    uint16_t prp_index = queue_idx / entries_per_page;
    uint16_t index_in_prp = queue_idx % entries_per_page;

    return dma_addr[prp_index] + index_in_prp * entry_size;
}

/**
 * solesie: check len bytes using Maximum Data Transfer Size
 */
static inline uint16_t nvme_check_mdts(FemuCtrl *n, size_t len)
{
    uint8_t mdts = n->mdts;

    if (mdts && len > n->page_size << mdts) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

#define MN_MAX_LEN (64)
#define ID_MAX_LEN (4)

#define FEMU_DEBUG_NVME
#ifndef FEMU_DEBUG_NVME
#define femu_debug(fmt, ...) \
    do { printf("[FEMU] Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define femu_debug(fmt, ...) \
    do { } while (0)
#endif

#define femu_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] Err: " fmt, ## __VA_ARGS__); } while (0)

#define femu_log(fmt, ...) \
    do { printf("[FEMU] Log: " fmt, ## __VA_ARGS__); } while (0)


#endif /* __FEMU_NVME_H */

