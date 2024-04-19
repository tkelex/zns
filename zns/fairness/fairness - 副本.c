/*
 * @brief: Implements ZNS
 fairness scheme in it.
 * @date: 2023.05.15
 * @author: tzh
 */

#include "fairness.h"


/* �������ʵ�֣����䵱������ʹ�� */
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
//	if (list->len == 0)		/* ������ */
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
		if (p->next) { /* P����β��� */
			p->next->prev = list->head;
		} else {	/* P��β��㣬����βָ�� */
			list->tail = list->head;
		}
		*data = p->data;
		list->len--;
		g_free(p);

		return 0;
	} 

	return -1;
}


/* ZNS SSD �ڲ����� */
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
	
//	ts->req_plus->maxlat = (curlat > maxlat) ? curlat : maxlat;	//TODO������Ҫ���������ִ���ӳپ������һ������ִ�е��ӳ�
//	femu_debug("clat=%lu, mlat=%lu, rmlat=%lu\n", curlat, maxlat, ts->req_plus->maxlat);

	ts->req_plus->maxlat = curlat;	//����ִ���ӳپ������һ������ִ�е��ӳ�

	if (--ts->req_plus->ts_num == 0) {	/* �ô�req������������ɣ������ύ��CQ��ȥ */
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

	for (j = 0; j < ts->req_plus->ts_sum; j++) {	/* �ͷŸ�ts��ռ�õĿռ� */
		if (ts->req_plus->flash_ts[j] == ts) {
			ts->req_plus->flash_ts[j] = NULL;
			g_free(ts);
			break;
		}
	}
	
}



/* ��ָ����die���ڲ����Һ��ʵ�FBG
 * FBGΪһ��die������plane��ͬƫ�Ƶ�ַ��һ��block
 * ԭ�򣺸���fbg������ӳ�仹�ǲ���ӳ��ȥ��ѯ��ӳ��
 * ����ӳ��ʱ��1.��֤������ʹ�õ�fbg���ȱ�����; 2.����die���ڵ�fbgƫ����ͬ
 * fbg_type: ����Ѱ�ҵ�Ŀ��FBGΪ������
 * Ŀǰ��֧������fbg_type���棬1��2��1��4����1����2����4
 */
void alloc_fbg(zns_dynamic_mapping_t *dymap_p, uint32_t fbg_type, uint32_t dg_idx, uint32_t *fbg_ofst, uint32_t *fbg_part)
{
	uint32_t i = 0, max_next_fbg = 0;
	die_group_t *die_group = &dymap_p->die_group[dg_idx];

	if (fbg_type == 1) { //����ӳ�䣬ֱ��ӳ�䵱ǰdie���next_fbg
		assert(die_group && die_group->remain_fbgs_num > 0);	
		while (die_group->fbgs_used_bitmap[die_group->next_fbg_id] > 0) {	//Note��die_group->remain_fbgs_num����׼ȷ����Ȼ�ͻ���ѭ��
			die_group->next_fbg_id++;
			if (die_group->next_fbg_id == dymap_p->fbgs_per_dg) {
				die_group->next_fbg_id = 0;
			}
		}

		//TODO:�����һ��������Ƿ���Ŀ���(������)
		die_group->remain_fbgs_num--;
		die_group->fbgs_used_bitmap[die_group->next_fbg_id]++;
		*fbg_ofst = die_group->next_fbg_id;
		
		if (++die_group->next_fbg_id == dymap_p->fbgs_per_dg) 
			die_group->next_fbg_id = 0;
		assert(die_group->next_fbg_id < dymap_p->fbgs_per_dg);
	} else {	//����ӳ�䣬���Ǵ���һ���н�С��dg_idx, ��֧������fbg_type����
		if (die_group->part_map_flag == 1) { //���ڲ���ӳ���fbg, ֱ��ʹ��
			assert(die_group->fbg_part_map.fbg_part_sum == fbg_type 
					&& die_group->fbg_part_map.next_fbg_part > 0);
//			assert(die_group->fbgs_used_bitmap[die_group->fbg_part_map.fbg_id] > 0); //��Щ����ӳ��zone�ᱻreset
			*fbg_ofst = die_group->fbg_part_map.fbg_id;
			*fbg_part = die_group->fbg_part_map.next_fbg_part;

			for (i = dg_idx; i < dg_idx + fbg_type; i++) { 	//����die�����Ϣ����Ҫ����
				die_group_t *temp_dg = &dymap_p->die_group[i];
				temp_dg->fbgs_used_bitmap[*fbg_ofst]++;
			}
			
			if (++die_group->fbg_part_map.next_fbg_part == fbg_type) {	//��fbg������
				die_group->part_map_flag = 0;
			}
		} else { //�����ڲ���ӳ�䣬��next_fbg����ȡ			
			for (i = dg_idx; i < dg_idx + fbg_type; i++) {	//Ϊ��fbgƫ��ͳһ��ʹ�ü���die���������Ǹ�next
				assert(i < dymap_p->die_group_sum);
				die_group_t *temp_dg = &dymap_p->die_group[i];
				if (temp_dg->next_fbg_id > max_next_fbg) {
					max_next_fbg = temp_dg->next_fbg_id;	
//					max_idx = i;
				}
			}
			
			femu_debug("max_fbg_ofst = %u\n", max_next_fbg); 

			*fbg_ofst = max_next_fbg;	//����die�鶼�����fbg
			*fbg_part = 0;				//��һ��ʹ�ø�fbg
			for (i = dg_idx; i < dg_idx + fbg_type; i++) { 	//����die�����Ϣ����Ҫ����
				die_group_t *temp_dg = &dymap_p->die_group[i];
				temp_dg->fbgs_used_bitmap[*fbg_ofst]++;		//����λͼ��reset��Ҫ
			
				if (++temp_dg->next_fbg_id == dymap_p->fbgs_per_dg)	//���е�die�鶼�Ӹ�λ�ÿ�ʼ��ѯ�����Ա�������
					temp_dg->next_fbg_id = 0;
				assert(temp_dg->next_fbg_id < dymap_p->fbgs_per_dg);

				temp_dg->remain_fbgs_num--;	//���ַ���Ҳ�����
			}
			
			die_group->part_map_flag = 1;	//ֻ�ǰ���die��Ϳ��ԣ���Ϊ���die��ʱ��ѭ��һ��die�����Ϣ
			die_group->fbg_part_map.fbg_id = max_next_fbg;
			die_group->fbg_part_map.fbg_part_sum = fbg_type;
			die_group->fbg_part_map.next_fbg_part = 1;
		}
	}
}


