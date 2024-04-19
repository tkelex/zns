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
	void *map_data;		//ָ���²㱣���Լ�ӳ�䷽���ľ������ݽṹ
	int (*phy_zone_mapping_init)(const char *name, FemuCtrl *n);
	struct ppa (*get_ppa_by_zone_info)(struct zns_ssd *zns_ssd, uint32_t zone_index, uint64_t wp, uint32_t lbasz);	/* wpΪ��Ե�ַ */
	int (*zone_advance_avail_time)(struct zns_ssd *zns_ssd, struct nand_cmd *ncmd, struct nand_lun **lun_group, uint32_t *page_arr);	
	int (*get_para_level)(uint32_t zone_idx);
	void (*get_lun)(uint32_t *die_num, uint32_t zone_idx);
}sup_zns_phy_m_strategy_t;


/* ��װһ�¶��� */
typedef struct app_queue{
	struct rte_ring *queue;
	int32_t sqid;
	QemuMutex tts_mutex;
	int32_t tt_size;		/* ������ܴ�С */
	int32_t flow_level;		
}app_queue_t;

typedef struct flash_transaction flash_ts_t;

/* ��NvmeRequestPlus��һ�����з�װ */
typedef struct NvmeRequestPlus{
	NvmeRequest *req;			//ָ��Դreq��ָ��
	flash_ts_t **flash_ts;		//ָ���req����ֵ����������ָ��, �����˳������ӳ���DIEһһ��Ӧ
	uint32_t ts_num;			//��ֳ����������(��ʵ��������die������)��ÿ����һ�������-1����ɾ��������Ϊ0ʱ�ύ��req��CQ��ȥ
	uint32_t ts_sum;			//��ֳ����������, ����ֵ
	uint64_t maxlat;			//��¼DIE��Ӧ���������еľ���ʱ�����ֵ���ύ��CQʱ���丳ֵ��req->expire_time
	uint32_t cmd_type;			//IO���ͣ�����д������, ͬһ���������������ͬ��
}NvmeRequestPlus_t;

/* flash_transaction�Ǵ����chip queue����ģ�����Ҳ��������Ϊ������λ */
struct flash_transaction {
	NvmeRequestPlus_t *req_plus;	//ָ��NvmeRequestPlus��ָ��
	uint32_t zone_idx;				//zone id
	uint32_t ops_num;				//��������������Ƕ�д����ָ����Ҫ��д��ҳ��������ǲ�������ָ����Ҫ�����Ŀ���

	uint32_t die_idx;				//DIE id�����е�ID���Ǵ�0��ʼ
	uint32_t pl_idx;				//���������ʼplane id��
	uint32_t blk_idx;				//plane�е� blk id
	uint32_t pg_idx;				//block�е� pg id
	
	uint32_t flow_level;			//����IO���ȼ���Ŀǰ��������ĸ���1-4������ȼ�
};


/* ˫�������ʵ�� */
typedef flash_ts_t *elemtype;
typedef struct Node	{	
	struct Node *prev;
	elemtype data;
	struct Node *next;
}Node_t;
typedef struct LinkList {
	Node_t *head;	/* ͷ��㣺��������� */
	Node_t *tail;	/* βָ�룺ָ�����һ�����,���ڷ������ */
	int32_t len;	/* ������ */
}List_t;



/* Description ZNS physical information */
typedef struct zns_ssd{
	char *ssdname;
    struct ssdparams sp;
    struct ssd_channel *ch;

	/* Zone�ײ�ӳ�� */
	struct sup_zns_phy_m_strategy zns_phy_m_strategy;

    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    bool *dataplane_started_ptr;

	/* DIE Queue */
	QemuLockCnt *list_mutex; 
	List_t *list;
	struct rte_ring **die_queue;

	struct zns_dynamic_mapping *dymap;	/* ���涯̬ӳ����Ƶ�����Ԫ��Ϣ */

	uint32_t start_up_flag;		//0,����femu�������׶Σ�ĳЩ��Ϣ��û��׼���ã�1���Ѿ��ɹ�����

	/* Statistics */
	uint64_t sm_pl_sz;			//��ͬ�������͵������С����pageΪ��λ��ע�����ۼӵ�
	uint64_t df_addr_type_sz;	//��ͬ���鵫��ͬ��ַ������
	uint64_t pl_sz;				//��ͬ��������ͬ��ַ�����ͣ�����plane����ִ��
	uint64_t ops_sum;			//�ܵĲ��������������die��ÿ�β���������д��
	uint64_t pl_sum; 			//�ܵ�ʹ��plane������
	

//	uint32_t start_st_flag;		//��ʼͳ��оƬ����ʱ��ı��
//	uint64_t sum_free_time_ms;	//����оƬ���ܿ���ʱ��(ms)������ִ��ʱ�����оƬ������
	uint64_t sum_serv_time_us;	//����оƬ�ķ��������ʱ��(us)
	uint64_t dg_time_us[8];		//ÿ��die����������ʱ��(us)

	FILE *f;					//��������ͳ�����ݵ��ļ�
}zns_ssd_t;



int zns_ssd_init(const char *name, FemuCtrl *n);

#ifdef STATIC_MAP
uint64_t zns_ssd_write(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info);
uint64_t zns_ssd_read(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info);
uint64_t zns_ssd_erase(struct zns_ssd *zns_ssd, NvmeRequest *req, zone_info_t *zone_info);
#endif


#endif