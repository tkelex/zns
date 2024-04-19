/*
 * @brief:Realize the management of the mapping scheme, and provide a unified mapping interface for the upper layer  
 * @date:2021.11.13
 * @author:tzh
 */

#include <string.h>

#include "sup_zns_mapping_strategy_interface.h"
#include "sup_zns_mapping_strategy_tradition.h"


static inline bool valid_ppa(struct zns_ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
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


static inline struct ssd_channel *get_ch(struct zns_ssd *ssd, struct ppa *ppa)
{
    return &(ssd->ch[ppa->g.ch]);
}

static inline struct nand_lun *get_lun(struct zns_ssd *ssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}


static inline struct nand_plane *get_pl(struct zns_ssd *ssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(ssd, ppa);
    return &(lun->pl[ppa->g.pl]);
}

static inline struct nand_block *get_blk(struct zns_ssd *ssd, struct ppa *ppa)
{
    struct nand_plane *pl = get_pl(ssd, ppa);
    return &(pl->blk[ppa->g.blk]);
}

//static inline struct line *get_line(struct zns_ssd *ssd, struct ppa *ppa)
//{
//    return &(ssd->lm.lines[ppa->g.blk]);
//}

static inline struct nand_page *get_pg(struct zns_ssd *ssd, struct ppa *ppa)
{
    struct nand_block *blk = get_blk(ssd, ppa);
    return &(blk->pg[ppa->g.pg]);
}


static void zns_ssd_init_nand_page(struct nand_page *pg, struct ssdparams *spp)
{
    pg->nsecs = spp->secs_per_pg;
	/* 这里sector   size = 4B，里面就只存了一个标记，标记其内部数据是否有效。真正的数据是存放在另一个地方 */
    pg->sec = g_malloc0(sizeof(nand_sec_status_t) * pg->nsecs);		
    for (int i = 0; i < pg->nsecs; i++) {
        pg->sec[i] = SEC_FREE;
    }
    pg->status = PG_FREE;
}


static void zns_ssd_init_nand_blk(struct nand_block *blk, struct ssdparams *spp)
{
    blk->npgs = spp->pgs_per_blk;
    blk->pg = g_malloc0(sizeof(struct nand_page) * blk->npgs);
    for (int i = 0; i < blk->npgs; i++) {
        zns_ssd_init_nand_page(&blk->pg[i], spp);
    }
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt = 0;
    blk->wp = 0;
}

static void zns_ssd_init_nand_plane(struct nand_plane *pl, struct ssdparams *spp)
{
    pl->nblks = spp->blks_per_pl;
    pl->blk = g_malloc0(sizeof(struct nand_block) * pl->nblks);
    for (int i = 0; i < pl->nblks; i++) {
        zns_ssd_init_nand_blk(&pl->blk[i], spp);
    }
}

static void zns_ssd_init_nand_lun(struct nand_lun *lun, struct ssdparams *spp)
{
    lun->npls = spp->pls_per_lun;
    lun->pl = g_malloc0(sizeof(struct nand_plane) * lun->npls);
    for (int i = 0; i < lun->npls; i++) {
        zns_ssd_init_nand_plane(&lun->pl[i], spp);
    }
    lun->next_lun_avail_time = 0;
    lun->busy = false;
}


static void zns_ssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp)
{
    ch->nluns = spp->luns_per_ch;
    ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    for (int i = 0; i < ch->nluns; i++) {
        zns_ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}


static void zns_ssd_init_params(struct ssdparams *spp)
{
    spp->secsz = 512;
    spp->secs_per_pg = 8;
    spp->pgs_per_blk = 1024;	/* block:4M */
    spp->blks_per_pl = 64; 		/* 可以再小 */
    spp->pls_per_lun = 2;
    spp->luns_per_ch = 8;
    spp->nchs = 8;				/* 32GB */

    spp->pg_rd_lat = NAND_READ_LATENCY;
    spp->pg_wr_lat = NAND_PROG_LATENCY;
    spp->blk_er_lat = NAND_ERASE_LATENCY;
    spp->ch_xfer_lat = 0;

    /* calculated values */
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

    spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch;
    spp->tt_pls = spp->pls_per_ch * spp->nchs;

    spp->tt_luns = spp->luns_per_ch * spp->nchs;

//    /* line is special, put it at the end */
//    spp->blks_per_line = spp->tt_luns; /* TODO: to fix under multiplanes */
//    spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
//    spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
//    spp->tt_lines = spp->blks_per_lun; /* TODO: to fix under multiplanes */

//    spp->gc_thres_pcent = 0.75;
//    spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * spp->tt_lines);
//    spp->gc_thres_pcent_high = 0.95;
//    spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * spp->tt_lines);
//    spp->enable_gc_delay = true;

//    check_params(spp);
}

#ifdef STATIC_MAP
static inline bool zns_check_page_valid(struct zns_ssd *ssd, struct ppa *ppa)
{
	struct nand_page *pg = NULL;

	pg = get_pg(ssd, ppa);
    if (pg->status == PG_VALID)
		return true;
	else 
		return false;
}

static void zns_mark_page_valid(struct zns_ssd *ssd, struct ppa *ppa)
{
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;	

    /* update page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_FREE);
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    ftl_assert(blk->vpc >= 0 && blk->vpc < ssd->sp.pgs_per_blk);
    blk->vpc++;	

}
#endif

/* 参考bbssd */
static int zns_ssd_init_internal_structure(zns_ssd_t *zns_ssd)
{
	femu_debug("In Func:%s\n", __FUNCTION__);

	struct ssdparams *spp = &zns_ssd->sp;
	
	zns_ssd_init_params(spp);
	
	/* initialize ssd internal layout architecture */
    zns_ssd->ch = g_malloc0(sizeof(struct ssd_channel) * zns_ssd->sp.nchs);
    for (int i = 0; i < spp->nchs; i++) {
        zns_ssd_init_ch(&zns_ssd->ch[i], spp);
    }

	return 0;
}

static int sup_zns_phy_m_strategy_select(const char *name, FemuCtrl *n)
{
	if (!strcmp(name, MAP_TRADITION_NAME)) {
		sup_zns_phy_m_tradition_register(n);
	} else {
		femu_log("Map strategy select Fail!\n");
	}

	return 0;
};



/* 提供给上层的接口为init、read、write */
int zns_ssd_init(const char *name, FemuCtrl *n)
{
	n->zns_ssd = (zns_ssd_t *)g_malloc0(sizeof(struct zns_ssd));

	n->zns_ssd->dataplane_started_ptr = &n->dataplane_started;
    n->zns_ssd->ssdname = (char *)n->devname;
	femu_debug("Starting FEMU in ZNS-SSD mode ...\n");
	
	/* Initialize the ZNS internal structure */
	zns_ssd_init_internal_structure(n->zns_ssd);

	/* Select a specific mapping strategy */
	sup_zns_phy_m_strategy_select(name, n);

	n->zns_ssd->zns_phy_m_strategy.phy_zone_mapping_init(name, n);

	/* Initialize Statistics */
	n->zns_ssd->sm_pl_sz = 0;
	n->zns_ssd->df_addr_type_sz = 0;
	n->zns_ssd->pl_sz = 0;
	
	n->zns_ssd->sum_serv_time_us = 0;

	return 0;
}

#ifdef STATIC_MAP
/* return: lat */
uint64_t zns_ssd_write(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info)
{
	uint64_t lat = 0;
	struct ssdparams *spp = &zns_ssd->sp;
	sup_zns_phy_m_strategy_t *map = &zns_ssd->zns_phy_m_strategy;
	uint64_t wp = zone_info->wp;
	uint32_t nlb = zone_info->nlb;
	uint32_t lbasz = zone_info->lbasz;
	uint32_t zone_idx = zone_info->zone_idx;
	struct ppa ppa;
	uint64_t i = wp;    
	int32_t para_level = 0;
	struct nand_lun **lun_group = NULL;
	struct nand_lun *first_lun = NULL;
	uint32_t *lun_num;
	uint32_t tt_lba_page = spp->secs_per_pg * spp->secsz / lbasz;
	uint32_t page_num= (nlb-1) / tt_lba_page + 1;
	uint32_t *page_arr;		//保存了该req给每个die各自分了多少页，die索引和lun_group中对齐
	int32_t temp = -1;
	int32_t fl = -1;

	for (; i <= wp + nlb; i += tt_lba_page) {	//wp应该页对齐
		ppa = map->get_ppa_by_zone_info(zns_ssd, zone_idx, wp, lbasz);
		zns_mark_page_valid(zns_ssd, &ppa);

		if (i == wp)
			first_lun = get_lun(zns_ssd, &ppa);		//找到第一个wp对应的lun
	}


	para_level = map->get_para_level(zone_idx);
	lun_group = g_malloc0((size_t)para_level * sizeof(struct nand_lun *));
	lun_num = g_malloc0((size_t)para_level * sizeof(uint32_t));
	page_arr = g_malloc0((size_t)para_level * sizeof(uint32_t));

	map->get_lun(lun_num, zone_idx);
	for (i = 0; i < para_level; i++) {
		ppa.g.ch = lun_num[i] % spp->nchs;
		ppa.g.lun = lun_num[i] / spp->nchs;
		lun_group[i] = get_lun(zns_ssd, &ppa);

		if (lun_group[i] == first_lun)
			temp = i;
	}

	assert(temp >= 0 && temp < para_level);
	fl = temp;
	memset(page_arr, 0, para_level * sizeof(uint32_t));
	for (i = 1; i <= page_num; i++) {
		page_arr[temp]++;
		if (++temp == para_level)
			temp = 0;
	}

	struct nand_cmd swr = {
		.type = USER_IO,
		.cmd = NAND_WRITE,
		.stime = req->stime,
	};
		
	lat = map->zone_advance_avail_time(zns_ssd, &swr, lun_group, page_arr);

	/* debug */
	static uint64_t req_num = 0;
	req_num++;
//	femu_debug("Write: zone_idx=%u, nlb=%u, pl=%d, fs_lun=%u, lat=%lu ns\n", zone_idx, nlb, para_level, lun_num[0], lat);
	femu_debug("Write: zone_idx=%u, nlb=%u, fDie=%d, wp=%lu, rNum=%lu, lat=%lu, st=%ld\n", zone_idx, nlb, fl, wp, req_num, lat, req->stime);

	g_free(lun_group);
	g_free(lun_num);
	g_free(page_arr);

	return lat;
}



uint64_t zns_ssd_read(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info)
{
	uint64_t lat = 0;
	struct ssdparams *spp = &zns_ssd->sp;
	sup_zns_phy_m_strategy_t *map = &zns_ssd->zns_phy_m_strategy;
	uint64_t wp = zone_info->wp;
	uint32_t nlb = zone_info->nlb;
	uint32_t lbasz = zone_info->lbasz;
	uint32_t zone_idx = zone_info->zone_idx;
	struct ppa ppa;
	uint64_t i = wp;    
	int32_t para_level = 0;
	struct nand_lun **lun_group = NULL;
	struct nand_lun *first_lun = NULL;
	uint32_t *lun_num;
	uint32_t tt_lba_page = spp->secs_per_pg * spp->secsz / lbasz;
	uint32_t page_num= (nlb-1) / tt_lba_page + 1;
	uint32_t *page_arr;		
	int32_t temp = -1;
		
	for (; i <= wp + nlb; i += tt_lba_page) {
		ppa = map->get_ppa_by_zone_info(zns_ssd, zone_idx, wp, lbasz);
		if (i == wp)
			first_lun = get_lun(zns_ssd, &ppa);
		
//		if (valid_ppa(zns_ssd, &ppa) &&  zns_check_page_valid(zns_ssd, &ppa)) {
//			continue;
//		} else {
//			femu_log("Zns Read: ppa error!\n");
//		}
	}

	para_level = map->get_para_level(zone_idx);
	lun_group = g_malloc0((size_t)para_level * sizeof(struct nand_lun *));
	lun_num = g_malloc0((size_t)para_level * sizeof(uint32_t));
	page_arr = g_malloc0((size_t)para_level * sizeof(uint32_t));

	map->get_lun(lun_num, zone_idx);
	for (i = 0; i < para_level; i++) {
		ppa.g.ch = lun_num[i] % spp->nchs;
		ppa.g.lun = lun_num[i] / spp->nchs;
		lun_group[i] = get_lun(zns_ssd, &ppa);

		if (lun_group[i] == first_lun)
			temp = i;
	}

	assert(temp >= 0 && temp < para_level);
	memset(page_arr, 0, para_level * sizeof(uint32_t));
	for (i = 1; i <= page_num; i++) {
		page_arr[temp]++;
		if (++temp == para_level)
			temp = 0;
	}

	struct nand_cmd swr = {
		.type = USER_IO,
		.cmd = NAND_READ,
		.stime = req->stime,
	};
		
	lat = map->zone_advance_avail_time(zns_ssd, &swr, lun_group, page_arr);
	
	return lat;
}

uint64_t zns_ssd_erase(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info)
{

	return 0;
}

#endif



