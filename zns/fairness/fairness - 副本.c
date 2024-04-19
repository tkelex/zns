/*
 * @brief: Implements ZNS
 fairness scheme in it.
 * @date: 2023.05.15
 * @author: tzh
 */

#include "fairness.h"


/* 单链表的实现，将其当作队列使用 */
static void InitList(List_t *list)							
{
	assert(list);
	list->head = (Node_t *)g_malloc0(sizeof(Node_t));
	list->head->next = NULL;
	list->head->prev = NULL;
	list->tail = list->head;
	list->len = 0;
}


static void InsertInTail(List_t *list, elemtype data)
{
	assert(list);
	Node_t *temp = (Node_t *)g_malloc0((size_t)sizeof(Node_t));
	temp->data = data;
	temp->next = NULL;

	list->tail->next = temp;
	temp->prev = list->tail;
	list->tail = temp;
	list->len++;
}

//static void DeleteInTail(List_t *list, elemtype *data)
//{
//	assert(list);
//	Node_t* p;
//	p = list->tail;
//	list->tail = p->prev;
//	list->tail->next = NULL;
//	*data = p->data;
//	list->len--;
//
//	g_free(p);
//}


//static int32_t DeleteInHead(List_t *list, elemtype *data)
//{
//	assert(list);
//	Node_t* p;
//	if (list->len == 0)		/* 空链表 */
//		return -1;
//
//	p = list->head->next;
//	if (1 == list->len) {
//		DeleteInTail(list, data);
//		return 0;
//	}
//	list->head->next = p->next;
//	p->next->prev = list->head;
//	*data = p->data;
//	list->len--;
//	g_free(p);
//
//	return 0;
//}

static int32_t DeleteInHead(List_t *list, elemtype *data)
{
	assert(list);
	Node_t *p = list->head->next;
	
	if (p) {
		assert(p->prev == list->head);
		list->head->next = p->next;
		if (p->next) { /* P不是尾结点 */
			p->next->prev = list->head;
		} else {	/* P是尾结点，更新尾指针 */
			list->tail = list->head;
		}
		*data = p->data;
		list->len--;
		g_free(p);

		return 0;
	} 

	return -1;
}


/* ZNS SSD 内部操作 */
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

static inline uint32_t zns_zone_idx(FemuCtrl *n, uint64_t slba)
{
    return (n->zone_size_log2 > 0 ? slba >> n->zone_size_log2 : slba /
            n->zone_size);
}

#if 0
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

void release_ts(FemuCtrl *n, int index_poller, flash_ts_t *ts, uint64_t curlat)
{
	assert(ts);
	int j;
	
//	ts->req_plus->maxlat = (curlat > maxlat) ? curlat : maxlat;	//TODO：不需要这个，请求执行延迟就是最后一个事务被执行的延迟
//	femu_debug("clat=%lu, mlat=%lu, rmlat=%lu\n", curlat, maxlat, ts->req_plus->maxlat);

	ts->req_plus->maxlat = curlat;	//请求执行延迟就是最后一个事务被执行的延迟

	if (--ts->req_plus->ts_num == 0) {	/* 该次req的所有事务都完成，将其提交到CQ中去 */
		ts->req_plus->req->reqlat = ts->req_plus->maxlat;
		ts->req_plus->req->expire_time += ts->req_plus->maxlat;
		if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&ts->req_plus->req, 1) != 1) {
			femu_debug("ZNS to_poller enqueue failed\n");
	    }
		
//		femu_debug("Type %u: Zone(%u), lat=%lu, stime=%ld\n",  ts->req_plus->cmd_type, ts->zone_idx, ts->req_plus->maxlat, ts->req_plus->req->stime);												

		g_free(ts->req_plus->flash_ts);
		g_free(ts->req_plus);
		g_free(ts);
		
		return;
	}

	for (j = 0; j < ts->req_plus->ts_sum; j++) {	/* 释放该ts所占用的空间 */
		if (ts->req_plus->flash_ts[j] == ts) {
			ts->req_plus->flash_ts[j] = NULL;
			g_free(ts);
			break;
		}
	}
	
}



/* 在指定的die组内部查找合适的FBG
 * FBG为一个die中所有plane相同偏移地址的一组block
 * 原则：根据fbg是完整映射还是部分映射去轮询地映射
 * 部分映射时：1.保证被部分使用的fbg优先被分配; 2.几个die组内的fbg偏移相同
 * fbg_type: 代表寻找的目标FBG为几部分
 * 目前仅支持两种fbg_type共存，1和2，1和4，仅1，仅2，仅4
 */
