#include "ftl.h"

//#define FEMU_DEBUG_FTL

static void *ftl_thread(void *arg);

static inline bool should_gc(struct fdpssd *fdpssd)
{
    return (fdpssd->lm.free_line_cnt <= fdpssd->sp.gc_thres_lines);
}

static inline bool should_gc_high(struct fdpssd *fdpssd)
{
    return (fdpssd->lm.free_line_cnt <= fdpssd->sp.gc_thres_lines_high);
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

static inline int victim_line_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static inline pqueue_pri_t victim_line_get_pri(void *a)
{
    return ((struct line *)a)->vpc;
}

static inline void victim_line_set_pri(void *a, pqueue_pri_t pri)
{
    ((struct line *)a)->vpc = pri;
}

static inline size_t victim_line_get_pos(void *a)
{
    return ((struct line *)a)->pos;
}

static inline void victim_line_set_pos(void *a, size_t pos)
{
    ((struct line *)a)->pos = pos;
}

static void ssd_init_lines(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;
    struct line_mgmt *lm = &fdpssd->lm;
    struct line *line;

    lm->tt_lines = spp->blks_per_pl;
    ftl_assert(lm->tt_lines == spp->tt_line);
    lm->lines = g_malloc0(sizeof(struct line) * lm->tt_lines);

    QTAILQ_INIT(&lm->free_line_list);
    lm->victim_line_pq = pqueue_init(spp->tt_line, victim_line_cmp_pri,
            victim_line_get_pri, victim_line_set_pri,
            victim_line_get_pos, victim_line_set_pos);
    QTAILQ_INIT(&lm->full_line_list);

    lm->free_line_cnt = 0;
    for (int i = 0; i < lm->tt_lines; i++) {
        line = &lm->lines[i];
        line->id = i;
        line->ipc = 0;
        line->vpc = 0;
        line->pos = 0;
        /* initialize all the lines as free lines */
        QTAILQ_INSERT_TAIL(&lm->free_line_list, line, entry);
        lm->free_line_cnt++;
    }

    ftl_assert(lm->free_line_cnt == lm->tt_lines);
    lm->victim_line_cnt = 0;
    lm->full_line_cnt = 0;
}

static void ssd_init_write_pointer(struct fdpssd *fdpssd)
{
    struct write_pointer *wpp = &fdpssd->wp;
    struct line_mgmt *lm = &fdpssd->lm;
    struct line *curline = NULL;

    curline = QTAILQ_FIRST(&lm->free_line_list);
    QTAILQ_REMOVE(&lm->free_line_list, curline, entry);
    lm->free_line_cnt--;

    /* wpp->curline is always our next-to-write super-block */
    wpp->curline = curline;
    wpp->ch = 0;
    wpp->lun = 0;
    wpp->pg = 0;
    wpp->blk = 0;
    wpp->pl = 0;
}

static inline void check_addr(int a, int max)
{
    ftl_assert(a >= 0 && a < max);
}

static struct line *get_next_free_line(struct fdpssd *fdpssd)
{
    struct line_mgmt *lm = &fdpssd->lm;
    struct line *curline = NULL;

    curline = QTAILQ_FIRST(&lm->free_line_list);
    if (!curline) {
        ftl_err("No free lines left in [%s] !!!!\n", fdpssd->ssdname);
        return NULL;
    }

    QTAILQ_REMOVE(&lm->free_line_list, curline, entry);
    lm->free_line_cnt--;
    return curline;
}
/**
 * @brief 
 * ssd_advance_write_pointer 
 * get write pointer from fdpssd sturcture and add +1 for each member respectively 
 * write pointer has a member which indicates each component of fdpssd and exposed by it's number
 * this function  
 * struct line *curline;
 * int ch;
 * int lun;
 * int pg;
 * int blk;
 * int pl;
 * 
 */

static void ssd_advance_write_pointer(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;
    struct write_pointer *wpp = &fdpssd->wp;
    struct line_mgmt *lm = &fdpssd->lm;

    check_addr(wpp->ch, spp->nchs);
    wpp->ch++;      //wpp->ch means channel #(int) for current write pointer
    if (wpp->ch == spp->nchs) { 
        //spp->nchs means max # of channels in fdpssd
        //so if wpp->ch += 1 equals fdpssd's max channel 
        // then fdpssd is currently saturated
        // so simply set wp's ch to 0, which means using channels in circular manner 
        wpp->ch = 0;    
        check_addr(wpp->lun, spp->luns_per_ch);
        wpp->lun++;
        /* in this case, we should go to next lun */
        if (wpp->lun == spp->luns_per_ch) {
            wpp->lun = 0;
            /* go to next page in the block */
            check_addr(wpp->pg, spp->pgs_per_blk);
            wpp->pg++;
            if (wpp->pg == spp->pgs_per_blk) {
                wpp->pg = 0;
                /* move current line to {victim,full} line list */
                if (wpp->curline->vpc == spp->pgs_per_line) {
                    /* all pgs are still valid, move to full line list */
                    ftl_assert(wpp->curline->ipc == 0);
                    QTAILQ_INSERT_TAIL(&lm->full_line_list, wpp->curline, entry);
                    lm->full_line_cnt++;
                } else {
                    ftl_assert(wpp->curline->vpc >= 0 && wpp->curline->vpc < spp->pgs_per_line);
                    /* there must be some invalid pages in this line */
                    ftl_assert(wpp->curline->ipc > 0);
                    pqueue_insert(lm->victim_line_pq, wpp->curline);
                    lm->victim_line_cnt++;
                }
                /* current line is used up, pick another empty line */
                check_addr(wpp->blk, spp->blks_per_pl);
                wpp->curline = NULL;
                wpp->curline = get_next_free_line(fdpssd);
                if (!wpp->curline) {
                    /* TODO */
                    abort();
                }
                wpp->blk = wpp->curline->id;
                check_addr(wpp->blk, spp->blks_per_pl);
                /* make sure we are starting from page 0 in the super block */
                ftl_assert(wpp->pg == 0);
                ftl_assert(wpp->lun == 0);
                ftl_assert(wpp->ch == 0);
                /* TODO: assume # of pl_per_lun is 1, fix later */
                ftl_assert(wpp->pl == 0);
            }
        }
    }
}

static struct ppa get_new_page(struct fdpssd *fdpssd)
{
    struct write_pointer *wpp = &fdpssd->wp;
    struct ppa ppa;
    ppa.ppa = 0;
    ppa.g.ch = wpp->ch;
    ppa.g.lun = wpp->lun;
    ppa.g.pg = wpp->pg;
    ppa.g.blk = wpp->blk;
    ppa.g.pl = wpp->pl;
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

    // solesie: A RU(line) should include at least one blk per chip.
    assert(spp->blks_per_line >= spp->tt_luns / spp->fdp.nrg);

    assert(spp->fdp.runs * spp->tt_line == dev_sz);
}

static void nvme_calc_rgif(int nruh, int nrg, int *rgif)
{
    int val;
    unsigned int i;

    if (unlikely(nrg == 1)) {
        /* PIDRG_NORGI scenario, all of pid is used for PHID */
        *rgif = 0;
        return;
    }

    val = nrg;
    i = 0;
    while (val) {
        val >>= 1;
        i++;
    }
    *rgif = i;
    
    /* ensure remaining bits suffice to represent number of phids in a RG */
    if (unlikely((UINT16_MAX >> i) < nruh)) {
        *rgif = 0;
        return;
    }
    return;
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

    /* fdp */
    spp->fdp.runs = fdp_params->runs;
    spp->fdp.nrg = fdp_params->nrg;
    spp->fdp.nruh = fdp_params->nruh;
    spp->fdp.chs_per_rg = fdp_params->chs_per_rg;
    spp->fdp.ways_per_rg = fdp_params->ways_per_rg;
    nvme_calc_rgif(spp->fdp.nruh, spp->fdp.nrg, &spp->fdp.rgif);

    /* line is special, put it at the end */
    /* See line as RU */
    spp->blks_per_line = (int) (spp->fdp.runs / blk_sz);
    spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
    spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
    spp->tt_line = spp->blks_per_lun * spp->tt_luns / spp->blks_per_line;

    femu_log("%d %d %d %d\n", spp->blks_per_line, spp->pgs_per_line, spp->secs_per_line, spp->tt_line);
    femu_log("%d %d %d %d\n", spp->tt_luns, spp->blks_per_line * spp->pgs_per_blk, spp->pgs_per_line * spp->secs_per_pg, spp->blks_per_lun);

    spp->gc_thres_pcent = spp->gc_thres_pcent / 100.0;
    spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * (spp->tt_line / spp->fdp.nrg));
    spp->gc_thres_pcent_high = 0.95;
    spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * (spp->tt_line / spp->fdp.nrg));
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

