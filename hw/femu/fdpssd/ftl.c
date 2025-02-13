#include "ftl.h"

//#define FEMU_DEBUG_FTL

static void *ftl_thread(void *arg);

static inline bool should_gc(struct fdpssd *fdpssd, int rg_id)
{
    return (fdpssd->rgs[rg_id].free_superblock_cnt <= fdpssd->sp.gc_thres_superblocks);
}

static inline bool should_gc_high(struct fdpssd *fdpssd, int rg_id)
{
    return (fdpssd->rgs[rg_id].free_superblock_cnt <= fdpssd->sp.gc_thres_superblocks_high);
}

static inline struct ppa get_maptbl_ent(struct fdpssd *fdpssd, uint64_t lpn)
{
    return fdpssd->maptbl[lpn];
}

static inline void set_maptbl_ent(struct fdpssd *fdpssd, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < fdpssd->sp.tt_pgs);
    fdpssd->maptbl[lpn] = *ppa;
}

static uint64_t ppa2pgidx(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct ssdparams *spp = &fdpssd->sp;
    uint64_t pgidx;

    pgidx = ppa->g.ch  * spp->pgs_per_ch  + \
            ppa->g.lun * spp->pgs_per_lun + \
            ppa->g.pl  * spp->pgs_per_pl  + \
            ppa->g.blk * spp->pgs_per_blk + \
            ppa->g.pg;

    ftl_assert(pgidx < spp->tt_pgs);

    return pgidx;
}

static inline uint64_t get_rmap_ent(struct fdpssd *fdpssd, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(fdpssd, ppa);

    return fdpssd->rmap[pgidx];
}

/* set rmap[page_no(ppa)] -> lpn */
static inline void set_rmap_ent(struct fdpssd *fdpssd, uint64_t lpn, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(fdpssd, ppa);

    fdpssd->rmap[pgidx] = lpn;
}

static inline int victim_superblock_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static inline pqueue_pri_t victim_superblock_get_pri(void *a)
{
    return ((struct Superblock *)a)->vpc;
}

static inline void victim_superblock_set_pri(void *a, pqueue_pri_t pri)
{
    ((struct Superblock *)a)->vpc = pri;
}

static inline size_t victim_superblock_get_pos(void *a)
{
    return ((struct Superblock *)a)->pos;
}

static inline void victim_superblock_set_pos(void *a, size_t pos)
{
    ((struct Superblock *)a)->pos = pos;
}

static void fdpssd_init_reclaim_groups(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;
    int nrg = spp->fdp.nrg;
    
    fdpssd->rgs = g_malloc0(sizeof(struct ReclaimGroup) * nrg);

    ReclaimGroup *rgs = fdpssd->rgs;
    Superblock *sb;

    for(int rg_id = 0; rg_id < nrg; ++rg_id) {
        rgs[rg_id].tt_superblock = spp->blks_per_pl;
        ftl_assert(rgs[rg_id].tt_superblock == spp->tt_superblock);
        rgs[rg_id].superblocks = g_malloc0(sizeof(struct Superblock) * rgs[rg_id].tt_superblock);

        QTAILQ_INIT(&rgs[rg_id].free_superblock_list);
        rgs[rg_id].victim_superblock_pq = pqueue_init(rgs[rg_id].tt_superblock, victim_superblock_cmp_pri,
            victim_superblock_get_pri, victim_superblock_set_pri,
            victim_superblock_get_pos, victim_superblock_set_pos);
        QTAILQ_INIT(&rgs[rg_id].full_superblock_list);

        rgs[rg_id].free_superblock_cnt = 0;
        for(int i = 0; i < rgs[rg_id].tt_superblock; ++i){
            sb = &rgs[rg_id].superblocks[i];
            sb->n.sbo = SBO_UNOWNED;
            sb->n.rg = rg_id;
            sb->n.offt = i;
            sb->ipc = 0;
            sb->vpc = 0;
            sb->pos = 0;
            /* initialize all the superblocks as free superblocks */
            QTAILQ_INSERT_TAIL(&rgs[rg_id].free_superblock_list, sb, entry);
            rgs[rg_id].free_superblock_cnt++;
        }

        ftl_assert(rgs[rg_id].free_superblock_cnt == rgs[rg_id].tt_superblock);
        rgs[rg_id].victim_superblock_cnt = 0;
        rgs[rg_id].full_superblock_cnt = 0;
        rgs[rg_id].wp = g_malloc0(sizeof(struct WritePointer));
        sb = QTAILQ_FIRST(&rgs[rg_id].free_superblock_list);
        QTAILQ_REMOVE(&rgs[rg_id].free_superblock_list, sb, entry);
        sb->n.sbo = SBO_RG;
        --rgs[rg_id].free_superblock_cnt;

        rgs[rg_id].wp->cur_sb = sb;
        rgs[rg_id].wp->ch = rgs[rg_id].start_ch;
        rgs[rg_id].wp->lun = rgs[rg_id].start_way;
        rgs[rg_id].wp->pg = 0;
        rgs[rg_id].wp->blk = 0;
        rgs[rg_id].wp->pl = 0;

        /* solesie: only continuous 2D reclaim group is supported */
        rgs[rg_id].start_ch = (rg_id * spp->fdp.chs_per_rg) % spp->nchs;
        rgs[rg_id].end_ch = rgs[rg_id].start_ch + spp->fdp.chs_per_rg - 1;
        rgs[rg_id].start_way = ((rg_id * spp->fdp.chs_per_rg) / spp->nchs) * spp->fdp.ways_per_rg;
        rgs[rg_id].end_way = rgs[rg_id].start_way + spp->fdp.ways_per_rg - 1;
    }
}