void alloc_fbg(zns_dynamic_mapping_t *dymap_p, uint32_t fbg_type, uint32_t dg_idx, uint32_t *fbg_ofst, uint32_t *fbg_part)
{
	uint32_t i = 0, max_next_fbg = 0;
	die_group_t *die_group = &dymap_p->die_group[dg_idx];

	if (fbg_type == 1) { //完整映射，直接映射当前die组的next_fbg
		assert(die_group && die_group->remain_fbgs_num > 0);	
		while (die_group->fbgs_used_bitmap[die_group->next_fbg_id] > 0) {	//Note：die_group->remain_fbgs_num必须准确，不然就会死循环
			die_group->next_fbg_id++;
			if (die_group->next_fbg_id == dymap_p->fbgs_per_dg) {
				die_group->next_fbg_id = 0;
			}
		}

		//TODO:检查这一组物理块是否真的空闲(物理上)
		die_group->remain_fbgs_num--;
		die_group->fbgs_used_bitmap[die_group->next_fbg_id]++;
		*fbg_ofst = die_group->next_fbg_id;
		
		if (++die_group->next_fbg_id == dymap_p->fbgs_per_dg) 
			die_group->next_fbg_id = 0;
		assert(die_group->next_fbg_id < dymap_p->fbgs_per_dg);
	} else {	//部分映射，都是传那一组中较小的dg_idx, 仅支持两种fbg_type共存
		if (die_group->part_map_flag == 1) { //存在部分映射的fbg, 直接使用
			assert(die_group->fbg_part_map.fbg_part_sum == fbg_type 
					&& die_group->fbg_part_map.next_fbg_part > 0);
//			assert(die_group->fbgs_used_bitmap[die_group->fbg_part_map.fbg_id] > 0); //有些部分映射zone会被reset
			*fbg_ofst = die_group->fbg_part_map.fbg_id;
			*fbg_part = die_group->fbg_part_map.next_fbg_part;

			for (i = dg_idx; i < dg_idx + fbg_type; i++) { 	//所有die组的信息都需要更新
				die_group_t *temp_dg = &dymap_p->die_group[i];
				temp_dg->fbgs_used_bitmap[*fbg_ofst]++;
			}
			
			if (++die_group->fbg_part_map.next_fbg_part == fbg_type) {	//该fbg被用完
				die_group->part_map_flag = 0;
			}
		} else { //不存在部分映射，从next_fbg向下取			
			for (i = dg_idx; i < dg_idx + fbg_type; i++) {	//为了fbg偏移统一，使用几个die组中最大的那个next
				assert(i < dymap_p->die_group_sum);
				die_group_t *temp_dg = &dymap_p->die_group[i];
				if (temp_dg->next_fbg_id > max_next_fbg) {
					max_next_fbg = temp_dg->next_fbg_id;	
//					max_idx = i;
				}
			}
			
			femu_debug("max_fbg_ofst = %u\n", max_next_fbg); 

			*fbg_ofst = max_next_fbg;	//所有die组都分配该fbg
			*fbg_part = 0;				//第一次使用该fbg
			for (i = dg_idx; i < dg_idx + fbg_type; i++) { 	//所有die组的信息都需要更新
				die_group_t *temp_dg = &dymap_p->die_group[i];
				temp_dg->fbgs_used_bitmap[*fbg_ofst]++;		//更新位图，reset需要
			
				if (++temp_dg->next_fbg_id == dymap_p->fbgs_per_dg)	//所有的die组都从该位置开始轮询，忽略被跳过的
					temp_dg->next_fbg_id = 0;
				assert(temp_dg->next_fbg_id < dymap_p->fbgs_per_dg);

				temp_dg->remain_fbgs_num--;	//部分分配也算分配
			}
			
			die_group->part_map_flag = 1;	//只填当前这个die组就可以，因为多个die组时遵循第一个die组的信息
			die_group->fbg_part_map.fbg_id = max_next_fbg;
			die_group->fbg_part_map.fbg_part_sum = fbg_type;
			die_group->fbg_part_map.next_fbg_part = 1;
		}
	}
}


/* 打开一个zone
 * 1.记录打开zone的数量，感受open zone limit
 * 2.根据数量映射资源
 * 3.记录映射后的资源
 * TODO: 实现感知open zone数量的动态变化
 */
void zone_open(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;

	assert(zone_idx < n->num_zones);
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];
	uint32_t i = 0;
	uint32_t min_used_num = 100, min_idx = 0;
	uint32_t fbg_ofst = 0, fbg_part = 0;
	uint32_t avail_dg[4];
	uint32_t avail_dg_len = 0;
	uint32_t map_zone_num = 0;	//映射时保证几个zone占用全部资源

	for (i = 0; i < dymap_p->last_opened_zones_num; i++) {
		if (dymap_p->opened_zones[i] == zone_idx) {	//已被上次任务打开
			return;
		}
	}

	dymap_p->opened_zones[dymap_p->opened_zones_num] = zone_idx;
	dymap_p->opened_zones_num++;

	if (dymap_p->st_stage_flag) { //位于初始阶段
		assert(dymap_p->opened_zones_num <= dymap_p->open_zone_limit);
		map_zone_num = dymap_p->open_zone_limit;
		if (dymap_p->opened_zones_num == dymap_p->open_zone_limit) {
			dymap_p->st_stage_flag = 0;
		}
	} else {			//处于后续阶段, 为了方便只实现了目前用的几个例子
		if (dymap_p->opened_zones_num <= 2)
			map_zone_num = dymap_p->opened_zones_num;	//1 or 2
		else
			map_zone_num = dymap_p->die_group_sum;		//=4		
	}
	
	femu_debug("Opened zone %u, open sum = %u, open limit = %u, map zone num = %u\n", 
			zone_idx, dymap_p->opened_zones_num, dymap_p->open_zone_limit, map_zone_num);

	if (map_zone_num < dymap_p->die_group_sum) {	//部分映射，每个zone映射多个die组中相同偏移的FBG
		phy_zone->map_die_group_num = dymap_p->die_group_sum / dymap_p->open_zone_limit; 	//目前仅支持对齐的数量
		for (i = 0; i < dymap_p->die_group_sum; i += phy_zone->map_die_group_num) {			//只看前面的那一个
			if (dymap_p->die_group_used_bitmap[i] < min_used_num) {
				min_used_num = dymap_p->die_group_used_bitmap[i];
				min_idx = i;
			}
		}

		//Todo: 判断是否有空闲FBG
		for (i = 0; i < phy_zone->map_die_group_num; i++) {
			femu_debug("min_idx=%u, used num=%u\n", min_idx+i, min_used_num);
			phy_zone->die_groups[i] = min_idx + i;
			dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]]++;
		}

		//进一步轮询地映射具体的FBG
		alloc_fbg(dymap_p, phy_zone->map_die_group_num, phy_zone->die_groups[0], &fbg_ofst, &fbg_part);
		phy_zone->fbg_ofst = fbg_ofst;
		phy_zone->fbg_part = fbg_part;
		
	} else {	//完整映射，每个zone映射一个die组
		phy_zone->map_die_group_num = 1;
		//先找出最合适的die组，遍历使用位图，在具有空闲fbg的die组中找出最小的位图
		for (i = 0; i < dymap_p->die_group_sum; i++) {	//先找出具有空闲fbg的die组
			if (dymap_p->die_group[i].remain_fbgs_num > 0) {
				avail_dg[avail_dg_len++] = i;
			}
		}

//		femu_debug("avail_dg_len = %u\n", avail_dg_len);
		for (i = 0; i < avail_dg_len; i++) {	//再在这些die组中寻找最小位图
			if (dymap_p->die_group_used_bitmap[avail_dg[i]] < min_used_num) {
				min_used_num = dymap_p->die_group_used_bitmap[avail_dg[i]];
				min_idx = avail_dg[i];
			}
		}
		
		//映射，更新位图
		femu_debug("min_idx=%u, used num=%u\n", min_idx, min_used_num);
	    assert(min_used_num != 100);	//=100说明没有空闲的FBG了
		
		phy_zone->die_groups[0] = min_idx;
		dymap_p->die_group_used_bitmap[min_idx]++;

		//进一步轮询地映射具体的FBG
		alloc_fbg(dymap_p, 1, phy_zone->die_groups[0], &fbg_ofst, NULL);
		phy_zone->fbg_ofst = fbg_ofst;
		phy_zone->fbg_part = 0;
	}
}

