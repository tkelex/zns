#ifndef __FEMU_FAIRNESS_H
#define __FEMU_FAIRNESS_H

#include <stdio.h>
#include "../../nvme.h"
#include "../zns.h"
#include "../../bbssd/ftl.h"
#include "../supplement/sup_zns_mapping_strategy_interface.h"

#define ZONE_MAX_DIE_PARA (16)	//单个zone能够容纳的最大晶圆级并行性, 也是die组中的die数量

#define INITIAL_OPEN_LIMIT (4)
#define PARA_ZNS (1)	//1，使能全并行性；0，仅使能Die级并行性(仅能测试open zone>N)

#define MAXOPENZONES (10)

typedef struct flash_transaction flash_ts_t;

/* phy_zone:ID和逻辑zoneID一一对应，存放除逻辑zone标准参数之外的所有信息，如映射表等 */
typedef struct phy_zone {
	uint32_t map_die_group_num;		//该zone映射的die组的数量
	uint32_t die_groups[8];			//具体的die组 id, 不能超过8个, 按顺序存放
	uint32_t fbg_ofst;				//该die组中具体的fbg(相对地址)，如果有多个die组，die组的偏移是相同的
	uint32_t fbg_part;				//当map_die_group_num > 1时（部分映射），该参数才起作用，用来表示其映射的fbg中的哪一部分
}phy_zone_t;

/* 存放打开的zone和其映射关系 */
//typedef struct opened_zones_map {
//	uint32_t zone_id;
//	uint32_t map_die_group_num;		//该zone映射的die组的数量
//	uint32_t die_groups[4];			//具体的die组 id
//};

typedef struct fbg_part {
	uint32_t fbg_id;			//被部分使用的fbg
	uint32_t fbg_part_sum;		//该fbg被分成了几部分
	uint32_t next_fbg_part;		//下一个可用部分,从0开始
}fbg_part_t;


/* 一组被捆绑在一起的die，这里的FBG为一组PBG（PBG由同一个die中所有分组相同偏移地址的块组成） */
typedef struct die_group {
	uint32_t *die;					//DIE id，即chip ID
	//映射时+1，reset时-1，为0代表该fbg才能擦除以及被再次分配
	uint32_t *fbgs_used_bitmap;		//使用位图来表示内部fbg的使用情况，0:空闲，n:n个zone在使用
	uint32_t *fbgs_parts_sum;		//单个fbg会被分成几部分，默认为1
	uint32_t remain_fbgs_num;	//未分配FBG的数量，部分分配的也算分配
	uint32_t next_fbg_id;		//FBG为轮询分配，next_fbg_id代表下一次分配fbg的id(FCG内的),该值只是作为轮询分配的一个参考，并不一定下次分配必须使用该值

	uint32_t part_map_flag;		//该die组中是否存在部分映射的FBG，优先使用
	fbg_part_t fbg_part_map;	//part_map_flag为1时有效
}die_group_t;

typedef struct zns_dynamic_mapping {
	uint32_t opened_zones[MAXOPENZONES];		//存放打开的zone ID
	uint32_t opened_zones_num;					//已打开的zone数量
	uint32_t open_zone_limit;					//打开zone的限制

	uint32_t die_group_sum;						//晶圆组的数量，一个晶圆组中晶圆的数量为一个zone能容纳的最大值
	die_group_t *die_group;						//保存具体的晶圆组信息
	uint32_t *die_group_used_bitmap;			//晶圆组被open zone使用的位图，可以重复使用，0:空闲，n:n个open zone在使用
	
	uint32_t fbgs_per_dg;						//每个晶圆组中FBG的数量 = blks_per_plane
	uint64_t zones_per_dg;						//每个晶圆组中zone的数量

	phy_zone_t *phy_zone;						//存放物理zone的所有信息，其ID和逻辑zoneID一一对应

	uint32_t st_stage_flag;						//是否位于初始时连续打开zone的阶段
	uint32_t last_opened_zones_num;				//上一个任务打开zone的数量
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