static void fdpssd_init_ruh(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;
    ReclaimGroup *rgs = fdpssd->rgs;
    Superblock *cur_sb = NULL;
    WritePointer *wp = NULL;

    fdpssd->ruhs = g_malloc0(sizeof(ReclaimUnitHandle) * spp->fdp.nruh);

    for (uint16_t ruh_id = 0; ruh_id < spp->fdp.nruh; ++ruh_id) {
        /* solesie: init ruh write pointers for each rg. */
        fdpssd->ruhs[ruh_id].wps = g_malloc0(sizeof(struct WritePointer) * spp->fdp.nrg);
        for (uint16_t rg_id = 0; rg_id < spp->fdp.nrg; ++rg_id) {
            cur_sb = QTAILQ_FIRST(&rgs[rg_id].free_superblock_list);
            QTAILQ_REMOVE(&rgs[rg_id].free_superblock_list, cur_sb, entry);
            --rgs[rg_id].free_superblock_cnt;
            cur_sb->n.sbo = SBO_INITIALLY_ISOLATED_RUH;
            cur_sb->n.ruh = ruh_id;

            wp = &fdpssd->ruhs[ruh_id].wps[rg_id];
            wp->cur_sb = cur_sb;

            wp->ch = rgs[rg_id].start_ch;
            wp->lun = rgs[rg_id].start_way;
            wp->pg = 0;
            wp->blk = 0;
            wp->pl = 0;
        }
    }
}

/* solesie: This function initializes chway2rg.
   chway2rg is used to determine which RG a LUN belongs to.
   This function should be called after fdpssd_init_ch() and fdpssd_init_reclaim_group(). */
static void fdpssd_init_chway2rg(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;
    ReclaimGroup *rgs = fdpssd->rgs;

    fdpssd->chway2rg = g_malloc0(sizeof(int *) * spp->nchs);
    for(int ch = 0; ch < spp->nchs; ++ch){
        fdpssd->chway2rg[ch] = g_malloc0(sizeof(int) * spp->luns_per_ch);
        for(int way = 0; way < spp->luns_per_ch; ++way){
            /* solesie: find RG */
            for(int rg_id = 0; rg_id < spp->fdp.nrg; ++rg_id) {
                if(rgs[rg_id].start_ch <= ch && ch <= rgs[rg_id].end_ch && 
                rgs[rg_id].start_way <= way && way <= rgs[rg_id].end_way){
                    fdpssd->chway2rg[ch][way] = rg_id;
                    break;
                }
            }
        }
    }
}

static inline void check_addr(int a, int max)
{
    ftl_assert(a >= 0 && a < max);
}

static struct Superblock *get_next_free_superblock(struct fdpssd *fdpssd, int rg_id)
{
    ReclaimGroup *rg = &fdpssd->rgs[rg_id];
    Superblock *cur_sb = NULL;

    cur_sb = QTAILQ_FIRST(&rg->free_superblock_list);
    ftl_assert(cur_sb->n.sbo == SBO_UNOWNED);
    if (!cur_sb) {
        ftl_err("No free superblocks left in [%s] !!!!\n", fdpssd->ssdname);
        return NULL;
    }

    QTAILQ_REMOVE(&rg->free_superblock_list, cur_sb, entry);
    rg->free_superblock_cnt--;
    return cur_sb;
}

/**
 * @brief 
 * ssd_advance_write_pointer 
 * get write pointer from fdpssd sturcture and add +1 for each member respectively 
 * write pointer has a member which indicates each component of fdpssd and exposed by it's number
 * this function  
 * struct Superblock *cur_sb;
 * int ch;
 * int lun;
 * int pg;
 * int blk;
 * int pl;
 */
