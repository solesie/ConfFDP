#include "../nvme.h"
#include "./ftl.h"

static void fdp_init_ctrl_str(FemuCtrl *n)
{
    static int fsid_vbb = 0;
    const char *vbbssd_mn = "FEMU FDP-SSD Controller";
    const char *vbbssd_sn = "vSSD";

    nvme_set_ctrl_name(n, vbbssd_mn, vbbssd_sn, &fsid_vbb);
}

static void fdp_init(FemuCtrl *n, Error **errp)
{
    struct fdpssd *fdpssd = n->fdpssd = g_malloc0(sizeof(struct fdpssd));

    fdp_init_ctrl_str(n);

    fdpssd->dataplane_started_ptr = &n->dataplane_started;
    fdpssd->ssdname = (char *)n->devname;
    femu_debug("Starting FEMU in FDP-SSD mode ...\n");
    fdpssd_init(n);
}

static void fdp_flip(FemuCtrl *n, NvmeCmd *cmd)
{
    struct fdpssd *fdpssd = n->fdpssd;
    int64_t cdw10 = le64_to_cpu(cmd->cdw10);

    switch (cdw10) {
    case FEMU_ENABLE_GC_DELAY:
        fdpssd->sp.enable_gc_delay = true;
        femu_err("%s,FEMU GC Delay Emulation [Enabled]!\n", n->devname);
        femu_log("%s,FEMU GC Delay Emulation [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_GC_DELAY:
        fdpssd->sp.enable_gc_delay = false;
        femu_err("%s,FEMU GC Delay Emulation [Disabled]!\n", n->devname);
        femu_log("%s,FEMU GC Delay Emulation [Disabled]!\n", n->devname);
        break;
    case FEMU_ENABLE_DELAY_EMU:
        fdpssd->sp.pg_rd_lat = NAND_READ_LATENCY;
        fdpssd->sp.pg_wr_lat = NAND_PROG_LATENCY;
        fdpssd->sp.blk_er_lat = NAND_ERASE_LATENCY;
        fdpssd->sp.ch_xfer_lat = 0;
        femu_log("%s,FEMU Delay Emulation [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_DELAY_EMU:
        fdpssd->sp.pg_rd_lat = 0;
        fdpssd->sp.pg_wr_lat = 0;
        fdpssd->sp.blk_er_lat = 0;
        fdpssd->sp.ch_xfer_lat = 0;
        femu_log("%s,FEMU Delay Emulation [Disabled]!\n", n->devname);
        break;
    case FEMU_RESET_ACCT:
        n->nr_tt_ios = 0;
        n->nr_tt_late_ios = 0;
        femu_log("%s,Reset tt_late_ios/tt_ios,%lu/%lu\n", n->devname,
                n->nr_tt_late_ios, n->nr_tt_ios);
        break;
    case FEMU_ENABLE_LOG:
        n->print_log = true;
        femu_log("%s,Log print [Enabled]!\n", n->devname);
        break;
    case FEMU_DISABLE_LOG:
        n->print_log = false;
        femu_log("%s,Log print [Disabled]!\n", n->devname);
        break;
    default:
        printf("FEMU:%s,Not implemented flip cmd (%lu)\n", n->devname, cdw10);
    }
}

static inline uint16_t nvme_pid2rg(NvmeNamespace *ns, uint16_t pid)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;
    if (!rgif) {
        return 0;
    }
    return pid >> (16 - rgif);
}
static inline uint16_t nvme_pid2ph(NvmeNamespace *ns, uint16_t pid)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;
    if (!rgif) {
        return pid;
    }
    return pid & ((1 << (15 - rgif)) - 1);
}
static inline bool nvme_rg_valid(NvmeNamespace *ns, uint16_t rg)
{
    return rg < ns->endgrp->fdp.nrg;
}
static inline bool nvme_ph_valid(NvmeNamespace *ns, uint16_t ph)
{
    return ph < ns->fdp.nphs;
}
static inline bool nvme_parse_pid(NvmeNamespace *ns, uint16_t pid,
                                  uint16_t *ph, uint16_t *rg)
{
    *rg = nvme_pid2rg(ns, pid);
    *ph = nvme_pid2ph(ns, pid);
    return nvme_ph_valid(ns, *ph) && nvme_rg_valid(ns, *rg);
}

static uint16_t fdp_nvme_write(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)cmd;
    uint8_t dtype = (rw->control >> 4) & 0xf;
    uint16_t pid = le16_to_cpu(rw->dspec);
    uint16_t ph, rg, ruh_id;

    if (dtype != NVME_DIRECTIVE_DATA_PLACEMENT ||
        !nvme_parse_pid(ns, pid, &ph, &rg)) {
        ph = 0;
        rg = 0;
    }
    ruh_id = ns->fdp.phs[ph];

    req->fdp.rg_id = rg;
    req->fdp.ruh_id = ruh_id;

    return nvme_rw(n, ns, cmd, req);
}

static uint16_t fdp_nvme_read(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req)
{
    return nvme_rw(n, ns, cmd, req);
}

static uint16_t fdp_io_cmd(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                          NvmeRequest *req)
{
    switch (cmd->opcode) {
    case NVME_CMD_READ:
        return fdp_nvme_read(n, ns, cmd, req);
    case NVME_CMD_WRITE:
        return fdp_nvme_write(n, ns, cmd, req);
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static uint16_t fdp_admin_cmd(FemuCtrl *n, NvmeCmd *cmd)
{
    switch (cmd->opcode) {
    case NVME_ADM_CMD_FEMU_FLIP:
        fdp_flip(n, cmd);
        return NVME_SUCCESS;
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

int nvme_register_fdpssd(FemuCtrl *n)
{
    n->ext_ops = (FemuExtCtrlOps) {
        .state            = NULL,
        .init             = fdp_init,
        .exit             = NULL,
        .rw_check_req     = NULL,
        .admin_cmd        = fdp_admin_cmd,
        .io_cmd           = fdp_io_cmd,
        .get_log          = NULL,
    };

    return 0;
}