static void ssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp)
{
    ch->nluns = spp->luns_per_ch;
    ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    for (int i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}

static void ssd_init_maptbl(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;

    fdpssd->maptbl = g_malloc0(sizeof(struct ppa) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        fdpssd->maptbl[i].ppa = UNMAPPED_PPA;
    }
}

static void ssd_init_rmap(struct fdpssd *fdpssd)
{
    struct ssdparams *spp = &fdpssd->sp;

    fdpssd->rmap = g_malloc0(sizeof(uint64_t) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        fdpssd->rmap[i] = INVALID_LPN;
    }
}

static void fdpssd_init_fdp(struct fdpssd *fdpssd)
{
    fdpssd->ruhs = g_new0(NvmeRuHandle, fdpssd->sp.fdp.nruh);

    for (uint16_t ruhid = 0; ruhid < fdpssd->sp.fdp.nruh; ruhid++) {
        fdpssd->ruhs[ruhid] = (NvmeRuHandle) {
            .ruht = NVME_RUHT_INITIALLY_ISOLATED,
            .ruha = NVME_RUHA_UNUSED,
        };

        fdpssd->ruhs[ruhid].rus = g_new0(NvmeReclaimUnit, fdpssd->sp.fdp.nrg);
    }
}

void fdpssd_init(FemuCtrl *n)
{
    struct fdpssd *fdpssd = n->fdpssd;
    struct ssdparams *spp = &fdpssd->sp;

    ftl_assert(fdpssd);

    fdpssd_init_params(spp, n);
    fdpssd_init_fdp(fdpssd);

    /* initialize fdpssd internal layout architecture */
    fdpssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);
    for (int i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&fdpssd->ch[i], spp);
    }

    /* initialize maptbl */
    ssd_init_maptbl(fdpssd);

    /* initialize rmap */
    ssd_init_rmap(fdpssd);

    /* initialize all the lines */
    ssd_init_lines(fdpssd);

    /* initialize write pointer, this is how we allocate new pages for writes */
    ssd_init_write_pointer(fdpssd);

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