/* ��һ��zone
 * 1.��¼��zone������������open zone limit
 * 2.��������ӳ����Դ
 * 3.��¼ӳ������Դ
 * TODO: ʵ�ָ�֪open zone�����Ķ�̬�仯
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
	uint32_t map_zone_num = 0;	//ӳ��ʱ��֤����zoneռ��ȫ����Դ

	for (i = 0; i < dymap_p->last_opened_zones_num; i++) {
		if (dymap_p->opened_zones[i] == zone_idx) {	//�ѱ��ϴ������
			return;
		}
	}

	dymap_p->opened_zones[dymap_p->opened_zones_num] = zone_idx;
	dymap_p->opened_zones_num++;

	if (dymap_p->st_stage_flag) { //λ�ڳ�ʼ�׶�
		assert(dymap_p->opened_zones_num <= dymap_p->open_zone_limit);
		map_zone_num = dymap_p->open_zone_limit;
		if (dymap_p->opened_zones_num == dymap_p->open_zone_limit) {
			dymap_p->st_stage_flag = 0;
		}
	} else {			//���ں����׶�, Ϊ�˷���ֻʵ����Ŀǰ�õļ�������
		if (dymap_p->opened_zones_num <= 2)
			map_zone_num = dymap_p->opened_zones_num;	//1 or 2
		else
			map_zone_num = dymap_p->die_group_sum;		//=4		
	}
	
	femu_debug("Opened zone %u, open sum = %u, open limit = %u, map zone num = %u\n", 
			zone_idx, dymap_p->opened_zones_num, dymap_p->open_zone_limit, map_zone_num);

	if (map_zone_num < dymap_p->die_group_sum) {	//����ӳ�䣬ÿ��zoneӳ����die������ͬƫ�Ƶ�FBG
		phy_zone->map_die_group_num = dymap_p->die_group_sum / dymap_p->open_zone_limit; 	//Ŀǰ��֧�ֶ��������
		for (i = 0; i < dymap_p->die_group_sum; i += phy_zone->map_die_group_num) {			//ֻ��ǰ�����һ��
			if (dymap_p->die_group_used_bitmap[i] < min_used_num) {
				min_used_num = dymap_p->die_group_used_bitmap[i];
				min_idx = i;
			}
		}

		//Todo: �ж��Ƿ��п���FBG
		for (i = 0; i < phy_zone->map_die_group_num; i++) {
			femu_debug("min_idx=%u, used num=%u\n", min_idx+i, min_used_num);
			phy_zone->die_groups[i] = min_idx + i;
			dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]]++;
		}

		//��һ����ѯ��ӳ������FBG
		alloc_fbg(dymap_p, phy_zone->map_die_group_num, phy_zone->die_groups[0], &fbg_ofst, &fbg_part);
		phy_zone->fbg_ofst = fbg_ofst;
		phy_zone->fbg_part = fbg_part;
		
	} else {	//����ӳ�䣬ÿ��zoneӳ��һ��die��
		phy_zone->map_die_group_num = 1;
		//���ҳ�����ʵ�die�飬����ʹ��λͼ���ھ��п���fbg��die�����ҳ���С��λͼ
		for (i = 0; i < dymap_p->die_group_sum; i++) {	//���ҳ����п���fbg��die��
			if (dymap_p->die_group[i].remain_fbgs_num > 0) {
				avail_dg[avail_dg_len++] = i;
			}
		}

//		femu_debug("avail_dg_len = %u\n", avail_dg_len);
		for (i = 0; i < avail_dg_len; i++) {	//������Щdie����Ѱ����Сλͼ
			if (dymap_p->die_group_used_bitmap[avail_dg[i]] < min_used_num) {
				min_used_num = dymap_p->die_group_used_bitmap[avail_dg[i]];
				min_idx = avail_dg[i];
			}
		}
		
		//ӳ�䣬����λͼ
		femu_debug("min_idx=%u, used num=%u\n", min_idx, min_used_num);
	    assert(min_used_num != 100);	//=100˵��û�п��е�FBG��
		
		phy_zone->die_groups[0] = min_idx;
		dymap_p->die_group_used_bitmap[min_idx]++;

		//��һ����ѯ��ӳ������FBG
		alloc_fbg(dymap_p, 1, phy_zone->die_groups[0], &fbg_ofst, NULL);
		phy_zone->fbg_ofst = fbg_ofst;
		phy_zone->fbg_part = 0;
	}
}

/* �ر�һ��zone��������������Դ */
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

	if (open_flag == 1) {	//����open״̬�����ͷ���Դ
		for (i = 0; i < phy_zone->map_die_group_num; i++) {	//�ͷ����zone��ռ�ݵĲ�����Դ
//			femu_debug("bitmap = %u\n", dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]]);
			assert(dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]] > 0);
			dymap_p->die_group_used_bitmap[phy_zone->die_groups[i]]--;	
		}

		//����zone�Ƴ�open zones����������open zone����д��ȥ-����
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