static void ssd_advance_write_pointer(struct fdpssd *fdpssd, WritePointer *wp)
{
    struct ssdparams *spp = &fdpssd->sp;
    int rg_id = wp->cur_sb->n.rg;
    ReclaimGroup *rg = &fdpssd->rgs[rg_id];
    uint8_t sbo = wp->cur_sb->n.sbo;
    uint16_t ruh = wp->cur_sb->n.ruh;

    ftl_assert(rg->start_ch <= wp->ch && wp->ch <= rg->end_ch);
    wp->ch++;      //wp->ch means channel #(int) for current write pointer
    if (wp->ch == rg->end_ch + 1) {
        wp->ch = rg->start_ch;
        ftl_assert(rg->start_way <= wp->lun && wp->lun <= rg->end_way);
        wp->lun++;
        /* in this case, we should go to next lun */
        if (wp->lun == rg->end_way + 1) {
            wp->lun = rg->start_way;
            /* go to next page in the block */
            ftl_assert(0 <= wp->pg && wp->pg < spp->pgs_per_blk);
            wp->pg++;
            if(wp->pg == spp->pgs_per_blk){
                wp->pg = 0;
                /* move current superblock to {victim,full} superblock list */
                if (wp->cur_sb->vpc == spp->pgs_per_superblock) {
                    ftl_assert(wp->cur_sb->ipc == 0);
                    QTAILQ_INSERT_TAIL(&rg->full_superblock_list, wp->cur_sb, entry);
                    rg->full_superblock_cnt++;
                } else {
                    ftl_assert(wp->cur_sb->vpc >= 0 && wp->cur_sb->vpc < spp->pgs_per_superblock);
                    ftl_assert(wp->cur_sb->ipc > 0);
                    pqueue_insert(rg->victim_superblock_pq, wp->cur_sb);
                    rg->victim_superblock_cnt++;
                }
                /* current superblock is used up, pick another empty superblock */
                ftl_assert(0 <= wp->blk && wp->blk < spp->blks_per_pl);
                wp->cur_sb = NULL;
                wp->cur_sb = get_next_free_superblock(fdpssd, rg_id);
                if (!wp->cur_sb) {
                    /* TODO */
                    abort();
                }
                wp->cur_sb->n.sbo = sbo;
                wp->cur_sb->n.ruh = ruh;

                wp->blk = wp->cur_sb->n.offt;
                /* make sure we are starting from page 0 in the super block */
                ftl_assert(wp->pg == 0);
                ftl_assert(wp->lun == rg->start_way);
                ftl_assert(wp->ch == rg->start_ch);
                /* TODO: assume # of pl_per_lun is 1, fix later */
                ftl_assert(wp->pl == 0);
            }
        }
    }
}

static struct ppa get_new_page(WritePointer *wp)
{
    struct ppa ppa;
    ppa.ppa = 0;
    ppa.g.ch = wp->ch;
    ppa.g.lun = wp->lun;
    ppa.g.pg = wp->pg;
    ppa.g.blk = wp->blk;
    ppa.g.pl = wp->pl;
    ftl_assert(ppa.g.pl == 0);

    return ppa;
}

static void check_params(struct ssdparams *spp, uint64_t dev_sz)
{
    /*
     * we are using a general write pointer increment method now, no need to
     * force luns_per_ch and nchs to be power of 2
     */
    // ftl_assert(is_power_of_2(spp->luns_per_ch));
    //ftl_assert(is_power_of_2(spp->nchs));
}

static void fdpssd_init_params(struct ssdparams *spp, FemuCtrl *n)
{
    ConfParams *conf_params = &n->conf_params;
    FdpParams *fdp_params = &n->fdp_params;
    uint64_t dev_sz = n->memsz * MiB;

    spp->secsz = conf_params->secsz;
    spp->secs_per_pg = conf_params->secs_per_pg;
    spp->pgs_per_blk = conf_params->pgs_per_blk;
    spp->pls_per_lun = conf_params->pls_per_lun;
    spp->luns_per_ch = conf_params->luns_per_ch;
    spp->nchs = conf_params->nchs;

    spp->pg_rd_lat = conf_params->pg_rd_lat;
    spp->pg_wr_lat = conf_params->pg_wr_lat;
    spp->blk_er_lat = conf_params->blk_er_lat;
    spp->ch_xfer_lat = conf_params->ch_xfer_lat;

    /* calculated values */
    spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch;
    spp->tt_pls = spp->pls_per_ch * spp->nchs;

    uint64_t blk_sz = spp->secsz * spp->secs_per_pg * spp->pgs_per_blk;
    uint64_t pl_sz = dev_sz / spp->nchs / spp->pls_per_ch;
    spp->blks_per_pl = pl_sz / blk_sz;

    spp->secs_per_blk = spp->secs_per_pg * spp->pgs_per_blk;
    spp->secs_per_pl = spp->secs_per_blk * spp->blks_per_pl;
    spp->secs_per_lun = spp->secs_per_pl * spp->pls_per_lun;
    spp->secs_per_ch = spp->secs_per_lun * spp->luns_per_ch;
    spp->tt_secs = spp->secs_per_ch * spp->nchs;

    spp->pgs_per_pl = spp->pgs_per_blk * spp->blks_per_pl;
    spp->pgs_per_lun = spp->pgs_per_pl * spp->pls_per_lun;
    spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
    spp->tt_pgs = spp->pgs_per_ch * spp->nchs;

    spp->blks_per_lun = spp->blks_per_pl * spp->pls_per_lun;
    spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch;
    spp->tt_blks = spp->blks_per_ch * spp->nchs;

    spp->tt_luns = spp->luns_per_ch * spp->nchs;

    spp->blks_per_superblock = spp->tt_luns; /* TODO: to fix under multiplanes */
    spp->pgs_per_superblock = spp->blks_per_superblock * spp->pgs_per_blk;
    spp->secs_per_superblock = spp->pgs_per_superblock * spp->secs_per_pg;
    spp->tt_superblock = spp->blks_per_lun; /* TODO: to fix under multiplanes */

    /* fdp */
    spp->fdp.nrg = fdp_params->nrg;
    spp->fdp.nruh = fdp_params->nruh;
    spp->fdp.chs_per_rg = fdp_params->chs_per_rg;
    spp->fdp.ways_per_rg = fdp_params->ways_per_rg;

    spp->gc_thres_pcent = conf_params->gc_thres_pcent / 100.0;
    spp->gc_thres_superblocks = (int)((1 - spp->gc_thres_pcent) * (spp->tt_superblock / spp->fdp.nrg));
    spp->gc_thres_pcent_high = conf_params->gc_thres_pcent_high / 100.0;
    spp->gc_thres_superblocks_high = (int)((1 - spp->gc_thres_pcent_high) * (spp->tt_superblock / spp->fdp.nrg));
    spp->enable_gc_delay = true;

    check_params(spp, dev_sz);
}