/* 关闭一个zone，回收其物理资源 */
void zone_close(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;

	assert(zone_idx < n->num_zones);
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];

	uint32_t i = 0, j = 0, open_flag = 0;
	uint32_t temp[MAXOPENZONES];

	for (i = 0; i < dymap_p->opened_zones_num; i++) {
		if (zone_idx == dymap_p->opened_zones[i]) {
			femu_debug("closed zone %u in Open status\n", zone_idx);
			open_flag = 1;
		}
	}

	if (open_flag == 1) {	//处于open状态才能释放资源
		for (i = 0; i < phy_zone->map_die_group_num; i++) {	//释放这个zone所占据的并行资源
//			femu_debug("bitmap = %u\n", dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]]);
			assert(dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]] > 0);
			dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]]--;	
		}

		//将该zone移除open zones，读出所有open zone，再写进去-整理
		for (i = 0, j = 0; i < dymap_p->opened_zones_num; i++) {	
//			femu_debug("%u\n", dymap_p->opened_zones[i]);
			if (dymap_p->opened_zones[i] != zone_idx) {
				temp[j] = dymap_p->opened_zones[i];
				j++;
			}
		}
		dymap_p->opened_zones_num--;
		assert(dymap_p->opened_zones_num == j);
		for (i = 0; i < dymap_p->opened_zones_num; i++) {
			dymap_p->opened_zones[i] = temp[i];
//			femu_debug("%u\n", dymap_p->opened_zones[i]);
		}
		
	} else {
		femu_debug("close unopened zone %u, error!\n", zone_idx);
	}
	
}

/* 返回是否发布了实际的擦除req，是(1)，否(0) */
uint32_t zone_reset(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];
	struct ssdparams *spp = &zns_ssd->sp;
	die_group_t *die_group = NULL;
	uint32_t i = 0, j = 0;

	die_group = &dymap_p->die_group[phy_zone->die_groups[0]];		//取第一个die组即可，因为后续die组同第一个
	femu_debug("Reset: zone(%u) fbg(%u, %u) bit map = %u\n", 	\
	zone_idx, phy_zone->die_groups[0], phy_zone->fbg_ofst, die_group->fbgs_used_bitmap[phy_zone->fbg_ofst]);

//	assert(die_group->fbgs_used_bitmap[phy_zone->fbg_ofst] > 0);	//必须被使用
	if (die_group->fbgs_used_bitmap[phy_zone->fbg_ofst] == 0) {		//仅在首次挂载Zenfs时会出现
		return 0;
	}
	if ((die_group->fbgs_used_bitmap[phy_zone->fbg_ofst] - 1) == 0) { 	//没有zone占用该fbg，擦除
		NvmeRequestPlus_t *req_plus = g_malloc(sizeof(NvmeRequestPlus_t));		//封装擦除req	
		req_plus->req = req;
		req_plus->maxlat = 0;
		req_plus->cmd_type = NAND_ERASE;		

		req_plus->ts_sum = req_plus->ts_num = phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA;
		req_plus->flash_ts = (flash_ts_t **)g_malloc(sizeof(flash_ts_t *) * req_plus->ts_sum);

		for (i = 0, j = die_group->die[0]; i < req_plus->ts_sum; i++) {		//从映射的第一个die开始，die组之间的die也是连续的
			req_plus->flash_ts[i] =(flash_ts_t *)g_malloc(sizeof(flash_ts_t));	/* 要分开申请，不然释放时会出错，注意释放空间 */
			req_plus->flash_ts[i]->req_plus = req_plus;
			req_plus->flash_ts[i]->zone_idx = zone_idx;
			req_plus->flash_ts[i]->die_idx = j;
			assert(++j <= spp->tt_luns);

			req_plus->flash_ts[i]->pl_idx = 0;	//reset请求都是每个plane都需要执行
			req_plus->flash_ts[i]->ops_num = spp->pls_per_lun;	/* 每个zone每次只会映射单个Die中等于其分组数量的块 */

			InsertInTail(&zns_ssd->list[req_plus->flash_ts[i]->die_idx], req_plus->flash_ts[i]);
		}

		for (i = 0; i < phy_zone->map_die_group_num; i++) {		//更新元数据	
			die_group = &dymap_p->die_group[phy_zone->die_groups[i]];
			die_group->fbgs_used_bitmap[phy_zone->fbg_ofst]--;
			die_group->remain_fbgs_num++;
		}
		
		return 1;
	}	else {		//不需要实际的擦除，直接提交任务
		for (i = 0; i < phy_zone->map_die_group_num; i++) {		
			die_group = &dymap_p->die_group[phy_zone->die_groups[i]];
			die_group->fbgs_used_bitmap[phy_zone->fbg_ofst]--;
		}

		return 0;
	}

	//不要重置zone的信息，在下次open时自己会重置
	
}

