#ifndef __FEMU_ZNS_MAP_INTERFACE_H
#define __FEMU_ZNS_MAP_INTERFACE_H

#include "../../nvme.h"
#include "../zns.h"
#include "../../bbssd/ftl.h"
#include "../fairness/fairness.h"


typedef struct zone_info {
	uint32_t zone_idx;
	uint64_t wp;
	uint32_t lbasz;
	uint32_t nlb;
}zone_info_t;

typedef struct sup_zns_phy_m_strategy {
	const char *name;
	void *map_data;		//指向下层保存自己映射方案的具体数据结构
	int (*phy_zone_mapping_init)(const char *name, FemuCtrl *n);
	struct ppa (*get_ppa_by_zone_info)(struct zns_ssd *zns_ssd, uint32_t zone_index, uint64_t wp, uint32_t lbasz);	/* wp为相对地址 */
	int (*zone_advance_avail_time)(struct zns_ssd *zns_ssd, struct nand_cmd *ncmd, struct nand_lun **lun_group, uint32_t *page_arr);	
	int (*get_para_level)(uint32_t zone_idx);
	void (*get_lun)(uint32_t *die_num, uint32_t zone_idx);
}sup_zns_phy_m_strategy_t;


/* 封装一下队列 */
typedef struct app_queue{
	struct rte_ring *queue;
	int32_t sqid;
	QemuMutex tts_mutex;
	int32_t tt_size;		/* 请求的总大小 */
	int32_t flow_level;		
}app_queue_t;

typedef struct flash_transaction flash_ts_t;

/* 将NvmeRequestPlus进一步进行分装 */
typedef struct NvmeRequestPlus{
	NvmeRequest *req;			//指向源req的指针
	flash_ts_t **flash_ts;		//指向该req所拆分的所有事务的指针, 事务的顺序与其映射的DIE一一对应
	uint32_t ts_num;			//拆分出的事务个数(其实就是所用die的数量)，每处理一个事务就-1，并删除其事务，为0时提交该req到CQ中去
	uint32_t ts_sum;			//拆分出的事务个数, 不变值
	uint64_t maxlat;			//记录DIE响应所有事务中的绝对时间最大值，提交到CQ时将其赋值到req->expire_time
	uint32_t cmd_type;			//IO类型：读、写、擦除, 同一个请求的类型是相同的
}NvmeRequestPlus_t;

/* flash_transaction是存放在chip queue里面的，调度也是以事务为基本单位 */
struct flash_transaction {
	NvmeRequestPlus_t *req_plus;	//指向NvmeRequestPlus的指针
	uint32_t zone_idx;				//zone id
	uint32_t ops_num;				//操作数量：如果是读写，其指的是要读写的页数；如果是擦除，其指的是要擦除的块数

	uint32_t die_idx;				//DIE id，所有的ID都是从0开始
	uint32_t pl_idx;				//该事务的起始plane id，
	uint32_t blk_idx;				//plane中的 blk id
	uint32_t pg_idx;				//block中的 pg id
	
	uint32_t flow_level;			//所属IO流等级，目前最多设置四个：1-4代表其等级
};


/* 双向链表的实现 */
typedef flash_ts_t *elemtype;
typedef struct Node	{	
	struct Node *prev;
	elemtype data;
	struct Node *next;
}Node_t;
typedef struct LinkList {
	Node_t *head;	/* 头结点：不存放数据 */
	Node_t *tail;	/* 尾指针：指向最后一个结点,用于反向遍历 */
	int32_t len;	/* 链表长度 */
}List_t;



/* Description ZNS physical information */
typedef struct zns_ssd{
	char *ssdname;
    struct ssdparams sp;
    struct ssd_channel *ch;

	/* Zone底层映射 */
	struct sup_zns_phy_m_strategy zns_phy_m_strategy;

    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    bool *dataplane_started_ptr;

	/* DIE Queue */
	QemuLockCnt *list_mutex; 
	List_t *list;
	struct rte_ring **die_queue;

	struct zns_dynamic_mapping *dymap;	/* 保存动态映射机制的所有元信息 */

	uint32_t start_up_flag;		//0,处于femu的启动阶段，某些信息还没有准备好；1，已经成功启动

	/* Statistics */
	uint64_t sm_pl_sz;			//相同分组类型的事务大小，以page为单位，注意是累加的
	uint64_t df_addr_type_sz;	//不同分组但不同地址或类型
	uint64_t pl_sz;				//不同分组且相同地址和类型，可以plane并行执行
	uint64_t ops_sum;			//总的操作次数，针对于die的每次操作（读、写）
	uint64_t pl_sum; 			//总的使用plane的数量
	

//	uint32_t start_st_flag;		//开始统计芯片空闲时间的标记
//	uint64_t sum_free_time_ms;	//所有芯片的总空闲时间(ms)，除以执行时间就是芯片利用率
	uint64_t sum_serv_time_us;	//所有芯片的服务事务的时间(us)
	uint64_t dg_time_us[8];		//每个die组服务事务的时间(us)

	FILE *f;					//用来保存统计数据的文件
}zns_ssd_t;



int zns_ssd_init(const char *name, FemuCtrl *n);

#ifdef STATIC_MAP
uint64_t zns_ssd_write(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info);
uint64_t zns_ssd_read(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info);
uint64_t zns_ssd_erase(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info);
#endif


#endif