static void ssd_init_nand_page(struct nand_page *pg, struct ssdparams *spp)
{
    pg->nsecs = spp->secs_per_pg;
    pg->sec = g_malloc0(sizeof(nand_sec_status_t) * pg->nsecs);
    for (int i = 0; i < pg->nsecs; i++) {
        pg->sec[i] = SEC_FREE;
    }
    pg->status = PG_FREE;
}

static void ssd_init_nand_blk(struct nand_block *blk, struct ssdparams *spp)
{
    blk->npgs = spp->pgs_per_blk;
    blk->pg = g_malloc0(sizeof(struct nand_page) * blk->npgs);
    for (int i = 0; i < blk->npgs; i++) {
        ssd_init_nand_page(&blk->pg[i], spp);
    }
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt = 0;
    blk->wp = 0;
}

static void ssd_init_nand_plane(struct nand_plane *pl, struct ssdparams *spp)
{
    pl->nblks = spp->blks_per_pl;
    pl->blk = g_malloc0(sizeof(struct nand_block) * pl->nblks);
    for (int i = 0; i < pl->nblks; i++) {
        ssd_init_nand_blk(&pl->blk[i], spp);
    }
}

static void ssd_init_nand_lun(struct nand_lun *lun, struct ssdparams *spp)
{
    lun->npls = spp->pls_per_lun;
    lun->pl = g_malloc0(sizeof(struct nand_plane) * lun->npls);
    for (int i = 0; i < lun->npls; i++) {
        ssd_init_nand_plane(&lun->pl[i], spp);
    }
    lun->next_lun_avail_time = 0;
    lun->busy = false;
}

static void fdpssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp)
{
    ch->nluns = spp->luns_per_ch;
    ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    for (int i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}

static void fdpssd_init_maptbl(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;

    fdpssd->maptbl = g_malloc0(sizeof(struct ppa) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        fdpssd->maptbl[i].ppa = UNMAPPED_PPA;
    }
}

static void fdpssd_init_rmap(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;

    fdpssd->rmap = g_malloc0(sizeof(uint64_t) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        fdpssd->rmap[i] = INVALID_LPN;
    }
}

void fdpssd_init(FemuCtrl *n)
{
    struct fdpssd *fdpssd = n->fdpssd;
    struct ssdparams *spp = &fdpssd->sp;

    ftl_assert(fdpssd);

    fdpssd_init_params(spp, n);

    /* initialize fdpssd internal layout architecture */
    fdpssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);
    for (int i = 0; i < spp->nchs; i++) {
        fdpssd_init_ch(&fdpssd->ch[i], spp);
    }

    /* initialize maptbl */
    fdpssd_init_maptbl(fdpssd);

    /* initialize rmap */
    fdpssd_init_rmap(fdpssd);

    /* initialize all the rg and sb */
    fdpssd_init_reclaim_groups(fdpssd);

    /* initialize ruh and write pointer, this is how we allocate new pages for writes */
    fdpssd_init_ruh(fdpssd);

    fdpssd_init_chway2rg(fdpssd);

    qemu_thread_create(&fdpssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n,
                       QEMU_THREAD_JOINABLE);
}