/* �����Ƿ񷢲���ʵ�ʵĲ���req����(1)����(0) */
uint32_t zone_reset(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx)
{
	struct zns_ssd *zns_ssd = n->zns_ssd;
	zns_dynamic_mapping_t *dymap_p = zns_ssd->dymap;
	phy_zone_t *phy_zone = &dymap_p->phy_zone[zone_idx];
	struct ssdparams *spp = &zns_ssd->sp;
	die_group_t *die_group = NULL;
	uint32_t i = 0, j = 0;

	die_group = &dymap_p->die_group[phy_zone->die_groups[0]];		//ȡ��һ��die�鼴�ɣ���Ϊ����die��ͬ��һ��
	femu_debug("Reset: zone(%u) fbg(%u, %u) bit map = %u\n", 	\
	zone_idx, phy_zone->die_groups[0], phy_zone->fbg_ofst, die_group->fbgs_used_bitmap[phy_zone->fbg_ofst]);

//	assert(die_group->fbgs_used_bitmap[phy_zone->fbg_ofst] > 0);	//���뱻ʹ��
	if (die_group->fbgs_used_bitmap[phy_zone->fbg_ofst] == 0) {		//�����״ι���Zenfsʱ�����
		return 0;
	}
	if ((die_group->fbgs_used_bitmap[phy_zone->fbg_ofst] - 1) == 0) { 	//û��zoneռ�ø�fbg������
		NvmeRequestPlus_t *req_plus = g_malloc(sizeof(NvmeRequestPlus_t));		//��װ����req	
		req_plus->req = req;
		req_plus->maxlat = 0;
		req_plus->cmd_type = NAND_ERASE;		

		req_plus->ts_sum = req_plus->ts_num = phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA;
		req_plus->flash_ts = (flash_ts_t **)g_malloc(sizeof(flash_ts_t *) * req_plus->ts_sum);

		for (i = 0, j = die_group->die[0]; i < req_plus->ts_sum; i++) {		//��ӳ��ĵ�һ��die��ʼ��die��֮���dieҲ��������
			req_plus->flash_ts[i] =(flash_ts_t *)g_malloc(sizeof(flash_ts_t));	/* Ҫ�ֿ����룬��Ȼ�ͷ�ʱ�����ע���ͷſռ� */
			req_plus->flash_ts[i]->req_plus = req_plus;
			req_plus->flash_ts[i]->zone_idx = zone_idx;
			req_plus->flash_ts[i]->die_idx = j;
			assert(++j <= spp->tt_luns);

			req_plus->flash_ts[i]->pl_idx = 0;	//reset������ÿ��plane����Ҫִ��
			req_plus->flash_ts[i]->ops_num = spp->pls_per_lun;	/* ÿ��zoneÿ��ֻ��ӳ�䵥��Die�е�������������Ŀ� */

			InsertInTail(&zns_ssd->list[req_plus->flash_ts[i]->die_idx], req_plus->flash_ts[i]);
		}

		for (i = 0; i < phy_zone->map_die_group_num; i++) {		//����Ԫ����	
			die_group = &dymap_p->die_group[phy_zone->die_groups[i]];
			die_group->fbgs_used_bitmap[phy_zone->fbg_ofst]--;
			die_group->remain_fbgs_num++;
		}
		
		return 1;
	}	else {		//����Ҫʵ�ʵĲ�����ֱ���ύ����
		for (i = 0; i < phy_zone->map_die_group_num; i++) {		
			die_group = &dymap_p->die_group[phy_zone->die_groups[i]];
			die_group->fbgs_used_bitmap[phy_zone->fbg_ofst]--;
		}

		return 0;
	}

	//��Ҫ����zone����Ϣ�����´�openʱ�Լ�������
	
}

