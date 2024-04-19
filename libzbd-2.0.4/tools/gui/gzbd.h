/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * SPDX-FileCopyrightText: 2020 Western Digital Corporation or its affiliates.
 *
 * Authors: Damien Le Moal (damien.lemoal@wdc.com)
 */
#ifndef __GZBD_H__
#define __GZBD_H__

#include <sys/time.h>
#include <pthread.h>
#include <gtk/gtk.h>

#include <libzbd/zbd.h>

/**
 * Default refresh interval (milliseconds).
 */
#define DZ_INTERVAL	1000

/**
 * Zone information list columns.
 */
enum {
	DZ_ZONE_NUM = 0,
	DZ_ZONE_LIST_COLUMS
};

/**
 * Device command IDs.
 */
enum {
	DZ_CMD_REPORT_ZONES,
	DZ_CMD_ZONE_OP,
};

/**
 * Maximum number of devices that can be open.
 */
#define DZ_MAX_DEV	32

/**
 * Device zone information.
 */
typedef struct dz_dev_zone {

	int			no;
	int			visible;
	struct zbd_zone		info;

} dz_dev_zone_t;

/**
 * GUI Tab data.
 */
typedef struct dz_dev {

	char			path[128];
	int			opening;

	int			dev_fd;
	struct zbd_info		info;
	unsigned long long	capacity;
	int			block_size;
	int			use_hexa;

	int			zone_ro;
	unsigned int		zone_op;
	int			zone_no;
	unsigned int		max_nr_zones;
	unsigned int		nr_zones;
	struct zbd_zone		*zbdz;
	dz_dev_zone_t		*zones;

	/**
	 * Command execution.
	 */
	int			cmd_id;
	pthread_t		cmd_thread;
	GtkWidget		*cmd_dialog;

	/**
	 * Interface stuff.
	 */
	GtkWidget		*page;
	GtkWidget		*page_frame;

	GtkWidget		*zfilter_combo;
	GtkWidget		*zlist_frame_label;
	GtkWidget		*zlist_treeview;
	GtkTreeModel		*zlist_model;
	GtkListStore		*zlist_store;
	unsigned int		zlist_start_no;
	unsigned int		zlist_end_no;
	int			zlist_selection;
	GtkWidget		*znum_entry;
	GtkWidget		*zblock_entry;

	GtkWidget		*zones_da;

} dz_dev_t;

/**
 * GUI data.
 */
typedef struct dz {

	dz_dev_t		dev[DZ_MAX_DEV];
	int			nr_devs;

	int			interval;
	int			block_size;
	int			abort;

	/**
	 * Interface stuff.
	 */
	GtkWidget		*window;
	GtkWidget		*vbox;
	GtkWidget		*notebook;
	GtkWidget		*no_dev_frame;

	GdkRGBA			conv_color;
	GdkRGBA			seqnw_color;
	GdkRGBA			seqw_color;
	GdkRGBA			nonw_color;

	/**
	 * For handling signals.
	 */
	int			sig_pipe[2];

} dz_t;

/**
 * System time in usecs.
 */
static inline unsigned long long dz_usec(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (unsigned long long) tv.tv_sec * 1000000LL +
		(unsigned long long) tv.tv_usec;
}

extern dz_t dz;

dz_dev_t *dz_open(char *path);
void dz_close(dz_dev_t *dzd);

int dz_cmd_exec(dz_dev_t *dzd, int cmd_id, char *msg);

void dz_if_err(const char *msg, const char *fmt, ...);
void dz_if_create(void);
void dz_if_destroy(void);
void dz_if_add_device(char *dev_path);
dz_dev_t *dz_if_dev_open(char *path);
void dz_if_dev_close(dz_dev_t *dzd);
void dz_if_dev_update(dz_dev_t *dzd, int do_report_zones);

#endif /* __GZBD_H__ */