static inline bool valid_ppa(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct ssdparams *spp = &fdpssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sec = ppa->g.sec;

    if (ch >= 0 && ch < spp->nchs && lun >= 0 && lun < spp->luns_per_ch && pl >=
        0 && pl < spp->pls_per_lun && blk >= 0 && blk < spp->blks_per_pl && pg
        >= 0 && pg < spp->pgs_per_blk && sec >= 0 && sec < spp->secs_per_pg)
        return true;

    return false;
}

static inline bool valid_lpn(struct fdpssd *fdpssd, uint64_t lpn)
{
    return (lpn < fdpssd->sp.tt_pgs);
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}

static inline struct ssd_channel *get_ch(struct fdpssd *fdpssd, struct ppa *ppa)
{
    return &(fdpssd->ch[ppa->g.ch]);
}
/**
 * @brief 
 * get logical unit in fdpssd according to physical-page address
 *  (page<block<plane<die<chip, and # of chips per 1 channel)
 * 
 * returns  struct nand_lun * which is channel -> lun[ppa->g.lun] 
 * arg      struct fdpssd * ,  struct ppa *
 * 
 * inhoinno!
 */
static inline struct nand_lun *get_lun(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(fdpssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}
/**
 * @brief 
 * get plane int fdpssd according to physical-page address
 * (page<block<plane<die<chip, and # of chips per 1 channel)
 * 
 * returns  struct nand_plane * which is logical unit -> pl[ppa->g.lun] 
 * args     struct fdpssd *, struct ppa *
 * 
 * inhoinno!
 */
static inline struct nand_plane *get_pl(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(fdpssd, ppa);
    return &(lun->pl[ppa->g.pl]);
}

static inline struct nand_block *get_blk(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct nand_plane *pl = get_pl(fdpssd, ppa);
    return &(pl->blk[ppa->g.blk]);
}

static inline int get_reclaim_group_id(struct fdpssd *fdpssd, struct ppa *ppa)
{
    return fdpssd->chway2rg[ppa->g.ch][ppa->g.lun];
}

static inline struct Superblock *get_superblock(struct fdpssd *fdpssd, struct ppa *ppa)
{
    int rg = get_reclaim_group_id(fdpssd, ppa);
    return &(fdpssd->rgs[rg].superblocks[ppa->g.blk]);
}

static inline struct nand_page *get_pg(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct nand_block *blk = get_blk(fdpssd, ppa);
    return &(blk->pg[ppa->g.pg]);
}

static uint64_t ssd_advance_status(struct fdpssd *fdpssd, struct ppa *ppa, struct
        nand_cmd *ncmd)
{
    int c = ncmd->cmd;
    uint64_t cmd_stime = (ncmd->stime == 0) ? \
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;
    uint64_t nand_stime;
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_lun *lun = get_lun(fdpssd, ppa);
#if ADVANCE_PER_CH_ENDTIME
    struct ssd_channel *ch = get_ch(fdpssd, ppa);
    uint64_t chnl_stime=0;
#endif
    uint64_t lat = 0;

    switch (c) {
    case NAND_READ:
        /* read: perform NAND cmd first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
        lat = lun->next_lun_avail_time - cmd_stime;
#if ADVANCE_PER_CH_ENDTIME
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;

        /* read: then data transfer through channel */
        chnl_stime = (ch->next_ch_avail_time < lun->next_lun_avail_time) ? \
            lun->next_lun_avail_time : ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        lat = ch->next_ch_avail_time - cmd_stime;
#endif
        break;

    case NAND_WRITE:
        /* write: transfer data through channel first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        if (ncmd->type == USER_IO) {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        } else {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        }
        lat = lun->next_lun_avail_time - cmd_stime;

#if ADVANCE_PER_CH_ENDTIME
        chnl_stime = (ch->next_ch_avail_time < cmd_stime) ? cmd_stime : \
                     ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        /* write: then do NAND program */
        nand_stime = (lun->next_lun_avail_time < ch->next_ch_avail_time) ? \
            ch->next_ch_avail_time : lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
#endif
        break;

    case NAND_ERASE:
        /* erase: only need to advance NAND status */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->blk_er_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
        break;

    default:
        ftl_err("Unsupported NAND command: 0x%x\n", c);
    }

    return lat;
}

/* update SSD status about one page from PG_VALID -> PG_VALID */
static void mark_page_invalid(struct fdpssd *fdpssd, struct ppa *ppa)
{
    int rg_id = get_reclaim_group_id(fdpssd, ppa);
    ReclaimGroup *rg = &fdpssd->rgs[rg_id];
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    bool was_full_sb = false;
    Superblock *sb;

    /* update corresponding page status */
    pg = get_pg(fdpssd, ppa);
    ftl_assert(pg->status == PG_VALID);
    pg->status = PG_INVALID;

    /* update corresponding block status */
    blk = get_blk(fdpssd, ppa);
    ftl_assert(blk->ipc >= 0 && blk->ipc < spp->pgs_per_blk);
    blk->ipc++;
    ftl_assert(blk->vpc > 0 && blk->vpc <= spp->pgs_per_blk);
    blk->vpc--;

    /* update corresponding superblock status */
    sb = get_superblock(fdpssd, ppa);
    ftl_assert(sb->ipc >= 0 && sb->ipc < spp->pgs_per_superblock);
    if (sb->vpc == spp->pgs_per_superblock) {
        ftl_assert(sb->ipc == 0);
        was_full_sb = true;
    }
    sb->ipc++;
    ftl_assert(sb->vpc > 0 && sb->vpc <= spp->pgs_per_superblock);
    /* Adjust the position of the victime superblock in the pq under over-writes */
    if (sb->pos) {
        /* Note that sb->vpc will be updated by this call */
        pqueue_change_priority(rg->victim_superblock_pq, sb->vpc - 1, sb);
    } else {
        sb->vpc--;
    }

    if (was_full_sb) {
        /* move superblock: "full" -> "victim" */
        QTAILQ_REMOVE(&rg->full_superblock_list, sb, entry);
        rg->full_superblock_cnt--;
        pqueue_insert(rg->victim_superblock_pq, sb);
        rg->victim_superblock_cnt++;
    }
}

static void mark_page_valid(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    struct Superblock *sb;

    /* update page status */
    pg = get_pg(fdpssd, ppa);
    ftl_assert(pg->status == PG_FREE);
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(fdpssd, ppa);
    ftl_assert(blk->vpc >= 0 && blk->vpc < fdpssd->sp.pgs_per_blk);
    blk->vpc++;

    /* update corresponding superblock status */
    sb = get_superblock(fdpssd, ppa);
    ftl_assert(sb->vpc >= 0 && sb->vpc < fdpssd->sp.pgs_per_superblock);
    sb->vpc++;
}

static void mark_block_free(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_block *blk = get_blk(fdpssd, ppa);
    struct nand_page *pg = NULL;

    for (int i = 0; i < spp->pgs_per_blk; i++) {
        /* reset page status */
        pg = &blk->pg[i];
        ftl_assert(pg->nsecs == spp->secs_per_pg);
        pg->status = PG_FREE;
    }

    /* reset block status */
    ftl_assert(blk->npgs == spp->pgs_per_blk);
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt++;
}

static void gc_read_page(struct fdpssd *fdpssd, struct ppa *ppa)
{
    /* advance fdpssd status, we don't care about how long it takes */
    if (fdpssd->sp.enable_gc_delay) {
        struct nand_cmd gcr;
        gcr.type = GC_IO;
        gcr.cmd = NAND_READ;
        gcr.stime = 0;
        ssd_advance_status(fdpssd, ppa, &gcr);
    }
}

/* move valid page data (already in DRAM) from victim superblock to a new page */
static uint64_t gc_write_page(struct fdpssd *fdpssd, struct ppa *old_ppa)
{
    struct ppa new_ppa;
    struct nand_lun *new_lun;
    uint64_t lpn = get_rmap_ent(fdpssd, old_ppa);
    struct Superblock *sb = get_superblock(fdpssd, old_ppa);
    WritePointer *wp = NULL;
    /* solesie: It is necessary to discuss 
       whether Initially Isolated should be treated like Persistently Isolated 
       under certain conditions. */
    if(sb->n.sbo == SBO_PERSISTENTLY_ISOLATED_RUH){
        wp = &fdpssd->ruhs[sb->n.ruh].wps[sb->n.rg];
    }
    else if(sb->n.sbo == SBO_INITIALLY_ISOLATED_RUH || sb->n.sbo == SBO_RG){
        wp = fdpssd->rgs[sb->n.rg].wp;
    }
    else{
        abort();
    }
    

    ftl_assert(valid_lpn(fdpssd, lpn));
    new_ppa = get_new_page(wp);
    /* update maptbl */
    set_maptbl_ent(fdpssd, lpn, &new_ppa);
    /* update rmap */
    set_rmap_ent(fdpssd, lpn, &new_ppa);

    mark_page_valid(fdpssd, &new_ppa);

    /* need to advance the write pointer here */
    ssd_advance_write_pointer(fdpssd, wp);

    if (fdpssd->sp.enable_gc_delay) {
        struct nand_cmd gcw;
        gcw.type = GC_IO;
        gcw.cmd = NAND_WRITE;
        gcw.stime = 0;
        ssd_advance_status(fdpssd, &new_ppa, &gcw);
    }

    /* advance per-ch gc_endtime as well */
#if 0
    new_ch = get_ch(fdpssd, &new_ppa);
    new_ch->gc_endtime = new_ch->next_ch_avail_time;
#endif

    new_lun = get_lun(fdpssd, &new_ppa);
    new_lun->gc_endtime = new_lun->next_lun_avail_time;

    return 0;
}

static struct Superblock *select_victim_superblock(struct fdpssd *fdpssd, int rg_id, bool force)
{
    ReclaimGroup *rg = &fdpssd->rgs[rg_id];
    Superblock *victim_sb = NULL;

    victim_sb = pqueue_peek(rg->victim_superblock_pq);
    if (!victim_sb) {
        return NULL;
    }

    if (!force && victim_sb->ipc < fdpssd->sp.pgs_per_superblock / 8) {
        return NULL;
    }

    pqueue_pop(rg->victim_superblock_pq);
    victim_sb->pos = 0;
    rg->victim_superblock_cnt--;

    /* victim_sb is a danggling node now */
    return victim_sb;
}

/* here ppa identifies the block we want to clean */
static void clean_one_block(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_page *pg_iter = NULL;
    int cnt = 0;

    for (int pg = 0; pg < spp->pgs_per_blk; pg++) {
        ppa->g.pg = pg;
        pg_iter = get_pg(fdpssd, ppa);
        /* there shouldn't be any free page in victim blocks */
        ftl_assert(pg_iter->status != PG_FREE);
        if (pg_iter->status == PG_VALID) {
            gc_read_page(fdpssd, ppa);
            /* delay the maptbl update until "write" happens */
            gc_write_page(fdpssd, ppa);
            cnt++;
        }
    }

    ftl_assert(get_blk(fdpssd, ppa)->vpc == cnt);
}

static void mark_superblock_free(struct fdpssd *fdpssd, struct ppa *ppa)
{
    int rg_id = get_reclaim_group_id(fdpssd, ppa);
    ReclaimGroup *rg = &fdpssd->rgs[rg_id];
    Superblock *sb = get_superblock(fdpssd, ppa);
    sb->n.sbo = SBO_UNOWNED;
    sb->ipc = 0;
    sb->vpc = 0;
    /* move this superblock to free superblock list */
    QTAILQ_INSERT_TAIL(&rg->free_superblock_list, sb, entry);
    rg->free_superblock_cnt++;
}

static int do_gc(struct fdpssd *fdpssd, int rg_id, bool force)
{
    Superblock *victim_sb = NULL;
    ReclaimGroup *rg = &fdpssd->rgs[rg_id];
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_lun *lunp;
    struct ppa ppa;
    int ch, lun;

    victim_sb = select_victim_superblock(fdpssd, rg_id, force);
    if (!victim_sb) {
        femu_err("FTL.c : 770 but GC doesn't happend to Inhoinno\n");
        return -1;
    }

    ppa.g.blk = victim_sb->n.offt;
    ftl_debug("GC-ing superblock:%d,ipc=%d,victim=%d,full=%d,free=%d\n", ppa.g.blk,
              victim_sb->ipc, rg->victim_superblock_cnt, rg->full_superblock_cnt,
              rg->free_superblock_cnt);

    /* copy back valid data */
    for (ch = rg->start_ch; ch < rg->end_ch; ch++) {
        for (lun = rg->start_way; lun < rg->end_way; lun++) {
            ppa.g.ch = ch;
            ppa.g.lun = lun;
            ppa.g.pl = 0;
            lunp = get_lun(fdpssd, &ppa);
            clean_one_block(fdpssd, &ppa);
            mark_block_free(fdpssd, &ppa);

            if (spp->enable_gc_delay) {
                femu_err("FTL.c : 790 GC happend to Inhoinno\n");
                struct nand_cmd gce;
                gce.type = GC_IO;
                gce.cmd = NAND_ERASE;
                gce.stime = 0;
                ssd_advance_status(fdpssd, &ppa, &gce);
            }

            lunp->gc_endtime = lunp->next_lun_avail_time;
        }
    }

    mark_superblock_free(fdpssd, &ppa);

    return 0;
}

static uint64_t fdpssd_read(struct fdpssd *fdpssd, NvmeRequest *req)
{
    struct ssdparams *spp = &fdpssd->sp;
    uint64_t lba = req->slba;
    int nsecs = req->nlb;
    struct ppa ppa;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    uint64_t end_lpn = (lba + nsecs - 1) / spp->secs_per_pg;
    uint64_t lpn;
    uint64_t sublat, maxlat = 0;

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, fdpssd->sp.tt_pgs);
    }

    /* normal IO read path */
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        ppa = get_maptbl_ent(fdpssd, lpn);
        if (!mapped_ppa(&ppa) || !valid_ppa(fdpssd, &ppa)) {
            //printf("%s,lpn(%" PRId64 ") not mapped to valid ppa\n", fdpssd->ssdname, lpn);
            //printf("Invalid ppa,ch:%d,lun:%d,blk:%d,pl:%d,pg:%d,sec:%d\n",
            //ppa.g.ch, ppa.g.lun, ppa.g.blk, ppa.g.pl, ppa.g.pg, ppa.g.sec);
            continue;
        }

        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;
        sublat = ssd_advance_status(fdpssd, &ppa, &srd);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }

    return maxlat;
}