#if PARA_ZNS 	//DIe��������+��һ��die������plane����ͬƫ�Ƶ�ַ�Ŀ����Ϊfbg
/* �����߼���ַ�Լ�ӳ������������������ַ */
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
	die_id = dymap_p->die_group[phy_zone->die_groups[dg_ofst_in_zone]].die[die_ofst_in_dg];	//�鿴ӳ���
	assert(die_id < spp->tt_luns);

	pl_ofst_in_die = page_ofst_in_dg % spp->pls_per_lun;	//die�е�planeҲ�ǲ���д
	pg_ofst_in_pl = page_ofst_in_dg / spp->pls_per_lun;		//����һ��zone���ӳ��һ��plane�е�һ��block		

	if (phy_zone->map_die_group_num > 1) { //����ӳ�䣬block�е�ҳ��ʼƫ�Ʋ�һ���Ǵ�0��ʼ
		pgs_per_fbg_part = spp->pgs_per_blk / phy_zone->map_die_group_num;
		pg_ofst_st_in_blk = phy_zone->fbg_part * pgs_per_fbg_part;
	}

	//תΪ���е�ַΪ��Ե�ַ, ���˲���д��Ԫ�����඼����ѭ����ԭ��(Channel��Die,Plane��block)
	chan_ofst_in_ssd = die_id / spp->luns_per_ch;	
	chip_ofst_in_chan = die_id % spp->luns_per_ch;
	pl_ofst_in_chip = pl_ofst_in_die;
	block_ofst_in_pl = phy_zone->fbg_ofst;
	pg_ofst_in_block = pg_ofst_in_pl + pg_ofst_st_in_blk;
	assert(pg_ofst_in_block < spp->pgs_per_blk);

	ppa->g.ch = chan_ofst_in_ssd;		//������Ҫ���������
	ppa->g.lun = chip_ofst_in_chan;
	ppa->g.pl = pl_ofst_in_chip;
	ppa->g.blk = block_ofst_in_pl;
	ppa->g.pg = pg_ofst_in_block;
	ppa->g.sec = 0;

//	femu_debug("zone=%u, wp=%lu, ch=%u, lun=%u, pl=%u, blk=%u, pg=%u\n", 
//		zone_idx, wp, ppa->g.ch, ppa->g.lun, ppa->g.pl, ppa->g.blk, ppa->g.pg);
}

#else

