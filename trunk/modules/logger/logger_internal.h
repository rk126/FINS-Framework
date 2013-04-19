/*
 * logger_internal.h
 *
 *  Created on: Feb 3, 2013
 *      Author: Jonathan Reed
 */

#ifndef LOGGER_INTERNAL_H_
#define LOGGER_INTERNAL_H_

#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef BUILD_FOR_ANDROID
#include <sys/endian.h>
#endif

#include <finsdebug.h>
#include <finstypes.h>
#include <finstime.h>
#include <metadata.h>
#include <finsqueue.h>

#include "logger.h"

#define LOGGER_LIB "logger"
#define LOGGER_MAX_PORTS 0

struct logger_data {
	struct linked_list *link_list;
	uint32_t flows_num;
	uint32_t flows[LOGGER_MAX_PORTS];

	pthread_t switch_to_logger_thread;
	uint8_t logger_interrupt_flag;

	double logger_interval;
	int logger_repeats;

	int logger_started;
	int logger_packets;
	int logger_bytes;
	int logger_saved_packets;
	int logger_saved_bytes;

	struct timeval logger_start;
	double logger_saved_curr;
	struct timeval logger_end;

	struct intsem_to_timer_data *logger_to_data;
	uint8_t logger_flag;
};

int logger_to_switch(struct fins_module *module, struct finsFrame *ff);
void logger_get_ff(struct fins_module *module);

void logger_fcf(struct fins_module *module, struct finsFrame *ff);
void logger_set_param(struct fins_module *module, struct finsFrame *ff);
//void logger_exec(struct fins_module *module, struct finsFrame *ff);
//void logger_exec_clear_sent(struct fins_module *module, struct finsFrame *ff, uint32_t host_ip, uint16_t host_port, uint32_t rem_ip, uint16_t rem_port);
//void logger_error(struct fins_module *module, struct finsFrame *ff);

//void logger_in_fdf(struct fins_module *module, struct finsFrame *ff);
//void logger_out_fdf(struct fins_module *module, struct finsFrame *ff);

void logger_interrupt(struct fins_module *module);

int logger_init(struct fins_module *module, uint32_t *flows, uint32_t flows_num, metadata_element *params, struct envi_record *envi);
int logger_run(struct fins_module *module, pthread_attr_t *attr);
int logger_pause(struct fins_module *module);
int logger_unpause(struct fins_module *module);
int logger_shutdown(struct fins_module *module);
int logger_release(struct fins_module *module);

#endif /* LOGGER_INTERNAL_H_ */