/**
 * @brief 
 * fdpssd_write  
 * static uint64_t fdpssd_write(sturct fdpssd * fdpssd, NvmeRequest *req)
 * returns maxlat which means max latency.
 * max latency is the hightest value of latency 
 * which is calculated by ssd_advance_status per lpn(logical page number)
 * and this value is not sum of total latency but just highest latency while fdpssd_write()
 *  
 */
static uint64_t fdpssd_write(struct fdpssd *fdpssd, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    struct ssdparams *spp = &fdpssd->sp;
    int len = req->nlb;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    uint64_t end_lpn = (lba + len - 1) / spp->secs_per_pg;
    struct ppa ppa;
    uint64_t lpn;
    uint64_t curlat = 0, maxlat = 0;
    int r;

    uint16_t rg_id = req->fdp.rg_id;
    uint16_t ruh_id = req->fdp.ruh_id;
    WritePointer *wp = &fdpssd->ruhs[ruh_id].wps[rg_id];

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, fdpssd->sp.tt_pgs);
    }

    while (should_gc_high(fdpssd, rg_id)) {
        /* perform GC here until !should_gc(fdpssd) */
        femu_err("In FTL.c :870 in fdpssd_write, GC triggered, to inhoinno");
        r = do_gc(fdpssd, rg_id, true);
        if (r == -1)
            break;
    }

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        ppa = get_maptbl_ent(fdpssd, lpn);
        if (mapped_ppa(&ppa)) {
            /* update old page information first */
            mark_page_invalid(fdpssd, &ppa);
            set_rmap_ent(fdpssd, INVALID_LPN, &ppa);
        }

        /* new write */
        ppa = get_new_page(wp);
        /* update maptbl */
        set_maptbl_ent(fdpssd, lpn, &ppa);
        /* update rmap */
        set_rmap_ent(fdpssd, lpn, &ppa);

        mark_page_valid(fdpssd, &ppa);

        /* need to advance the write pointer here */
        ssd_advance_write_pointer(fdpssd, wp);

        struct nand_cmd swr;
        swr.type = USER_IO;
        swr.cmd = NAND_WRITE;
        swr.stime = req->stime;
        /* get latency statistics */
        curlat = ssd_advance_status(fdpssd, &ppa, &swr);
        maxlat = (curlat > maxlat) ? curlat : maxlat;
    }

    return maxlat;
}