#if PARA_ZNS 	//DIe级并行性+将一个die中所有plane中相同偏移地址的块组成为fbg
/* 根据逻辑地址以及映射策略来索引其物理地址 */
static void get_ppa_by_lba_base(zns_ssd_t *zns_ssd, uint32_t zone_idx, uint64_t wp, struct ppa *ppa)
{	
	struct ssdparams *spp = &zns_ssd->sp;
	uint32_t lbas_per_pg = spp->secs_per_pg;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];
	
	uint32_t page_ofst_in_zone = 0, die_ofst_in_zone = 0, page_ofst_in_dg = 0;
	uint32_t dg_ofst_in_zone = 0, die_ofst_in_dg = 0;
	uint32_t pl_ofst_in_die = 0, pg_ofst_in_pl = 0;
	uint32_t pgs_per_fbg_part = 0, pg_ofst_st_in_blk = 0;

	uint32_t die_id = 0, chan_ofst_in_ssd = 0, chip_ofst_in_chan = 0;
	uint32_t pl_ofst_in_chip = 0, block_ofst_in_pl = 0, pg_ofst_in_block = 0;

	assert(phy_zone->map_die_group_num > 0);
	page_ofst_in_zone = wp / lbas_per_pg;
	die_ofst_in_zone = page_ofst_in_zone % (phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA);
	page_ofst_in_dg = page_ofst_in_zone / (phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA);

	dg_ofst_in_zone = die_ofst_in_zone / ZONE_MAX_DIE_PARA;
	die_ofst_in_dg = die_ofst_in_zone % ZONE_MAX_DIE_PARA;
	assert(dg_ofst_in_zone <= phy_zone->map_die_group_num);
	die_id = dymap_p->die_group[phy_zone->die_groups[dg_ofst_in_zone]].die[die_ofst_in_dg];	//查看映射表
	assert(die_id < spp->tt_luns);

	pl_ofst_in_die = page_ofst_in_dg % spp->pls_per_lun;	//die中的plane也是并行写
	pg_ofst_in_pl = page_ofst_in_dg / spp->pls_per_lun;		//由于一个zone最多映射一个plane中的一个block		

	if (phy_zone->map_die_group_num > 1) { //部分映射，block中的页起始偏移不一定是从0开始
		pgs_per_fbg_part = spp->pgs_per_blk / phy_zone->map_die_group_num;
		pg_ofst_st_in_blk = phy_zone->fbg_part * pgs_per_fbg_part;
	}

	//转为所有地址为相对地址, 除了并行写单元，其余都是遵循相邻原则(Channel与Die,Plane和block)
	chan_ofst_in_ssd = die_id / spp->luns_per_ch;	
	chip_ofst_in_chan = die_id % spp->luns_per_ch;
	pl_ofst_in_chip = pl_ofst_in_die;
	block_ofst_in_pl = phy_zone->fbg_ofst;
	pg_ofst_in_block = pg_ofst_in_pl + pg_ofst_st_in_blk;
	assert(pg_ofst_in_block < spp->pgs_per_blk);

	ppa->g.ch = chan_ofst_in_ssd;		//最终需要的五个参数
	ppa->g.lun = chip_ofst_in_chan;
	ppa->g.pl = pl_ofst_in_chip;
	ppa->g.blk = block_ofst_in_pl;
	ppa->g.pg = pg_ofst_in_block;
	ppa->g.sec = 0;

//	femu_debug("zone=%u, wp=%lu, ch=%u, lun=%u, pl=%u, blk=%u, pg=%u\n", 
//		zone_idx, wp, ppa->g.ch, ppa->g.lun, ppa->g.pl, ppa->g.blk, ppa->g.pg);
}

#else

/* 根据逻辑地址以及映射策略来索引其物理地址
 * 为了不改动其他的函数，仅改动FBG的组成
 * FBG仍然由两个块组成，但是由同一个Pl中连续的两个块组成，从而FBG不再能并行操作
 * DIE中FBG的编号为Pl0和Pl1交叉编号，从而按顺序的FBG ID映射时可以实现Pl的轮询映射
 * 基于此FBG构成，FBG内部为一个Block一个一个Block地写满，串行
 * 由于FBG构成改变，因此索引方式改变
 */