/* �����߼���ַ�Լ�ӳ������������������ַ
 * Ϊ�˲��Ķ������ĺ��������Ķ�FBG�����
 * FBG��Ȼ����������ɣ�������ͬһ��Pl����������������ɣ��Ӷ�FBG�����ܲ��в���
 * DIE��FBG�ı��ΪPl0��Pl1�����ţ��Ӷ���˳���FBG IDӳ��ʱ����ʵ��Pl����ѯӳ��
 * ���ڴ�FBG���ɣ�FBG�ڲ�Ϊһ��Blockһ��һ��Block��д��������
 * ����FBG���ɸı䣬���������ʽ�ı�
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
	die_id = dymap_p->die_group[phy_zone->die_groups[dg_ofst_in_zone]].die[die_ofst_in_dg];	//�鿴ӳ���
	assert(die_id < spp->tt_luns);

	blk_ofst_in_fbg = page_ofst_in_die / spp->pgs_per_blk;
	pg_ofst_in_block = page_ofst_in_die % spp->pgs_per_blk;

	assert(phy_zone->map_die_group_num == 1);	//��ģʽ�²��ܲ���ӳ�䣬open zone=2��4������=4����

	pl_ofst_in_die = phy_zone->fbg_ofst % spp->pls_per_lun;
	fbg_ofst_in_pl = phy_zone->fbg_ofst / spp->pls_per_lun;

	block_ofst_in_pl = fbg_ofst_in_pl * spp->pls_per_lun + blk_ofst_in_fbg;
	
	//תΪ���е�ַΪ��Ե�ַ, ���˲���д��Ԫ�����඼����ѭ����ԭ��(Channel��Die,Plane��block)
	chan_ofst_in_ssd = die_id / spp->luns_per_ch;	
	chip_ofst_in_chan = die_id % spp->luns_per_ch;
	pl_ofst_in_chip = pl_ofst_in_die;
	assert(pl_ofst_in_chip < spp->pls_per_lun);
//	block_ofst_in_pl;
//	pg_ofst_in_block;
	assert(block_ofst_in_pl < spp->blks_per_pl);
	assert(pg_ofst_in_block < spp->pgs_per_blk);

	ppa->g.ch = chan_ofst_in_ssd;		//������Ҫ���������
	ppa->g.lun = chip_ofst_in_chan;
	ppa->g.pl = pl_ofst_in_chip;
	ppa->g.blk = block_ofst_in_pl;
	ppa->g.pg = pg_ofst_in_block;
	ppa->g.sec = 0;

//	femu_debug("zone=%u, wp=%lu, ch=%u, lun=%u, pl=%u, blk=%u, pg=%u\n", 
//		zone_idx, wp, ppa->g.ch, ppa->g.lun, ppa->g.pl, ppa->g.blk, ppa->g.pg);
}

#endif

/* ����ӳ���ϵ�ַ���д������Ӧ�Ķ���
 * type��0��д��1������
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

	//����δ�����wp��˵������������ҳ�Ѿ���д�루��ȡ����д��һҳ��������������д���ҳ
	//��ͬ��
	if (wp % tt_lba_page > 0) {	
		new_wp = (wp / tt_lba_page + 1) * tt_lba_page;	//��һ������ҳ��ʹ��wp����
		if (nlb <= new_wp - wp) {	//д��û�г�����һҳ������Ҫ��������
			return;
		} else {
			nlb -= new_wp - wp;		//nlbҲ��Ҫ��дһЩ
		}
	} else {
		new_wp = wp;
	}
	page_num = (nlb-1) / tt_lba_page + 1;
//	femu_debug("Req: RW, Zone=%u, wp=%lu, new wp=%u, nlb=%u, pg_num=%u\n", zone_idx, wp, new_wp, nlb, page_num);

	NvmeRequestPlus_t *req_plus = g_malloc(sizeof(NvmeRequestPlus_t));		/* ��ÿһ��req��װΪreq plus */
	req_plus->req = req;
	req_plus->maxlat = 0;

	if (req->cmd.opcode == NVME_CMD_WRITE) {
		req_plus->cmd_type = NAND_WRITE;
//		assert(phy_zone->map_die_group_num > 0);
	} else if (req->cmd.opcode == NVME_CMD_READ) {
		req_plus->cmd_type = NAND_READ;
	} else
		assert(0);

	//�ַ�ʱ������Die�������ԣ�����ʱ�ٿ���Plane��������
	para_num_in_zone = phy_zone->map_die_group_num * ZONE_MAX_DIE_PARA;
	req_plus->ts_num = (page_num > para_num_in_zone) ? para_num_in_zone : page_num;
	req_plus->ts_sum = req_plus->ts_num;
	req_plus->flash_ts = (flash_ts_t **)g_malloc(sizeof(flash_ts_t *) * req_plus->ts_sum);		/* ҲҪ���� */
	
	for (i = 0; i < req_plus->ts_sum; i++) {
		req_plus->flash_ts[i] =(flash_ts_t *)g_malloc(sizeof(flash_ts_t));	/* Ҫ�ֿ����룬��Ȼ�ͷ�ʱ�����ע���ͷſռ� */
		memset(req_plus->flash_ts[i], 0, sizeof(flash_ts_t));
		req_plus->flash_ts[i]->pg_idx = 0xff;		//0xffΪһ����ʼֵ
	}

	for (i = new_wp, temp = 0; i < new_wp + nlb; i += tt_lba_page) {	//��page����
		get_ppa_by_lba_base(zns_ssd, zone_idx, i, &ppa);
		assert(req_plus->flash_ts[temp]);
		req_plus->flash_ts[temp]->die_idx = ppa.g.ch * spp->luns_per_ch + ppa.g.lun; //ת��Ϊ����die��ַ
		assert(req_plus->flash_ts[temp]->die_idx < spp->tt_luns);

#if PARA_ZNS
		if (req_plus->flash_ts[temp]->pl_idx == 0xff) {
			req_plus->flash_ts[temp]->pl_idx = ppa.g.pl;	//��ҳʱ��Я����ʼPlane
			assert(req_plus->flash_ts[temp]->pl_idx < spp->pls_per_lun);
		}
#else
		if (req_plus->flash_ts[temp]->pg_idx == 0xff) {	//��Я����ֵ
			req_plus->flash_ts[temp]->pl_idx = ppa.g.pl;
			req_plus->flash_ts[temp]->blk_idx = ppa.g.blk;
			req_plus->flash_ts[temp]->pg_idx = ppa.g.pg;
		}
#endif

		req_plus->flash_ts[temp]->ops_num++;	//ÿ�������д��ҳ��, ��Щҳ�������ڲ�ͬ��Plane����ֻ����һ��zone

		if (++temp == req_plus->ts_num)
			temp = 0;
	}

	for (i = 0; i < req_plus->ts_sum; i++) {
		req_plus->flash_ts[i]->req_plus = req_plus;		//��������
		req_plus->flash_ts[i]->zone_idx = zone_idx;

		InsertInTail(&zns_ssd->list[req_plus->flash_ts[i]->die_idx], req_plus->flash_ts[i]);
		
//		femu_debug("Transaction[%d], DIE(%u), ops num(%u)\n", i, req_plus->flash_ts[i]->die_idx, req_plus->flash_ts[i]->ops_num);
	}

	
}