static void *ftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct fdpssd *fdpssd = n->fdpssd;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;
    int i;
    /*FIXME : inhoinno: set "always GC"*/

    while (!*(fdpssd->dataplane_started_ptr)) {
        usleep(100000);
    }

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    fdpssd->to_ftl = n->to_ftl;
    fdpssd->to_poller = n->to_poller;

    while (1) {
        for (i = 1; i <= n->num_poller; i++) {
            if (!fdpssd->to_ftl[i] || !femu_ring_count(fdpssd->to_ftl[i]))
                continue;

            rc = femu_ring_dequeue(fdpssd->to_ftl[i], (void *)&req, 1);
            if (rc != 1) {
                printf("FEMU: FTL to_ftl dequeue failed\n");
            }
            fdpssd->sp.enable_gc_delay=true;
            ftl_assert(req);
            if(!fdpssd->sp.enable_gc_delay)
                femu_err("In FTL.c :937 fdpssd->sp.enable_gc_delay=false, to inhoinno");
            switch (req->cmd.opcode) {
            case NVME_CMD_WRITE:
                lat = fdpssd_write(fdpssd, req);
                //lat += 100000000; //IT WORKS AT CQ 1mil ms=100 000sec us=100sec ns=0.1sec
                break;
            case NVME_CMD_READ:
                lat = fdpssd_read(fdpssd, req);
                //lat += 100000000; //IT WORKS AT CQ 1mil ms=100 000sec us=100sec ns=0.1sec

                break;
            case NVME_CMD_DSM:
                lat = 0;
                break;
            default:
                //ftl_err("FTL received unkown request type, ERROR\n");
                ;
            }

            req->reqlat = lat;
            req->expire_time += lat;

            rc = femu_ring_enqueue(fdpssd->to_poller[i], (void *)&req, 1);
            if (rc != 1) {
                ftl_err("FTL to_poller enqueue failed\n");
            }

            for(int rg_id = 0; rg_id < fdpssd->sp.fdp.nrg; ++rg_id){
                if (should_gc(fdpssd, rg_id)) {
                    femu_err("In ftl.c:965 gc processed, to Inhoinno\n");
                    do_gc(fdpssd, rg_id, false);
                }
            }
        }
    }

    return NULL;
}