static inline struct line *get_line(struct fdpssd *fdpssd, struct ppa *ppa)
{
    return &(fdpssd->lm.lines[ppa->g.blk]);
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
    struct line_mgmt *lm = &fdpssd->lm;
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    bool was_full_line = false;
    struct line *line;

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

    /* update corresponding line status */
    line = get_line(fdpssd, ppa);
    ftl_assert(line->ipc >= 0 && line->ipc < spp->pgs_per_line);
    if (line->vpc == spp->pgs_per_line) {
        ftl_assert(line->ipc == 0);
        was_full_line = true;
    }
    line->ipc++;
    ftl_assert(line->vpc > 0 && line->vpc <= spp->pgs_per_line);
    /* Adjust the position of the victime line in the pq under over-writes */
    if (line->pos) {
        /* Note that line->vpc will be updated by this call */
        pqueue_change_priority(lm->victim_line_pq, line->vpc - 1, line);
    } else {
        line->vpc--;
    }

    if (was_full_line) {
        /* move line: "full" -> "victim" */
        QTAILQ_REMOVE(&lm->full_line_list, line, entry);
        lm->full_line_cnt--;
        pqueue_insert(lm->victim_line_pq, line);
        lm->victim_line_cnt++;
    }
}

static void mark_page_valid(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    struct line *line;

    /* update page status */
    pg = get_pg(fdpssd, ppa);
    ftl_assert(pg->status == PG_FREE);
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(fdpssd, ppa);
    ftl_assert(blk->vpc >= 0 && blk->vpc < fdpssd->sp.pgs_per_blk);
    blk->vpc++;

    /* update corresponding line status */
    line = get_line(fdpssd, ppa);
    ftl_assert(line->vpc >= 0 && line->vpc < fdpssd->sp.pgs_per_line);
    line->vpc++;
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

/* move valid page data (already in DRAM) from victim line to a new page */
static uint64_t gc_write_page(struct fdpssd *fdpssd, struct ppa *old_ppa)
{
    struct ppa new_ppa;
    struct nand_lun *new_lun;
    uint64_t lpn = get_rmap_ent(fdpssd, old_ppa);

    ftl_assert(valid_lpn(fdpssd, lpn));
    new_ppa = get_new_page(fdpssd);
    /* update maptbl */
    set_maptbl_ent(fdpssd, lpn, &new_ppa);
    /* update rmap */
    set_rmap_ent(fdpssd, lpn, &new_ppa);

    mark_page_valid(fdpssd, &new_ppa);

    /* need to advance the write pointer here */
    ssd_advance_write_pointer(fdpssd);

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

static struct line *select_victim_line(struct fdpssd *fdpssd, bool force)
{
    struct line_mgmt *lm = &fdpssd->lm;
    struct line *victim_line = NULL;

    victim_line = pqueue_peek(lm->victim_line_pq);
    if (!victim_line) {
        return NULL;
    }

    if (!force && victim_line->ipc < fdpssd->sp.pgs_per_line / 8) {
        return NULL;
    }

    pqueue_pop(lm->victim_line_pq);
    victim_line->pos = 0;
    lm->victim_line_cnt--;

    /* victim_line is a danggling node now */
    return victim_line;
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

static void mark_line_free(struct fdpssd *fdpssd, struct ppa *ppa)
{
    struct line_mgmt *lm = &fdpssd->lm;
    struct line *line = get_line(fdpssd, ppa);
    line->ipc = 0;
    line->vpc = 0;
    /* move this line to free line list */
    QTAILQ_INSERT_TAIL(&lm->free_line_list, line, entry);
    lm->free_line_cnt++;
}

static int do_gc(struct fdpssd *fdpssd, bool force)
{
    struct line *victim_line = NULL;
    struct ssdparams *spp = &fdpssd->sp;
    struct nand_lun *lunp;
    struct ppa ppa;
    int ch, lun;

    victim_line = select_victim_line(fdpssd, force);
    if (!victim_line) {
        femu_err("FTL.c : 770 but GC doesn't happend to Inhoinno\n");
        return -1;
    }

    ppa.g.blk = victim_line->id;
    ftl_debug("GC-ing line:%d,ipc=%d,victim=%d,full=%d,free=%d\n", ppa.g.blk,
              victim_line->ipc, fdpssd->lm.victim_line_cnt, fdpssd->lm.full_line_cnt,
              fdpssd->lm.free_line_cnt);

    /* copy back valid data */
    for (ch = 0; ch < spp->nchs; ch++) {
        for (lun = 0; lun < spp->luns_per_ch; lun++) {
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

    /* update line status */
    mark_line_free(fdpssd, &ppa);

    return 0;
}

static uint64_t ssd_read(struct fdpssd *fdpssd, NvmeRequest *req)
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
 * ssd_write  
 * static uint64_t ssd_write(sturct fdpssd * fdpssd, NvmeRequest *req)
 * returns maxlat which means max latency.
 * max latency is the hightest value of latency 
 * which is calculated by ssd_advance_status per lpn(logical page number)
 * and this value is not sum of total latency but just highest latency while ssd_write()
 *  
 */
static uint64_t ssd_write(struct fdpssd *fdpssd, NvmeRequest *req)
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

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, fdpssd->sp.tt_pgs);
    }

    while (should_gc_high(fdpssd)) {
        /* perform GC here until !should_gc(fdpssd) */
        femu_err("In FTL.c :870 in ssd_write, GC triggered, to inhoinno");
        r = do_gc(fdpssd, true);
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
        ppa = get_new_page(fdpssd);
        /* update maptbl */
        set_maptbl_ent(fdpssd, lpn, &ppa);
        /* update rmap */
        set_rmap_ent(fdpssd, lpn, &ppa);

        mark_page_valid(fdpssd, &ppa);

        /* need to advance the write pointer here */
        ssd_advance_write_pointer(fdpssd);

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
                lat = ssd_write(fdpssd, req);
                //lat += 100000000; //IT WORKS AT CQ 1mil ms=100 000sec us=100sec ns=0.1sec
                break;
            case NVME_CMD_READ:
                lat = ssd_read(fdpssd, req);
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

            /* clean one line if needed (in the background) */
            if (should_gc(fdpssd)) {
                femu_err("In ftl.c:965 gc processed, to Inhoinno\n");
                do_gc(fdpssd, false);
            }
        }
    }

    return NULL;
}

