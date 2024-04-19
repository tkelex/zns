/*
 * @brief:Realize ZNS traditional mapping method
 * @date:2021.11.20
 * @author:tzh
 */

#include "sup_zns_mapping_strategy_tradition.h"

static sup_zns_phy_map_tradition_t zns_map_trad;

static int phy_zone_mapping_init_tradition(const char *name, FemuCtrl *n)
{
	zns_ssd_t *zns_ssd = n->zns_ssd;
	struct ssdparams *spp = &zns_ssd->sp;
	uint64_t secs_per_lun = spp->secs_per_lun;	//防止数据过大溢出，溢出会导致结果错误
	uint64_t secsz = spp->secsz;

	assert(!strcmp(name, MAP_TRADITION_NAME));

	zns_map_trad.block_group_num_per_die = (PARA_LEVEL * secs_per_lun * secsz) / n->zone_size_bs;		/* 都是以B为单位 */
	zns_map_trad.die_group = (uint32_t *)g_malloc0((size_t)(sizeof(uint32_t) * PARA_LEVEL));

	femu_debug("In Func:%s, block_group_num = %lu\n", __FUNCTION__, zns_map_trad.block_group_num_per_die);

	return 0;
}

static int get_para_level_by_zone_info_tradition(uint32_t zone_index)
{
	(void)zone_index;
	
	return PARA_LEVEL;
}

static void get_lun_by_zone_info_tradition(uint32_t *die_num, uint32_t zone_idx)
{
	int first_die_idx = zone_idx / zns_map_trad.block_group_num_per_die * PARA_LEVEL;
	int i;
	
	for (i = 0; i < PARA_LEVEL; i++) {
		die_num[i] = first_die_idx++;
	}
}


static void get_ppa_by_blkg_info(struct ppa *ppa, struct ssdparams *spp, uint32_t tt_blkg, uint32_t blkg_offset, uint32_t page_offset_in_blkg)
{
	int blocks_per_blkg = spp->blks_per_lun / tt_blkg;
	int start_block_the_blkg = blkg_offset * blocks_per_blkg;
	int block_offset_in_blkg = page_offset_in_blkg / spp->pgs_per_blk;
	int page_offset_in_block = page_offset_in_blkg % spp->pgs_per_blk;

	/* 根据绝对block编号来计算plane index */
	int pl_offset_in_die = (start_block_the_blkg+block_offset_in_blkg) / spp->blks_per_pl;
	int block_offset_in_plane = (start_block_the_blkg+block_offset_in_blkg) % spp->blks_per_pl;

	ppa->g.pl = pl_offset_in_die;
	ppa->g.blk = block_offset_in_plane;
	ppa->g.pg = page_offset_in_block;
}


/* 该wp为该zone的slba的相对地址，注意上层调用时的传值 */
static struct ppa get_ppa_by_zone_info_tradition(struct zns_ssd *zns_ssd, uint32_t zone_index, uint64_t wp, uint32_t lbasz)
{
	struct ssdparams *spp = &zns_ssd->sp;
	uint32_t first_die_index = 0;
	uint32_t blkg_ofst_in_die = 0, die_ofst_in_chan, channel_ofst;
	uint32_t page_ofst_in_zone = 0, die_offset = 0, page_ofst_in_blkg = 0;
	uint32_t i;
	struct ppa ppa = {0};

	first_die_index = zone_index / zns_map_trad.block_group_num_per_die * PARA_LEVEL;
	for (i = 0; i < PARA_LEVEL; i++) {
		zns_map_trad.die_group[i] = first_die_index++;
	}

	blkg_ofst_in_die = zone_index % zns_map_trad.block_group_num_per_die;

	page_ofst_in_zone = wp / (spp->secs_per_pg * spp->secsz / lbasz);
	die_offset = page_ofst_in_zone % PARA_LEVEL;			/* DIE组内的die_offset */
	page_ofst_in_blkg = page_ofst_in_zone / PARA_LEVEL;	
	
	channel_ofst = zns_map_trad.die_group[die_offset] % spp->nchs;
	die_ofst_in_chan = zns_map_trad.die_group[die_offset] / spp->nchs; 

	ppa.g.ch = channel_ofst;
	ppa.g.lun = die_ofst_in_chan;
	get_ppa_by_blkg_info(&ppa, spp, zns_map_trad.block_group_num_per_die, blkg_ofst_in_die, page_ofst_in_blkg);

	return ppa;
}



/* 
 * 1. 找到zone映射的die_group, die_gruop里面的die一起响应同一个请求
 * 2. 查询die_group中每一个die是否空闲，若空闲, 则响应，否则阻塞等待
 * 3. 返回响应最久的那个die的响应时间maxlat
 */
static int zone_advance_avail_time_tradition(struct zns_ssd *zns_ssd, struct nand_cmd *ncmd, struct nand_lun **lun_group, uint32_t *page_arr)
{
	int c = ncmd->cmd;
	uint64_t nand_stime;						//flash开始执行该情求的时间
	struct ssdparams *spp = &zns_ssd->sp;
	struct nand_lun *lun = NULL;
	uint64_t cmd_stime = (ncmd->stime == 0) ? qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;

	int curlat = 0, maxlat = 0;
	uint32_t i = 0;
	uint32_t blocks_per_die = spp->blks_per_lun / zns_map_trad.block_group_num_per_die;
	uint32_t page_num = 0;

	for (i = 0; i < PARA_LEVEL; i++) {
		lun = lun_group[i];
		assert(lun != NULL);
		page_num = page_arr[i];
		if (page_num <= 0)
			continue;
	
	    switch (c) {
	    case NAND_READ:
	        /* read: perform NAND cmd first */
	        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
	                     lun->next_lun_avail_time;
	        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat * page_num;
	        curlat = lun->next_lun_avail_time - cmd_stime;
#if 0
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
	            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat * page_num;
	        } else {
	            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat * page_num;
	        }
	        curlat = lun->next_lun_avail_time - cmd_stime;

#if 0
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
	        lun->next_lun_avail_time = nand_stime + spp->blk_er_lat * blocks_per_die;

	        curlat = lun->next_lun_avail_time - cmd_stime;
	        break;

	    default:
	        ftl_err("Unsupported NAND command: 0x%x\n", c);
	    }
		
		maxlat = (curlat > maxlat) ? curlat : maxlat;
	}
	
    return maxlat;
}




int sup_zns_phy_m_tradition_register(FemuCtrl *n)
{
	n->zns_ssd->zns_phy_m_strategy = (sup_zns_phy_m_strategy_t) {
		.name = MAP_TRADITION_NAME,
		.map_data = (void *)&zns_map_trad,
		.phy_zone_mapping_init = phy_zone_mapping_init_tradition,
		.get_ppa_by_zone_info = get_ppa_by_zone_info_tradition,
		.zone_advance_avail_time = zone_advance_avail_time_tradition,
		.get_para_level = get_para_level_by_zone_info_tradition,
		.get_lun = get_lun_by_zone_info_tradition,
	};

	return 0;
}


