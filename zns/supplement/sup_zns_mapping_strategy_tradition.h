#ifndef __FEMU_ZNS_MAP_TRADITION_H
#define __FEMU_ZNS_MAP_TRADITION_H

#include "../../nvme.h"
#include "../../nvme.h"
#include "../zns.h"
#include "../../bbssd/ftl.h"
#include "sup_zns_mapping_strategy_interface.h"

#define MAP_TRADITION_NAME "tradition map"
#define PARA_LEVEL		(16)

/* ZNS传统映射：并行性是固定的 */
typedef struct sup_zns_phy_map_tradition{
	uint32_t *die_group;
	uint64_t block_group_num_per_die;	//每个die中映射的zone的数量
}sup_zns_phy_map_tradition_t;


int sup_zns_phy_m_tradition_register(FemuCtrl *n);


#endif