/* �����ϲ㷢�͵�����ZNS�������open/close, reset, write/read��
 * ����reset, write/read��Ҫ��ʱ������Ҫ�������
 * open/closeΪ����ӳ���ϵ������Ҫ��ʱ������Ӧ��ֱ���ύ
 * ͳ�ƶ�д����ĸ������ж��Ƿ�Ϊ�����׶�
 */
void zns_req_distribute(NvmeRequest *req, FemuCtrl *n, int index_poller) 
{
	NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
	uint64_t slba = le64_to_cpu(rw->slba);
	uint32_t zone_idx = zns_zone_idx(n, slba);	
	assert(zone_idx < n->num_zones);
	NvmeZone *zone = &n->zone_array[zone_idx];						/* �߼�zone */
	uint64_t wp = slba - zone->d.zslba;
	phy_zone_t *phy_zone = &n->zns_ssd->dymap->phy_zone[zone_idx];	/* ����zone */
	static uint32_t req_sum = 0;

	uint32_t ret = 0;

	if (req->cmd.opcode != NVME_CMD_WRITE && req->cmd.opcode != NVME_CMD_READ) {	/* zone_mgmt���� */
		assert(wp == 0);	//�����������zoneΪ��λ��wp��nlb����0

		uint32_t dw13 = le32_to_cpu(req->cmd.cdw13);
		uint8_t action = dw13 & 0xff;
		if (action != NVME_ZONE_ACTION_RESET) {	//opens/close����
			if (action == NVME_ZONE_ACTION_OPEN) {
				zone_open(n, req, zone_idx);
			} else if (action == NVME_ZONE_ACTION_CLOSE) {
				zone_close(n, req, zone_idx);
			}

			//ֱ���ύ������CQ
			req->reqlat = 0;
			req->expire_time += 0;
			if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1) {
				femu_debug("ZNS to_poller enqueue failed\n");
		    }
		} else {		//reset����
			ret = zone_reset(n, req, zone_idx);
			if (ret == 0) {	//δ����ʵ�ʵĲ�������ֱ���ύ����
				req->reqlat = 0;
				req->expire_time += 0;
				if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1) {
					femu_debug("ZNS to_poller enqueue failed\n");
			    }
			}
		}
	} else {  //��д����
		//ͨ����д���������(�����Է���:�����׶λᷢ��148����д����)���ж��Ƿ�ͨ��femu�������׶�
		if (n->zns_ssd->start_up_flag == 0 && ++req_sum > 150) {	
			n->zns_ssd->start_up_flag = 1;
		}
//		femu_debug("req_sum = %u, type=%x\n", req_sum, req->cmd.opcode);

		if (n->zns_ssd->start_up_flag) {
			//��open zone֮ǰ��һЩ������ִ��ֱ���ύ
			if (req->cmd.opcode == NVME_CMD_READ && phy_zone->map_die_group_num == 0) {
				req->reqlat = 0;
				req->expire_time += 0;
				if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1)
					femu_debug("ZNS to_poller enqueue failed\n");
			} else {
				zone_rw(n, req, zone_idx, wp);
			}
		} else {	//λ�������׶ε�����ֱ���ύ
			req->reqlat = 0;
			req->expire_time += 0;
			if (femu_ring_enqueue(n->to_poller[index_poller], (void *)&req, 1) != 1)
				femu_debug("ZNS to_poller enqueue failed\n");
		}
	}
}