static void get_ppa_by_lba_base(zns_ssd_t *zns_ssd, uint32_t zone_idx, uint64_t wp, struct ppa *ppa)
{	
	struct ssdparams *spp = &zns_ssd->sp;
	uint32_t lbas_per_pg = spp->secs_per_pg;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];
	
	uint32_t page_ofst_in_zone = 0, die_ofst_in_zone = 0, page_ofst_in_die = 0;
	uint32_t dg_ofst_in_zone = 0, die_ofst_in_dg = 0;
	uint32_t pl_ofst_in_die = 0, blk_ofst_in_fbg = 0, fbg_ofst_in_pl = 0;

	uint32_t die_id = 0, chan_ofst_in_ssd = 0, chip_ofst_in_chan = 0;
	uint32_t pl_ofst_in_chip = 0, block_ofst_in_pl = 0, pg_ofst_in_block = 0;

	assert(phy_zone->map_die_group_num > 0);
	page_ofst_in_zone = wp / lbas_per_pg;
	die_ofst_in_zone = page_ofst_in_zone % (phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA);
	page_ofst_in_die = page_ofst_in_zone / (phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA);

	dg_ofst_in_zone = die_ofst_in_zone / ZONE_MAX_DIE_PARA;
	die_ofst_in_dg = die_ofst_in_zone % ZONE_MAX_DIE_PARA;
	assert(dg_ofst_in_zone <= phy_zone->map_die_group_num);
	die_id = dymap_p->die_group[phy_zone->die_groups[dg_ofst_in_zone]].die[die_ofst_in_dg];	//查看映射表
	assert(die_id < spp->tt_luns);

	blk_ofst_in_fbg = page_ofst_in_die / spp->pgs_per_blk;
	pg_ofst_in_block = page_ofst_in_die % spp->pgs_per_blk;

	assert(phy_zone->map_die_group_num == 1);	//该模式下不能部分映射，open zone=2和4都采用=4测试

	pl_ofst_in_die = phy_zone->fbg_ofst % spp->pls_per_lun;
	fbg_ofst_in_pl = phy_zone->fbg_ofst / spp->pls_per_lun;

	block_ofst_in_pl = fbg_ofst_in_pl * spp->pls_per_lun + blk_ofst_in_fbg;
	
	//转为所有地址为相对地址, 除了并行写单元，其余都是遵循相邻原则(Channel与Die,Plane和block)
	chan_ofst_in_ssd = die_id / spp->luns_per_ch;	
	chip_ofst_in_chan = die_id % spp->luns_per_ch;
	pl_ofst_in_chip = pl_ofst_in_die;
	assert(pl_ofst_in_chip < spp->pls_per_lun);
//	block_ofst_in_pl;
//	pg_ofst_in_block;
	assert(block_ofst_in_pl < spp->blks_per_pl);
	assert(pg_ofst_in_block < spp->pgs_per_blk);

	ppa->g.ch = chan_ofst_in_ssd;		//最终需要的五个参数
	ppa->g.lun = chip_ofst_in_chan;
	ppa->g.pl = pl_ofst_in_chip;
	ppa->g.blk = block_ofst_in_pl;
	ppa->g.pg = pg_ofst_in_block;
	ppa->g.sec = 0;

//	femu_debug("zone=%u, wp=%lu, ch=%u, lun=%u, pl=%u, blk=%u, pg=%u\n", 
//		zone_idx, wp, ppa->g.ch, ppa->g.lun, ppa->g.pl, ppa->g.blk, ppa->g.pg);
}

#endif

/* 根据映射关系分发读写请求到相应的队列
 * type：0，写；1，读；
 */
void zone_rw(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx, uint64_t wp)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];
	struct ssdparams *spp = &zns_ssd->sp;
	uint32_t lbasz = 1 << zns_ns_lbads(&n->namespaces[0]);	//512B
	uint32_t tt_lba_page = (spp->secs_per_pg * spp->secsz) / lbasz;

	NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
	struct ppa ppa;

	uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb) + 1;
	uint32_t page_num = 0, new_wp = 0, para_num_in_zone = 0;

	uint32_t i = 0, temp = 0;

	//对于未对齐的wp，说明其所在物理页已经被写入（采取事先写入一页的做法），则不再写入该页
	//读同理
	if (wp % tt_lba_page > 0) {	
		new_wp = (wp / tt_lba_page + 1) * tt_lba_page;	//进一个物理页，使得wp对齐
		if (nlb <= new_wp - wp) {	//写入没有超过这一页，不需要生成请求
			return;
		} else {
			nlb -= new_wp - wp;		//nlb也需要少写一些
		}
	} else {
		new_wp = wp;
	}
	page_num = (nlb-1) / tt_lba_page + 1;
//	femu_debug("Req: RW, Zone=%u, wp=%lu, new wp=%u, nlb=%u, pg_num=%u\n", zone_idx, wp, new_wp, nlb, page_num);

	NvmeRequestPlus_t *req_plus = g_malloc(sizeof(NvmeRequestPlus_t));		/* 将每一个req封装为req plus */
	req_plus->req = req;
	req_plus->maxlat = 0;

	if (req->cmd.opcode == NVME_CMD_WRITE) {
		req_plus->cmd_type = NAND_WRITE;
//		assert(phy_zone->map_die_group_num > 0);
	} else if (req->cmd.opcode == NVME_CMD_READ) {
		req_plus->cmd_type = NAND_READ;
	} else
		assert(0);

	//分发时仅考虑Die级并行性，处理时再考虑Plane级并行性
	para_num_in_zone = phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA;
	req_plus->ts_num = (page_num > para_num_in_zone) ? para_num_in_zone : page_num;
	req_plus->ts_sum = req_plus->ts_num;
	req_plus->flash_ts = (flash_ts_t **)g_malloc(sizeof(flash_ts_t *) * req_plus->ts_sum);		/* 也要回收 */
	
	for (i = 0; i < req_plus->ts_sum; i++) {
		req_plus->flash_ts[i] =(flash_ts_t *)g_malloc(sizeof(flash_ts_t));	/* 要分开申请，不然释放时会出错，注意释放空间 */
		memset(req_plus->flash_ts[i], 0, sizeof(flash_ts_t));
		req_plus->flash_ts[i]->pg_idx = 0xff;		//0xff为一个初始值
	}

	for (i = new_wp, temp = 0; i < new_wp + nlb; i += tt_lba_page) {	//按page遍历
		get_ppa_by_lba_base(zns_ssd, zone_idx, i, &ppa);
		assert(req_plus->flash_ts[temp]);
		req_plus->flash_ts[temp]->die_idx = ppa.g.ch * spp->luns_per_ch + ppa.g.lun; //转换为绝对die地址
		assert(req_plus->flash_ts[temp]->die_idx < spp->tt_luns);

#if PARA_ZNS
		if (req_plus->flash_ts[temp]->pl_idx == 0xff) {
			req_plus->flash_ts[temp]->pl_idx = ppa.g.pl;	//多页时仅携带起始Plane
			assert(req_plus->flash_ts[temp]->pl_idx < spp->pls_per_lun);
		}
#else
		if (req_plus->flash_ts[temp]->pg_idx == 0xff) {	//仅携带初值
			req_plus->flash_ts[temp]->pl_idx = ppa.g.pl;
			req_plus->flash_ts[temp]->blk_idx = ppa.g.blk;
			req_plus->flash_ts[temp]->pg_idx = ppa.g.pg;
		}
#endif

		req_plus->flash_ts[temp]->ops_num++;	//每个事务读写的页数, 这些页可能属于不同的Plane，但只属于一个zone

		if (++temp == req_plus->ts_num)
			temp = 0;
	}

	for (i = 0; i < req_plus->ts_sum; i++) {
		req_plus->flash_ts[i]->req_plus = req_plus;		//反向索引
		req_plus->flash_ts[i]->zone_idx = zone_idx;

		InsertInTail(&zns_ssd->list[req_plus->flash_ts[i]->die_idx], req_plus->flash_ts[i]);
		
//		femu_debug("Transaction[%d], DIE(%u), ops num(%u)\n", i, req_plus->flash_ts[i]->die_idx, req_plus->flash_ts[i]->ops_num);
	}

	
}





