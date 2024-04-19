#ifndef __FEMU_FAIRNESS_H
#define __FEMU_FAIRNESS_H

#include <stdio.h>
#include "../../nvme.h"
#include "../zns.h"
#include "../../bbssd/ftl.h"
#include "../supplement/sup_zns_mapping_strategy_interface.h"

#define ZONE_MAX_DIE_PARA (16)	//����zone�ܹ����ɵ����Բ��������, Ҳ��die���е�die����

#define INITIAL_OPEN_LIMIT (4)
#define PARA_ZNS (1)	//1��ʹ��ȫ�����ԣ�0����ʹ��Die��������(���ܲ���open zone>N)

#define MAXOPENZONES (10)

typedef struct flash_transaction flash_ts_t;

/* phy_zone:ID���߼�zoneIDһһ��Ӧ����ų��߼�zone��׼����֮���������Ϣ����ӳ���� */
typedef struct phy_zone {
	uint32_t map_die_group_num;		//��zoneӳ���die�������
	uint32_t die_groups[8];			//�����die�� id, ���ܳ���8��, ��˳����
	uint32_t fbg_ofst;				//��die���о����fbg(��Ե�ַ)������ж��die�飬die���ƫ������ͬ��
	uint32_t fbg_part;				//��map_die_group_num > 1ʱ������ӳ�䣩���ò����������ã�������ʾ��ӳ���fbg�е���һ����
}phy_zone_t;

/* ��Ŵ򿪵�zone����ӳ���ϵ */
//typedef struct opened_zones_map {
//	uint32_t zone_id;
//	uint32_t map_die_group_num;		//��zoneӳ���die�������
//	uint32_t die_groups[4];			//�����die�� id
//};

typedef struct fbg_part {
	uint32_t fbg_id;			//������ʹ�õ�fbg
	uint32_t fbg_part_sum;		//��fbg���ֳ��˼�����
	uint32_t next_fbg_part;		//��һ�����ò���,��0��ʼ
}fbg_part_t;


/* һ�鱻������һ���die�������FBGΪһ��PBG��PBG��ͬһ��die�����з�����ͬƫ�Ƶ�ַ�Ŀ���ɣ� */
typedef struct die_group {
	uint32_t *die;					//DIE id����chip ID
	//ӳ��ʱ+1��resetʱ-1��Ϊ0�����fbg���ܲ����Լ����ٴη���
	uint32_t *fbgs_used_bitmap;		//ʹ��λͼ����ʾ�ڲ�fbg��ʹ�������0:���У�n:n��zone��ʹ��
	uint32_t *fbgs_parts_sum;		//����fbg�ᱻ�ֳɼ����֣�Ĭ��Ϊ1
	uint32_t remain_fbgs_num;	//δ����FBG�����������ַ����Ҳ�����
	uint32_t next_fbg_id;		//FBGΪ��ѯ���䣬next_fbg_id������һ�η���fbg��id(FCG�ڵ�),��ֵֻ����Ϊ��ѯ�����һ���ο�������һ���´η������ʹ�ø�ֵ

	uint32_t part_map_flag;		//��die�����Ƿ���ڲ���ӳ���FBG������ʹ��
	fbg_part_t fbg_part_map;	//part_map_flagΪ1ʱ��Ч
}die_group_t;

typedef struct zns_dynamic_mapping {
	uint32_t opened_zones[MAXOPENZONES];		//��Ŵ򿪵�zone ID
	uint32_t opened_zones_num;					//�Ѵ򿪵�zone����
	uint32_t open_zone_limit;					//��zone������

	uint32_t die_group_sum;						//��Բ���������һ����Բ���о�Բ������Ϊһ��zone�����ɵ����ֵ
	die_group_t *die_group;						//�������ľ�Բ����Ϣ
	uint32_t *die_group_used_bitmap;			//��Բ�鱻open zoneʹ�õ�λͼ�������ظ�ʹ�ã�0:���У�n:n��open zone��ʹ��
	
	uint32_t fbgs_per_dg;						//ÿ����Բ����FBG������ = blks_per_plane
	uint64_t zones_per_dg;						//ÿ����Բ����zone������

	phy_zone_t *phy_zone;						//�������zone��������Ϣ����ID���߼�zoneIDһһ��Ӧ

	uint32_t st_stage_flag;						//�Ƿ�λ�ڳ�ʼʱ������zone�Ľ׶�
	uint32_t last_opened_zones_num;				//��һ�������zone������
}zns_dynamic_mapping_t;




void alloc_fbg(zns_dynamic_mapping_t *dymap_p, uint32_t fbg_type, uint32_t dg_idx, uint32_t *fbg_ofst, uint32_t *fbg_part);
void zone_open(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx);
void zone_close(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx);
uint32_t zone_reset(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx);
void zone_rw(FemuCtrl *n, NvmeRequest *req, uint32_t zone_idx, uint64_t wp);
void zns_dynamic_mapping_init(FemuCtrl *n);
void reset_open_infos(zns_dynamic_mapping_t *dymap_p);

void release_ts(FemuCtrl *n, int index_poller, flash_ts_t *ts, uint64_t curlat);
void zns_process_die_queue(FemuCtrl *n, int index_poller);
void zns_req_distribute(NvmeRequest *req, FemuCtrl *n, int index_poller);
void zns_ftl_init(FemuCtrl *n);

#endif