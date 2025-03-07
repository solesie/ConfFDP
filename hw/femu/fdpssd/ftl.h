#ifndef __FEMU_FTL_H
#define __FEMU_FTL_H

#include "../nvme.h"

#define INVALID_PPA     (~(0ULL))
#define INVALID_LPN     (~(0ULL))
#define UNMAPPED_PPA    (~(0ULL))

enum {
    NAND_READ =  0,
    NAND_WRITE = 1,
    NAND_ERASE = 2,

    NAND_READ_LATENCY = 65000,
    NAND_PROG_LATENCY = 450000,
    NAND_ERASE_LATENCY= 2000000,
};

enum {
    USER_IO = 0,
    GC_IO = 1,
};

enum {
    SEC_FREE = 0,
    SEC_INVALID = 1,
    SEC_VALID = 2,

    PG_FREE = 0,
    PG_INVALID = 1,
    PG_VALID = 2
};

enum {
    FEMU_ENABLE_GC_DELAY = 1,
    FEMU_DISABLE_GC_DELAY = 2,

    FEMU_ENABLE_DELAY_EMU = 3,
    FEMU_DISABLE_DELAY_EMU = 4,

    FEMU_RESET_ACCT = 5,
    FEMU_ENABLE_LOG = 6,
    FEMU_DISABLE_LOG = 7,
};


#define BLK_BITS    (16)
#define PG_BITS     (16)
#define SEC_BITS    (8)
#define PL_BITS     (8)
#define LUN_BITS    (8)
#define CH_BITS     (7)

/* describe a physical page addr */
struct ppa {
    union {
        struct {
            uint64_t blk : BLK_BITS;
            uint64_t pg  : PG_BITS;
            uint64_t sec : SEC_BITS;
            uint64_t pl  : PL_BITS;
            uint64_t lun : LUN_BITS;
            uint64_t ch  : CH_BITS;
            uint64_t rsv : 1;
        } g;

        uint64_t ppa;
    };
};

typedef int nand_sec_status_t;

struct nand_page {
    nand_sec_status_t *sec;
    int nsecs;
    int status;
};

struct nand_block {
    struct nand_page *pg;
    int npgs;
    int ipc; /* invalid page count */
    int vpc; /* valid page count */
    int erase_cnt;
    int wp; /* current write pointer */
};

struct nand_plane {
    struct nand_block *blk;
    int nblks;
};