/* 处理上层发送的所有ZNS命令，包括open/close, reset, write/read等
 * 其中reset, write/read需要延时处理，需要放入队列
 * open/close为处理映射关系，不需要延时处理，响应后直接提交
 * 统计读写请求的个数，判断是否为启动阶段
 */
void zns_req_distribute(NvmeRequest *req, FemuCtrl *n, int index_poller) 
{
	NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
	uint64_t slba = le64_to_cpu(rw->slba);
	uint32_t zone_idx = zns_zone_idx(n, slba);	
	assert(zone_idx < n->num_zones);
	NvmeZone *zone = &n->zone_array[zone_idx];						/* 逻辑zone */
	uint64_t wp = slba - zone->d.zslba;
	phy_zone_t *phy_zone = &n->zns_ssd->dymap->phy_zone[zone_idx];	/* 物理zone */
	static uint32_t req_sum = 0;

	uint32_t ret = 0;

	if (req->cmd.opcode != NVME_CMD_WRITE && req->cmd.opcode != NVME_CMD_READ) {	/* zone_mgmt命令 */
		assert(wp == 0);	//管理命令都是以zone为单位，wp和nlb都是0

		uint32_t dw13 = le32_to_cpu(req->cmd.cdw13);
		uint8_t action = dw13 & 0xff;
		if (action != NVME_ZONE_ACTION_RESET) {	//opens/close命令
			if (action == NVME_ZONE_ACTION_OPEN) {
				zone_open(n, req, zone_idx);
			} else if (action == NVME_ZONE_ACTION_CLOSE) {
				zone_close(n, req, zone_idx);
			}

			//直接提交该请求到CQ
			req->reqlat = 0;
			req->expire_time += 0;
			if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1) {
				femu_debug("ZNS to_poller enqueue failed\n");
		    }
		} else {		//reset命令
			ret = zone_reset(n, req, zone_idx);
			if (ret == 0) {	//未发布实际的擦除请求，直接提交任务
				req->reqlat = 0;
				req->expire_time += 0;
				if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1) {
					femu_debug("ZNS to_poller enqueue failed\n");
			    }
			}
		}
	} else {  //读写命令
		//通过读写请求的数量(经测试发现:启动阶段会发出148个读写请求)来判断是否通过femu的启动阶段
		if (n->zns_ssd->start_up_flag == 0 && ++req_sum > 150) {	
			n->zns_ssd->start_up_flag = 1;
		}
//		femu_debug("req_sum = %u, type=%x\n", req_sum, req->cmd.opcode);

		if (n->zns_ssd->start_up_flag) {
			//在open zone之前有一些读请求，执行直接提交
			if (req->cmd.opcode == NVME_CMD_READ && phy_zone->map_die_group_num == 0) {
				req->reqlat = 0;
				req->expire_time += 0;
				if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1)
					femu_debug("ZNS to_poller enqueue failed\n");
			} else {
				zone_rw(n, req, zone_idx, wp);
			}
		} else {	//位于启动阶段的请求直接提交
			req->reqlat = 0;
			req->expire_time += 0;
			if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1)
				femu_debug("ZNS to_poller enqueue failed\n");
		}
	}
}

//重置所有open信息，以便执行下一个任务
void reset_open_infos(zns_dynamic_mapping_t *dymap_p)
{
	dymap_p->st_stage_flag = 1;
	
	//根据当前任务open zone数量，决定下一个任务的open zone数量，注意发任务的顺序必须和这保持一致
	if (dymap_p->opened_zones_num == 4) {
		dymap_p->last_opened_zones_num = dymap_p->opened_zones_num;
		dymap_p->open_zone_limit = 2;
	} else if (dymap_p->opened_zones_num == 2 || dymap_p->opened_zones_num == 1) {
		dymap_p->last_opened_zones_num = dymap_p->opened_zones_num;
		dymap_p->open_zone_limit = 8;
	} else if (dymap_p->opened_zones_num == 8) {
		dymap_p->last_opened_zones_num = dymap_p->opened_zones_num;
		dymap_p->open_zone_limit = 4;
	} else if (dymap_p->opened_zones_num == 0) {	//说明已经被重置过
		;
	} else 
		assert(0);		//出错

	//不清空已open zone的信息，因为其已被映射不可改变
	dymap_p->opened_zones_num = 0;
}