//��������open��Ϣ���Ա�ִ����һ������
void reset_open_infos(zns_dynamic_mapping_t *dymap_p)
{
	dymap_p->st_stage_flag = 1;
	
	//���ݵ�ǰ����open zone������������һ�������open zone������ע�ⷢ�����˳�������Ᵽ��һ��
	if (dymap_p->opened_zones_num == 4) {
		dymap_p->last_opened_zones_num = dymap_p->opened_zones_num;
		dymap_p->open_zone_limit = 2;
	} else if (dymap_p->opened_zones_num == 2 || dymap_p->opened_zones_num == 1) {
		dymap_p->last_opened_zones_num = dymap_p->opened_zones_num;
		dymap_p->open_zone_limit = 8;
	} else if (dymap_p->opened_zones_num == 8) {
		dymap_p->last_opened_zones_num = dymap_p->opened_zones_num;
		dymap_p->open_zone_limit = 4;
	} else if (dymap_p->opened_zones_num == 0) {	//˵���Ѿ������ù�
		;
	} else 
		assert(0);		//����

	//�������open zone����Ϣ����Ϊ���ѱ�ӳ�䲻�ɸı�
	dymap_p->opened_zones_num = 0;
}

/* ��ѯ���е�DIE Queue��ȡ�����������ִ�У�ִ��ʱ����plane�������� */
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

	static uint64_t last_time = 0;	//�ϴ�ȡ�������ʱ�䣬���ڴ�ӡ
#if PARA_ZNS
	uint32_t pl_ops = 0;	//ִ�в���plane�����Ĵ���
#else
	Node_t *p = NULL;
	Node_t *temp = NULL;
	uint32_t cur_type = 0; //0,����ͬplane��1����ͬpl����ͬ���ࣻ2������pl���У�
#endif

	for (i = 0; i < spp->tt_luns; i++) {
		ppa.g.ch = i % spp->nchs;
		ppa.g.lun = i / spp->nchs;
		lun = get_lun(zns_ssd, &ppa);

		now_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
		if (lun->next_lun_avail_time <= now_time) {					/* LUN���� */
			if (DeleteInHead(&list[i], &transaction_p) < 0) {		/* ����Ϊ�� */
				if (now_time-last_time >= 3000000000 && last_time != 0) {	//����ʱ�� >5s �Ŵ�ӡ,ͬʱ��������Ϊһ���������
					femu_debug("ops_num=%lu, pl_sum=%lu\n", zns_ssd->ops_sum, zns_ssd->pl_sum);
					femu_debug("sm_pl_sz=%lu, df_addr_sz=%lu, pl_sz=%lu\n", zns_ssd->sm_pl_sz, zns_ssd->df_addr_type_sz, zns_ssd->pl_sz);
					femu_debug("dg0_time=%lu, dg1_time=%lu, dg2_time=%lu, dg3_time=%lu, sum_serv_time_us=%lu\n",\
							zns_ssd->dg_time_us[0], zns_ssd->dg_time_us[1], zns_ssd->dg_time_us[2], zns_ssd->dg_time_us[3], 
							zns_ssd->sum_serv_time_us);	//TODO��д���ļ�
					last_time = now_time;

//					reset_open_infos(zns_ssd->dymap);	//��������open��Ϣ���Ա�ִ����һ������
				}
					
				continue;
			} 
			last_time = now_time;

			assert(transaction_p);
			assert(transaction_p->die_idx == i);
#if PARA_ZNS
			//����ֹplane������Ҫִ�м��β���plane����, �����漰����ҳ��ַ�ж�
			//TODO:��һ��ͳ�ƶ���ͬһ��zone�����Ĳ���plane
			pl_ops = (transaction_p->pl_idx + transaction_p->ops_num - 1) / spp->pls_per_lun + 1;
			switch (transaction_p->req_plus->cmd_type) {	//�Բ���plane�������������ӳ�curlat
				case NAND_WRITE:
					lun->next_lun_avail_time = now_time + pl_ops * spp->pg_wr_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += pl_ops * spp->pg_wr_lat / 1000;					//ͳ���ܵ�оƬ����ʱ��, ��us�����Դ���ִ���ӳ�
					zns_ssd->dg_time_us[i/ZONE_MAX_DIE_PARA] += pl_ops * spp->pg_wr_lat / 1000;		//����die���оƬ����ʱ��
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

//			if (curlat > 50000000) {	//�쳣
//				femu_debug("Die[%d], queue len = %d, zone %u\n", i, list[i].len, transaction_p->zone_idx);
//			}

			if (transaction_p->req_plus->cmd_type != NAND_ERASE) {	//��ͳ�ƶ�д����
				zns_ssd->pl_sz += transaction_p->ops_num;
				zns_ssd->pl_sum += transaction_p->ops_num;			//ÿ�����񶼻�ʹ��һ��plane
				zns_ssd->ops_sum += pl_ops;
			}

			release_ts(n, index_poller, transaction_p, curlat);
#else
			switch (transaction_p->req_plus->cmd_type) {	//�ȼ����ӳ�curlat
				case NAND_WRITE:
					lun->next_lun_avail_time = now_time + transaction_p->ops_num * spp->pg_wr_lat;
					curlat = lun->next_lun_avail_time - transaction_p->req_plus->req->stime;
					zns_ssd->sum_serv_time_us += transaction_p->ops_num * spp->pg_wr_lat / 1000;	//ͳ���ܵ�оƬ����ʱ��, ��us�����Դ���ִ���ӳ�
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

			//�������ж��Ƿ���ִ��plane�����У�����д
			if (transaction_p->req_plus->cmd_type != NAND_ERASE) {	
				p = list[i].head->next;
				while (p) {
					if (p->data->pl_idx != transaction_p->pl_idx) {	//������������plane������
						//��ַ���Ͷ���ͬ��ȡ����������ִ�У�����Ӱ��zone��˳��д����Ϊһ��zone��һ��dieֻ������ĳһ��plane�������ܲ���
						if (p->data->req_plus->cmd_type == transaction_p->req_plus->cmd_type &&
							p->data->blk_idx == transaction_p->blk_idx &&
							p->data->pg_idx == transaction_p->pg_idx) {	//TODO�����ж���������׵�ַ
							p->prev->next = p->next;		//�Ӷ�����ȡ��������
							if (p->next)
								p->next->prev = p->prev;
							else	//pΪβ�ڵ�
								list[i].tail = p->prev;
							list[i].len--;

							zns_ssd->pl_sz += p->data->ops_num;		//ͳ��
							zns_ssd->ops_sum +=  p->data->ops_num;
							cur_type = 2;
							
							release_ts(n, index_poller, p->data, curlat);	//ִ�У�ע����ͷ�����p->data�Ŀռ�

							temp = p->next;
							g_free(p);
							p = temp;
							continue;
						} else {
							if (cur_type != 2) //����ʱ��Ҫ�ı�֮ǰ������
								cur_type = 1;
						}
					} 

					p = p->next;
				}

				if (cur_type == 2) {
					zns_ssd->pl_sz += transaction_p->ops_num;
					zns_ssd->pl_sum += transaction_p->ops_num * 2;		//ÿ����������Ҫ��һ��plane
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

	dymap_p->fbgs_per_dg = spp->blks_per_pl;	//����FBG������Ϊ���������п������

	dymap_p->die_group_sum = spp->tt_luns / ZONE_MAX_DIE_PARA;
	dymap_p->die_group = (die_group_t *)g_malloc0(sizeof(die_group_t) * dymap_p->die_group_sum);
	dymap_p->die_group_used_bitmap = (uint32_t *)g_malloc0(sizeof(uint32_t) * dymap_p->die_group_sum);
	for (i = 0; i < dymap_p->die_group_sum; i++) {
		dymap_p->die_group_used_bitmap[i] = 0;

		dymap_p->die_group[i].die = (uint32_t *)g_malloc0(sizeof(uint32_t) * ZONE_MAX_DIE_PARA);
		for (j = 0; j < ZONE_MAX_DIE_PARA; j++) {
			dymap_p->die_group[i].die[j] = i * ZONE_MAX_DIE_PARA + j;		//DIE ID���ڵ�Ϊһ��die group
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
		

	/* ��ÿ��DIE��ʼ��һ��DIE QUEUE�������������� */
	zns_ssd->list = (List_t *)g_malloc((size_t)spp->tt_luns * sizeof(List_t));
	for (i = 0; i < spp->tt_luns; i++) {
		InitList(&zns_ssd->list[i]);
	}
	
	femu_debug("DIE Queue init successfully!\n");

	zns_dynamic_mapping_init(n);
	femu_debug("Zone mapping init successfully!\n");
}