struct nand_lun {
    struct nand_plane *pl;
    int npls;
    uint64_t next_lun_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssd_channel {
    struct nand_lun *lun;       //logical unit
    int nluns;
    uint64_t next_ch_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssdparams {
    int secsz;        /* sector size in bytes */
    int secs_per_pg;  /* # of sectors per page */
    int pgs_per_blk;  /* # of NAND pages per block */
    int pls_per_lun;  /* # of planes per LUN (Die) */
    int luns_per_ch;  /* # of LUNs per channel */
    int nchs;         /* # of channels in the SSD */

    int pg_rd_lat;    /* NAND page read latency in nanoseconds */
    int pg_wr_lat;    /* NAND page program latency in nanoseconds */
    int blk_er_lat;   /* NAND block erase latency in nanoseconds */
    int ch_xfer_lat;  /* channel transfer latency for one page in nanoseconds
                       * this defines the channel bandwith
                       */

    double gc_thres_pcent;
    int gc_thres_superblocks;
    double gc_thres_pcent_high;
    int gc_thres_superblocks_high;
    bool enable_gc_delay;

    /* below are all calculated values */
    int blks_per_pl;  /* # of blocks per plane */

    int secs_per_blk; /* # of sectors per block */
    int secs_per_pl;  /* # of sectors per plane */
    int secs_per_lun; /* # of sectors per LUN */
    int secs_per_ch;  /* # of sectors per channel */
    int tt_secs;      /* # of sectors in the SSD */

    int pgs_per_pl;   /* # of pages per plane */
    int pgs_per_lun;  /* # of pages per LUN (Die) */
    int pgs_per_ch;   /* # of pages per channel */
    int tt_pgs;       /* total # of pages in the SSD */

    int blks_per_lun; /* # of blocks per LUN */
    int blks_per_ch;  /* # of blocks per channel */
    int tt_blks;      /* total # of blocks in the SSD */

    int secs_per_superblock;
    int pgs_per_superblock;
    int blks_per_superblock;
    int tt_superblock;

    int secs_per_ru;
    int pgs_per_ru;
    int blks_per_ru;
    int tt_ru;
    int superblocks_per_ru;

    int pls_per_ch;   /* # of planes per channel */
    int tt_pls;       /* total # of planes in the SSD */

    int tt_luns;      /* total # of LUNs in the SSD */

    struct {
        uint16_t nrg;
        uint16_t chs_per_rg;
        uint16_t ways_per_rg;

        uint16_t nruh;
    } fdp;
};

enum SuperblockOwner{
    SBO_UNOWNED,
    SBO_INITIALLY_ISOLATED_RUH,
    SBO_RG,
    SBO_PERSISTENTLY_ISOLATED_RUH,
};

typedef struct Superblock {
    /* solesie: superblock id.
       rg + offt are enough to indicate unique sb */
    union {
        struct {
            uint8_t rg;
            uint32_t offt;

            uint8_t sbo;
            uint16_t ruh;
        } n;
        uint64_t sb_id;
    };

    int ipc; /* invalid page count in this superblock */
    int vpc; /* valid page count in this superblock */
    QTAILQ_ENTRY(Superblock) entry; /* in either {free,victim,full} list */
    /* position in the priority queue for victim Superblock */
    size_t                  pos; 
} Superblock;

/* wp: record next write addr */
typedef struct WritePointer {
    struct Superblock *cur_sb;
    int ch;
    int lun;
    int pg;
    int blk;
    int pl;
} WritePointer;

typedef struct ReclaimGroup {
    Superblock *superblocks;
    /* free superblock list, we only need to maintain a list of superblock numbers */
    QTAILQ_HEAD(free_superblock_list, Superblock) free_superblock_list;
    pqueue_t *victim_superblock_pq;
    QTAILQ_HEAD(full_superblock_list, Superblock) full_superblock_list;

    int tt_superblock;
    int free_superblock_cnt;
    int victim_superblock_cnt;
    int full_superblock_cnt;
    WritePointer *wp; // solesie: This WP is used for INITIALLY ISOLATED GC.

    int start_ch, end_ch;
    int start_way, end_way;
} ReclaimGroup;

struct nand_cmd {
    int type;
    int cmd;
    int64_t stime; /* Coperd: request arrival time */
};

typedef struct ReclaimUnitHandle {
    /* solesie: These WPs are used to point to RUs indexed by RG 
       and are also used for PERSISTENT ISOLATED GC.*/
    WritePointer *wps;
} ReclaimUnitHandle;

struct fdpssd {
    char *ssdname;
    struct ssdparams sp;
    struct ssd_channel *ch;
    struct ppa *maptbl; /* page level mapping table */
    uint64_t *rmap;     /* reverse mapptbl, assume it's stored in OOB */

    /* solesie: FDP */
    ReclaimUnitHandle *ruhs;
    ReclaimGroup *rgs;
    int **chway2rg;

    /* lockless ring for communication with NVMe IO thread */
    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    bool *dataplane_started_ptr;
    QemuThread ftl_thread;
};

void fdpssd_init(FemuCtrl *n);

#ifdef FEMU_DEBUG_FTL
#define ftl_debug(fmt, ...) \
    do { printf("[FEMU] FTL-Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ftl_debug(fmt, ...) \
    do { } while (0)
#endif

#define ftl_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] FTL-Err: " fmt, ## __VA_ARGS__); } while (0)

#define ftl_log(fmt, ...) \
    do { printf("[FEMU] FTL-Log: " fmt, ## __VA_ARGS__); } while (0)


/* FEMU assert() */
#ifdef FEMU_DEBUG_FTL
#define ftl_assert(expression) assert(expression)
#else
#define ftl_assert(expression)
#endif

#endif