/* 轮询所有的DIE Queue，取出里面的事务并执行，执行时利用plane级并行性 */
void zns_process_die_queue(FemuCtrl *n, int index_poller)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	List_t *list = zns_ssd->list;
	struct ssdparams *spp = &zns_ssd->sp;
	struct ppa ppa;
	struct nand_lun *lun = NULL;
	flash_ts_t *transaction_p = NULL;
	uint64_t now_time;
	uint64_t curlat = 0;
	int32_t i;

	static uint64_t last_time = 0;	//上次取出请求的时间，用于打印
#if PARA_ZNS
	uint32_t pl_ops = 0;	//执行并行plane操作的次数
#else
	Node_t *p = NULL;
	Node_t *temp = NULL;
	uint32_t cur_type = 0; //0,代表不同plane；1，相同pl但不同地类；2，可以pl并行；
#endif

	for (i = 0; i < spp->tt_luns; i++) {
		ppa.g.ch = i % spp->nchs;
		ppa.g.lun = i / spp->nchs;
		lun = get_lun(zns_ssd, &ppa);

		now_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
		if (lun->next_lun_avail_time <= now_time) {					/* LUN空闲 */
			if (DeleteInHead(&list[i], &transaction_p) < 0) {		/* 队列为空 */
				if (now_time-last_time >= 3000000000 && last_time != 0) {	//空闲时间 >5s 才打印,同时将其视作为一个任务结束
					femu_debug("ops_num=%lu, pl_sum=%lu\n", zns_ssd->ops_sum, zns_ssd->pl_sum);
					femu_debug("sm_pl_sz=%lu, df_addr_sz=%lu, pl_sz=%lu\n", zns_ssd->sm_pl_sz, zns_ssd->df_addr_type_sz, zns_ssd->pl_sz);
					femu_debug("dg0_time=%lu, dg1_time=%lu, dg2_time=%lu, dg3_time=%lu, sum_serv_time_us=%lu\n",\
							zns_ssd->dg_time_us[0], zns_ssd->dg_time_us[1], zns_ssd->dg_time_us[2], zns_ssd->dg_time_us[3], 
							zns_ssd->sum_serv_time_us);	//TODO：写入文件
					last_time = now_time;

//					reset_open_infos(zns_ssd->dymap);	//重置所有open信息，以便执行下一个任务
				}
					
				continue;
			} 
			last_time = now_time;

			assert(transaction_p);
			assert(transaction_p->die_idx == i);
#if PARA_ZNS
			//按终止plane计算需要执行几次并行plane操作, 并不涉及具体页地址判断
			//TODO:进一步统计对于同一个zone事务间的并行plane
			pl_ops = (transaction_p->pl_idx + transaction_p->ops_num - 1) / spp->pls_per_lun + 1;
			switch (transaction_p->req_plus->cmd_type) {	//以并行plane操作次数计算延迟curlat
				case NAND_WRITE:
					lun->next_lun_avail_time = now_time + pl_ops * spp->pg_wr_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += pl_ops * spp->pg_wr_lat / 1000;					//统计总的芯片服务时间, 用us来忽略代码执行延迟
					zns_ssd->dg_time_us[i/ZONE_MAX_DIE_PARA] += pl_ops * spp->pg_wr_lat / 1000;		//单个die组的芯片服务时间
					break;
				case NAND_READ:
					lun->next_lun_avail_time = now_time + pl_ops * spp->pg_rd_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += pl_ops * spp->pg_rd_lat / 1000;
					zns_ssd->dg_time_us[i/ZONE_MAX_DIE_PARA] += pl_ops * spp->pg_rd_lat / 1000;	
					break;
				case NAND_ERASE:
					lun->next_lun_avail_time = now_time + pl_ops * spp->blk_er_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += pl_ops * spp->blk_er_lat / 1000;
					zns_ssd->dg_time_us[i/ZONE_MAX_DIE_PARA] += pl_ops * spp->blk_er_lat / 1000;	
					break;
				default:
					femu_debug("Unsupported NAND command: 0x%x\n", transaction_p->req_plus->cmd_type);
			}	

//			if (curlat > 50000000) {	//异常
//				femu_debug("Die[%d], queue len = %d, zone %u\n", i, list[i].len, transaction_p->zone_idx);
//			}

			if (transaction_p->req_plus->cmd_type != NAND_ERASE) {	//仅统计读写操作
				zns_ssd->pl_sz += transaction_p->ops_num;
				zns_ssd->pl_sum += transaction_p->ops_num;			//每个事务都会使用一个plane
				zns_ssd->ops_sum += pl_ops;
			}

			release_ts(n, index_poller, transaction_p, curlat);
#else
			switch (transaction_p->req_plus->cmd_type) {	//先计算延迟curlat
				case NAND_WRITE:
					lun->next_lun_avail_time = now_time + transaction_p->ops_num * spp->pg_wr_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += transaction_p->ops_num * spp->pg_wr_lat / 1000;	//统计总的芯片服务时间, 用us来忽略代码执行延迟
					break;
				case NAND_READ:
					lun->next_lun_avail_time = now_time + transaction_p->ops_num * spp->pg_rd_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += transaction_p->ops_num * spp->pg_rd_lat / 1000;
					break;
				case NAND_ERASE:
					lun->next_lun_avail_time = now_time + transaction_p->ops_num * spp->blk_er_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += transaction_p->ops_num * spp->blk_er_lat / 1000;
					break;
				default:
					femu_debug("Unsupported NAND command: 0x%x\n", transaction_p->req_plus->cmd_type);
			}	

			//遍历地判断是否能执行plane级并行，仅读写
			if (transaction_p->req_plus->cmd_type != NAND_ERASE) {	
				p = list[i].head->next;
				while (p) {
					if (p->data->pl_idx != transaction_p->pl_idx) {	//队列中有其他plane的事务
						//地址类型都相同，取出该事务并行执行，不会影响zone内顺序写，因为一个zone在一个die只存在于某一个plane，不可能并行
						if (p->data->req_plus->cmd_type == transaction_p->req_plus->cmd_type &&
							p->data->blk_idx == transaction_p->blk_idx &&
							p->data->pg_idx == transaction_p->pg_idx) {	//TODO：仅判断了事务的首地址
							p->prev->next = p->next;		//从队列中取出该事务
							if (p->next)
								p->next->prev = p->prev;
							else	//p为尾节点
								list[i].tail = p->prev;
							list[i].len--;

							zns_ssd->pl_sz += p->data->ops_num;		//统计
							zns_ssd->ops_sum +=  p->data->ops_num;
							cur_type = 2;
							
							release_ts(n, index_poller, p->data, curlat);	//执行，注意会释放事务p->data的空间

							temp = p->next;
							g_free(p);
							p = temp;
							continue;
						} else {
							if (cur_type != 2) //遍历时不要改变之前的类型
								cur_type = 1;
						}
					} 

					p = p->next;
				}

				if (cur_type == 2) {
					zns_ssd->pl_sz += transaction_p->ops_num;
					zns_ssd->pl_sum += transaction_p->ops_num * 2;		//每个操作都需要用一次plane
				} else if (cur_type == 1) {
					zns_ssd->df_addr_type_sz += transaction_p->ops_num;
					zns_ssd->pl_sum += transaction_p->ops_num;			
				} else {
					zns_ssd->sm_pl_sz += transaction_p->ops_num;
					zns_ssd->pl_sum += transaction_p->ops_num;
				}

				zns_ssd->ops_sum += transaction_p->ops_num;
					
			}

			release_ts(n, index_poller, transaction_p, curlat);
#endif
		}

	}
	
}



