#include "./nvme.h"

static uint8_t nvme_calc_rgif(uint16_t nruh, uint16_t nrg)
{
    uint16_t val;
    uint8_t ret;

    if (unlikely(nrg == 1)) {
        /* PIDRG_NORGI scenario, all of pid is used for PHID */
        return 0;
    }

    val = nrg;
    ret = 0;
    while (val) {
        val >>= 1;
        ++ret;
    }
    
    /* ensure remaining bits suffice to represent number of phids in a RG */
    if (unlikely((UINT16_MAX >> ret) < nruh)) {
        return 0;
    }
    return ret;
}

void nvme_init_endgrp(FemuCtrl *n)
{
    NvmeEnduranceGroup *endgrp = n->endgrp;
    FdpParams *fdp_params = &n->fdp_params;
    
    if(FDPSSD(n)){
        endgrp->fdp.enabled = true;
    }

    endgrp->fdp.nrg = fdp_params->nrg;
    endgrp->fdp.nruh = fdp_params->nruh;
    endgrp->fdp.rgif = nvme_calc_rgif(endgrp->fdp.nruh, endgrp->fdp.nrg);
    endgrp->fdp.runs = fdp_params->runs;
    endgrp->fdp.ruhs = g_malloc0(sizeof(NvmeRuHandle) * endgrp->fdp.nruh);

    for (uint16_t ruhid = 0; ruhid < endgrp->fdp.nruh; ruhid++) {
        endgrp->fdp.ruhs[ruhid] = (NvmeRuHandle) {
            .ruht = NVME_RUHT_INITIALLY_ISOLATED,
            .ruha = NVME_RUHA_UNUSED,
        };

        endgrp->fdp.ruhs[ruhid].rus = g_new(NvmeReclaimUnit, endgrp->fdp.nrg);
    }

    endgrp->ctrl = n;
    endgrp->namespaces = n->namespaces;
}