void zns_dynamic_mapping_init(FemuCtrl *n)
{
	n->zns_ssd->dymap = (zns_dynamic_mapping_t *)g_malloc0(sizeof(zns_dynamic_mapping_t));
	zns_dynamic_mapping_t *dymap_p = n->zns_ssd->dymap;
	struct ssdparams *spp = &n->zns_ssd->sp;
	uint32_t i = 0, j = 0;

	dymap_p->fbgs_per_dg = spp->blks_per_pl;	//现在FBG的数量为单个分组中块的数量

	dymap_p->die_group_sum = spp->tt_luns / ZONE_MAX_DIE_PARA;
	dymap_p->die_group = (die_group_t *)g_malloc0(sizeof(die_group_t) * dymap_p->die_group_sum);
	dymap_p->die_group_used_bitmap = (uint32_t *)g_malloc0(sizeof(uint32_t) * dymap_p->die_group_sum);
	for (i = 0; i < dymap_p->die_group_sum; i++) {
		dymap_p->die_group_used_bitmap[i] = 0;

		dymap_p->die_group[i].die = (uint32_t *)g_malloc0(sizeof(uint32_t) * ZONE_MAX_DIE_PARA);
		for (j = 0; j < ZONE_MAX_DIE_PARA; j++) {
			dymap_p->die_group[i].die[j] = i * ZONE_MAX_DIE_PARA + j;		//DIE ID相邻的为一组die group
		}

		dymap_p->die_group[i].fbgs_used_bitmap = (uint32_t *)g_malloc0(sizeof(uint32_t) * dymap_p->fbgs_per_dg);
		memset(dymap_p->die_group[i].fbgs_used_bitmap, 0, sizeof(uint32_t) * dymap_p->fbgs_per_dg);
		dymap_p->die_group[i].fbgs_parts_sum = (uint32_t *)g_malloc0(sizeof(uint32_t) * dymap_p->fbgs_per_dg);
		memset(dymap_p->die_group[i].fbgs_parts_sum, 1, sizeof(uint32_t) * dymap_p->fbgs_per_dg);

		dymap_p->die_group[i].remain_fbgs_num = dymap_p->fbgs_per_dg;
		dymap_p->die_group[i].next_fbg_id = 0;

		dymap_p->die_group[i].part_map_flag = 0;
	}

	dymap_p->phy_zone = (phy_zone_t *)g_malloc0(sizeof(phy_zone_t) * n->num_zones);
	for (i = 0; i < n->num_zones; i++) {
		dymap_p->phy_zone[i].map_die_group_num = 0;
		dymap_p->phy_zone[i].fbg_part = 0;
		memset(dymap_p->phy_zone[i].die_groups, 0, sizeof(uint32_t) * 8);
	}

	dymap_p->opened_zones_num = 0;
	dymap_p->open_zone_limit = INITIAL_OPEN_LIMIT;

	dymap_p->st_stage_flag = 1;
	dymap_p->last_opened_zones_num = 0;

}

void zns_ftl_init(FemuCtrl *n)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	struct ssdparams *spp = &zns_ssd->sp;
	int32_t i;
		

	/* 给每个DIE初始化一个DIE QUEUE，用来保存事务 */
	zns_ssd->list = (List_t *)g_malloc((size_t)spp->tt_luns * sizeof(List_t));
	for (i = 0; i < spp->tt_luns; i++) {
		InitList(&zns_ssd->list[i]);
	}
	
	femu_debug("DIE Queue init successfully!\n");

	zns_dynamic_mapping_init(n);
	femu_debug("Zone mapping init successfully!\n");
}



