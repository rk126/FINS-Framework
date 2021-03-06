/*
 * rtm.c
 *
 *  Created on: Jul 10, 2012
 *      Author: bamj001, atm011
 */
#include "rtm_internal.h"

int rtm_cmd_serial_test(struct rtm_command *cmd, uint32_t *serial_num) {
	return cmd->serial_num == *serial_num;
}

int rtm_console_id_test(struct rtm_console *console, uint32_t *id) {
	return console->id == *id;
}

int rtm_console_fd_test(struct rtm_console *console, int *fd) {
	return console->fd == *fd;
}

void console_free(struct rtm_console *console) {
	PRINT_DEBUG("Entered: console=%p", console);

	if (console->addr != NULL) {
		PRINT_DEBUG("Freeing: addr=%p", console->addr);
		free(console->addr);
	}

	free(console);
}

void *accept_console(void *local) {
	struct fins_module *module = (struct fins_module *) local;
	struct rtm_data *md = (struct rtm_data *) module->data;

	PRINT_IMPORTANT("Entered: module=%p", module);

	int32_t addr_size = sizeof(struct sockaddr_un);
	struct sockaddr_un *addr;
	int console_fd;
	struct rtm_console *console;
	int i;

	secure_sem_wait(&md->shared_sem);
	while (module->state == FMS_RUNNING) {
		if (list_has_space(md->console_list)) {
			sem_post(&md->shared_sem);

			addr = (struct sockaddr_un *) secure_malloc(addr_size);
			while (module->state == FMS_RUNNING) {
				sleep(1);
				console_fd = accept(md->server_fd, (struct sockaddr *) addr, (socklen_t *) &addr_size);
				if (console_fd > 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
					break;
				}
			}
			if (module->state != FMS_RUNNING) {
				free(addr);

				secure_sem_wait(&md->shared_sem);
				break;
			}

			if (console_fd < 0) {
				PRINT_ERROR("accept error: server_fd=%d, console_fd=%d, errno=%u, str='%s'", md->server_fd, console_fd, errno, strerror(errno));
				free(addr);

				secure_sem_wait(&md->shared_sem);
				continue;
			}

			secure_sem_wait(&md->shared_sem);
			console = (struct rtm_console *) secure_malloc(sizeof(struct rtm_console));
			console->id = md->console_counter++;
			console->fd = console_fd;
			console->addr = addr;

			PRINT_IMPORTANT("Console created: id=%u, fd=%d, addr='%s'", console->id, console->fd, console->addr->sun_path);
			list_append(md->console_list, console);

			for (i = 0; i < MAX_CONSOLES; i++) {
				if (md->console_fds[i] == 0) {
					md->console_fds[i] = console_fd;
					break;
				}
			}
		} else {
			sem_post(&md->shared_sem);
			sleep(5);
			secure_sem_wait(&md->shared_sem);
		}
	}
	sem_post(&md->shared_sem);

	PRINT_IMPORTANT("Exited: module=%p", module);
	return NULL;
}

void *console_to_rtm(void *local) {
	struct fins_module *module = (struct fins_module *) local;
	struct rtm_data *md = (struct rtm_data *) module->data;

	PRINT_IMPORTANT("Entered: module=%p", module);

	int poll_num;
	struct pollfd poll_fds[MAX_CONSOLES];
	int time = 1;
	int ret;
	struct rtm_console *console;

	int i;
	for (i = 0; i < MAX_CONSOLES; i++) {
		poll_fds[i].events = POLLIN | POLLPRI | POLLRDNORM;
		//poll_fds[1].events = POLLIN | POLLPRI | POLLOUT | POLLERR | POLLHUP | POLLNVAL | POLLRDNORM | POLLRDBAND | POLLWRNORM | POLLWRBAND;
	}
	PRINT_DEBUG("events=0x%x", poll_fds[0].events);

	uint32_t cmd_len;
	uint8_t cmd_buf[MAX_CMD_LEN + 1];

	secure_sem_wait(&md->shared_sem);
	while (module->state == FMS_RUNNING) {
		poll_num = md->console_list->len;
		if (poll_num > 0) {
			for (i = 0; i < MAX_CONSOLES; i++) {
				if (md->console_fds[i] == 0) {
					poll_fds[i].fd = -1;
				} else {
					poll_fds[i].fd = md->console_fds[i];
				}
			}
			sem_post(&md->shared_sem);
			ret = poll(poll_fds, poll_num, time);
			secure_sem_wait(&md->shared_sem);
			if (ret < 0) {
				PRINT_ERROR("ret=%d, errno=%u, str='%s'", ret, errno, strerror(errno));
				break;
			} else if (ret > 0) {
				PRINT_DEBUG("poll: ret=%d", ret);

				for (i = 0; i < MAX_CONSOLES; i++) {
					if (poll_fds[i].fd > 0 && poll_fds[i].revents > 0) {
						if (1) {
							PRINT_DEBUG(
									"POLLIN=%d POLLPRI=%d POLLOUT=%d POLLERR=%d POLLHUP=%d POLLNVAL=%d POLLRDNORM=%d POLLRDBAND=%d POLLWRNORM=%d POLLWRBAND=%d",
									(poll_fds[i].revents & POLLIN) > 0, (poll_fds[i].revents & POLLPRI) > 0, (poll_fds[i].revents & POLLOUT) > 0, (poll_fds[i].revents & POLLERR) > 0, (poll_fds[i].revents & POLLHUP) > 0, (poll_fds[i].revents & POLLNVAL) > 0, (poll_fds[i].revents & POLLRDNORM) > 0, (poll_fds[i].revents & POLLRDBAND) > 0, (poll_fds[i].revents & POLLWRNORM) > 0, (poll_fds[i].revents & POLLWRBAND) > 0);
						}

						console = (struct rtm_console *) list_find1(md->console_list, rtm_console_fd_test, &poll_fds[i].fd);
						if (console != NULL) {
							if (poll_fds[i].revents & (POLLERR | POLLNVAL)) {
								//TODO ??
								PRINT_DEBUG("todo: kinda error case that needs to be handled");
							} else if (poll_fds[i].revents & (POLLHUP)) {
								PRINT_IMPORTANT("Console closed: console=%p, id=%u", console, console->id);
								list_remove(md->console_list, console);
								console_free(console);

								md->console_fds[i] = 0;
							} else if (poll_fds[i].revents & (POLLIN | POLLRDNORM | POLLPRI | POLLRDBAND)) {
								cmd_len = (uint32_t) rtm_recv_fd(console->fd, MAX_CMD_LEN, cmd_buf);
								if (cmd_len != (uint32_t) -1) {
									cmd_buf[cmd_len] = '\0';
									rtm_process_cmd(module, console, cmd_len, cmd_buf);
								} else {
									PRINT_ERROR("todo error");
								}
							}
						} else {
							PRINT_ERROR("todo error");
							//console removed after poll started, before it returned, remove?
						}
					}
				}
			}
		} else {
			sem_post(&md->shared_sem);
			sleep(time);
			secure_sem_wait(&md->shared_sem);
		}
	}
	sem_post(&md->shared_sem);

	PRINT_IMPORTANT("Exited: module=%p", module);
	return NULL;
}

int rtm_recv_fd(int fd, uint32_t buf_len, uint8_t *buf) {
	PRINT_DEBUG("Entered: fd=%d, buf_len=%u, buf='%s'", fd, buf_len, buf);

	uint32_t msg_len;
	int numBytes = read(fd, &msg_len, sizeof(uint32_t));
	if (numBytes <= 0) {
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, numBytes);
		return numBytes;
	}

	if (numBytes != sizeof(uint32_t) || msg_len > buf_len) {
		PRINT_ERROR("todo error");
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, -1);
		return -1;
	}

	numBytes = read(fd, buf, msg_len);
	if (numBytes <= 0) {
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, numBytes);
		return numBytes;
	}

	if (msg_len != (uint32_t) numBytes) {
		PRINT_ERROR("todo error");
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, -1);
		return -1;
	}

	PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, numBytes);
	return numBytes;
}

int rtm_send_fd(int fd, uint32_t buf_len, uint8_t *buf) {
	PRINT_DEBUG("Entered: fd=%d, buf_len=%u, buf='%s'", fd, buf_len, buf);

	int numBytes = write(fd, &buf_len, sizeof(uint32_t));
	if (numBytes <= 0) {
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, numBytes);
		return numBytes;
	}

	if (numBytes != sizeof(uint32_t)) {
		PRINT_ERROR("todo error");
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, -1);
		return -1;
	}

	numBytes = write(fd, buf, buf_len);
	if (numBytes <= 0) {
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, numBytes);
		return numBytes;
	}

	if (buf_len != (uint32_t) numBytes) {
		PRINT_ERROR("todo error");
		PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, -1);
		return -1;
	}

	PRINT_DEBUG("Exited: fd=%d, buf_len=%u, buf='%s', %d", fd, buf_len, buf, numBytes);
	return numBytes;
}

int rtm_send_nack(int fd, uint32_t cmd_len, uint8_t *cmd_buf) {
	uint8_t msg[1000];
	memset(msg, 0, 1000);

	sprintf((char *) msg, "Unsupported:'%s'", cmd_buf);
	msg[cmd_len + 14] = '\0';
	PRINT_DEBUG("msg='%s'", msg);
	//TODO remove

	return rtm_send_fd(fd, cmd_len + 14, msg);
}

int rtm_send_error(int fd, const char *text, uint32_t buf_len, uint8_t *buf) {
	uint8_t msg[2000];
	memset(msg, 0, 2000);

	uint32_t text_len = strlen(text);

	sprintf((char *) msg, "%s:'%s'", text, buf);
	msg[text_len + buf_len + 3] = '\0';
	PRINT_DEBUG("msg='%s'", msg);
	//TODO remove

	return rtm_send_fd(fd, text_len + buf_len + 3, msg);
}

int rtm_send_text(int fd, const char *text) {
	uint32_t text_len = strlen(text);
	return rtm_send_fd(fd, text_len, (uint8_t *) text);
}

void rtm_send_fcf(struct fins_module *module, struct rtm_command *cmd, metadata *meta) {
	PRINT_DEBUG("Entered: module=%p, cmd=%p, meta=%p", module, cmd, meta);

	struct finsFrame *ff = (struct finsFrame *) secure_malloc(sizeof(struct finsFrame));
	ff->dataOrCtrl = FF_CONTROL;
	ff->destinationID = cmd->mod;
	ff->metaData = meta;

	ff->ctrlFrame.sender_id = module->index;
	ff->ctrlFrame.serial_num = cmd->serial_num;
	ff->ctrlFrame.opcode = cmd->op;
	ff->ctrlFrame.param_id = cmd->param_id;

	ff->ctrlFrame.data_len = 0;
	ff->ctrlFrame.data = NULL;

	PRINT_DEBUG("Sending ff=%p", ff);
	module_to_switch(module, ff);
}

void *switch_to_rtm(void *local) {
	struct fins_module *module = (struct fins_module *) local;

	PRINT_IMPORTANT("Entered: module=%p", module);

	while (module->state == FMS_RUNNING) {
		rtm_get_ff(module);
		PRINT_DEBUG("");
	}

	PRINT_IMPORTANT("Exited: module=%p", module);
	return NULL;
}

void rtm_get_ff(struct fins_module *module) {
	struct rtm_data *md = (struct rtm_data *) module->data;
	struct finsFrame *ff;

	do {
		secure_sem_wait(module->event_sem);
		secure_sem_wait(module->input_sem);
		ff = read_queue(module->input_queue);
		sem_post(module->input_sem);
	} while (module->state == FMS_RUNNING && ff == NULL && !md->interrupt_flag); //TODO change logic here, combine with switch_to_rtm?

	if (module->state != FMS_RUNNING) {
		if (ff != NULL) {
			freeFinsFrame(ff);
		}
		return;
	}

	if (ff != NULL) {
		if (ff->metaData == NULL) {
			PRINT_ERROR("Error fcf.metadata==NULL");
			exit(-1);
		}

		if (ff->dataOrCtrl == FF_CONTROL) {
			rtm_fcf(module, ff);
			PRINT_DEBUG("");
		} else if (ff->dataOrCtrl == FF_DATA) {
			if (ff->dataFrame.directionFlag == DIR_UP) {
				//rtm_in_fdf(module, ff);
				PRINT_ERROR("todo error");
				freeFinsFrame(ff);
			} else if (ff->dataFrame.directionFlag == DIR_DOWN) {
				//rtm_out_fdf(module, ff);
				PRINT_ERROR("todo error");
				freeFinsFrame(ff);
			} else {
				PRINT_ERROR("todo error");
				exit(-1);
			}
		} else {
			PRINT_ERROR("todo error");
			exit(-1);
		}
	} else if (md->interrupt_flag) {
		md->interrupt_flag = 0;

		rtm_interrupt(module); //TODO unused, implement or remove
	} else {
		PRINT_ERROR("todo error: dataOrCtrl=%u", ff->dataOrCtrl);
		exit(-1);
	}
}

void rtm_fcf(struct fins_module *module, struct finsFrame *ff) {
	PRINT_DEBUG("Entered: module=%p, ff=%p, meta=%p", module, ff, ff->metaData);

//TODO when recv FCF, pull params from meta to figure out connection, send through socket

	switch (ff->ctrlFrame.opcode) {
	case CTRL_ALERT:
		PRINT_DEBUG("opcode=CTRL_ALERT (%d)", CTRL_ALERT);
		PRINT_ERROR("todo");
		module_reply_fcf(module, ff, 0, 0);
		break;
	case CTRL_ALERT_REPLY:
		PRINT_DEBUG("opcode=CTRL_ALERT_REPLY (%d)", CTRL_ALERT_REPLY);
		PRINT_ERROR("todo");
		freeFinsFrame(ff);
		break;
	case CTRL_READ_PARAM:
		PRINT_DEBUG("opcode=CTRL_READ_PARAM (%d)", CTRL_READ_PARAM);
		PRINT_ERROR("todo");
		module_reply_fcf(module, ff, 0, 0);
		break;
	case CTRL_READ_PARAM_REPLY:
		PRINT_DEBUG("opcode=CTRL_READ_PARAM_REPLY (%d)", CTRL_READ_PARAM_REPLY);
		rtm_read_param_reply(module, ff);
		break;
	case CTRL_SET_PARAM:
		PRINT_DEBUG("opcode=CTRL_SET_PARAM (%d)", CTRL_SET_PARAM);
		rtm_set_param(module, ff);
		break;
	case CTRL_SET_PARAM_REPLY:
		PRINT_DEBUG("opcode=CTRL_SET_PARAM_REPLY (%d)", CTRL_SET_PARAM_REPLY);
		rtm_set_param_reply(module, ff);
		break;
	case CTRL_EXEC:
		PRINT_DEBUG("opcode=CTRL_EXEC (%d)", CTRL_EXEC);
		PRINT_ERROR("todo");
		module_reply_fcf(module, ff, 0, 0);
		break;
	case CTRL_EXEC_REPLY:
		PRINT_DEBUG("opcode=CTRL_EXEC_REPLY (%d)", CTRL_EXEC_REPLY);
		rtm_exec_reply(module, ff);
		break;
	case CTRL_ERROR:
		PRINT_DEBUG("opcode=CTRL_ERROR (%d)", CTRL_ERROR);
		PRINT_ERROR("todo");
		freeFinsFrame(ff);
		break;
	default:
		PRINT_DEBUG("opcode=default (%d)", ff->ctrlFrame.opcode);
		PRINT_ERROR("todo");
		exit(-1);
		break;
	}
}

void rtm_read_param_reply(struct fins_module *module, struct finsFrame *ff) {
	PRINT_DEBUG("Entered: module=%p, ff=%p, meta=%p", module, ff, ff->metaData);

}

void rtm_set_param(struct fins_module *module, struct finsFrame *ff) {
	PRINT_DEBUG("Entered: module=%p, ff=%p, meta=%p", module, ff, ff->metaData);

	switch (ff->ctrlFrame.param_id) {
	case RTM_SET_PARAM_FLOWS:
		PRINT_DEBUG("RTM_SET_PARAM_FLOWS");
		module_set_param_flows(module, ff);
		break;
	case RTM_SET_PARAM_LINKS:
		PRINT_DEBUG("RTM_SET_PARAM_LINKS");
		module_set_param_links(module, ff);
		break;
	case RTM_SET_PARAM_DUAL:
		PRINT_DEBUG("RTM_SET_PARAM_DUAL");
		module_set_param_dual(module, ff);
		break;
	default:
		PRINT_DEBUG("param_id=default (%d)", ff->ctrlFrame.param_id);
		PRINT_ERROR("todo");
		module_reply_fcf(module, ff, 0, 0);
		break;
	}
}

void rtm_set_param_reply(struct fins_module *module, struct finsFrame *ff) {
	PRINT_DEBUG("Entered: module=%p, ff=%p, meta=%p", module, ff, ff->metaData);
	struct rtm_data *md = (struct rtm_data *) module->data;

	secure_sem_wait(&md->shared_sem);
	struct rtm_command *cmd = (struct rtm_command *) list_find1(md->cmd_list, rtm_cmd_serial_test, &ff->ctrlFrame.serial_num);
	if (cmd != NULL) {
		list_remove(md->cmd_list, cmd);

		struct rtm_console *console = (struct rtm_console *) list_find1(md->console_list, rtm_console_id_test, &cmd->console_id);
		if (console != NULL) {
			//TODO extract answer
			if (ff->ctrlFrame.ret_val) {
				//send '' ?
				rtm_send_text(console->fd, "successful");
			} else {
				//send error
				rtm_send_text(console->fd, "unsuccessful");
			}
		} else {
			PRINT_ERROR("todo error");
		}
		sem_post(&md->shared_sem);

		free(cmd);
	} else {
		sem_post(&md->shared_sem);
		PRINT_ERROR("todo error");
		//TODO error, drop
		freeFinsFrame(ff);
	}
}

void rtm_exec_reply(struct fins_module *module, struct finsFrame *ff) {
	PRINT_DEBUG("Entered: module=%p, ff=%p, meta=%p", module, ff, ff->metaData);
}

void rtm_interrupt(struct fins_module *module) {
	struct rtm_data *md = (struct rtm_data *) module->data;

	if (md != NULL) {
		//TODO for any timers/TOs that we eventually implement
	}
}

void rtm_init_params(struct fins_module *module) {
	metadata_element *root = config_root_setting(module->params);
	metadata_element *exec_elem = config_setting_add(root, "exec", CONFIG_TYPE_GROUP);
	if (exec_elem == NULL) {
		PRINT_DEBUG("todo error");
		exit(-1);
	}

	metadata_element *get_elem = config_setting_add(root, "get", CONFIG_TYPE_GROUP);
	if (get_elem == NULL) {
		PRINT_DEBUG("todo error");
		exit(-1);
	}

	metadata_element *set_elem = config_setting_add(root, "set", CONFIG_TYPE_GROUP);
	if (set_elem == NULL) {
		PRINT_DEBUG("todo error");
		exit(-1);
	}

	metadata_element *sub = config_setting_add(exec_elem, "test", CONFIG_TYPE_GROUP);
	if (sub == NULL) {
		PRINT_DEBUG("todo error");
		exit(-1);
	}

	metadata_element *elem = config_setting_add(sub, "key", CONFIG_TYPE_INT);
	if (elem == NULL) {
		PRINT_DEBUG("todo error");
		exit(-1);
	}

	uint32_t value = 10;
	int status = config_setting_set_int(elem, *(int *) &value);
	if (status == CONFIG_FALSE) {
		PRINT_DEBUG("todo error");
		exit(-1);
	}
}

int rtm_init(struct fins_module *module, uint32_t flows_num, uint32_t *flows, metadata_element *params, struct envi_record *envi) {
	PRINT_IMPORTANT("Entered: module=%p, params=%p, envi=%p", module, params, envi);
	module->state = FMS_INIT;
	module_create_structs(module);

	rtm_init_params(module);

	module->data = secure_malloc(sizeof(struct rtm_data));
	struct rtm_data *md = (struct rtm_data *) module->data;

	if (module->flows_max < flows_num) {
		PRINT_ERROR("todo error");
		return 0;
	}
	md->flows_num = flows_num;

	int i;
	for (i = 0; i < flows_num; i++) {
		md->flows[i] = flows[i];
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(struct sockaddr_un));
	int32_t size = sizeof(addr);

	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, UNIX_PATH_MAX, RTM_PATH);
	unlink(addr.sun_path);

	md->server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (md->server_fd < 0) {
		PRINT_ERROR("socket error: server_fd=%d, errno=%u, str='%s'", md->server_fd, errno, strerror(errno));
		return 0;
	}

	PRINT_DEBUG("binding to: addr='%s'", RTM_PATH);
	if (bind(md->server_fd, (struct sockaddr *) &addr, size) < 0) {
		PRINT_ERROR("bind error: server_fd=%d, errno=%u, str='%s'", md->server_fd, errno, strerror(errno));
		return 0;
	}
	if (listen(md->server_fd, 10) < 0) {
		PRINT_ERROR("listen error: server_fd=%d, errno=%u, str='%s'", md->server_fd, errno, strerror(errno));
		return 0;
	}

	sem_init(&md->shared_sem, 0, 1);
	for (i = 0; i < MAX_CONSOLES; i++) {
		md->console_fds[i] = 0;
	}

	md->console_list = list_create(MAX_CONSOLES);
	md->console_counter = 0;

	md->cmd_list = list_create(MAX_COMMANDS);
	md->cmd_counter = 0;

	return 1;
}

int rtm_run(struct fins_module *module, pthread_attr_t *attr) {
	PRINT_IMPORTANT("Entered: module=%p, attr=%p", module, attr);
	module->state = FMS_RUNNING;

	struct rtm_data *md = (struct rtm_data *) module->data;
	secure_pthread_create(&md->accept_console_thread, attr, accept_console, module);
	secure_pthread_create(&md->console_to_rtm_thread, attr, console_to_rtm, module);
	secure_pthread_create(&md->switch_to_rtm_thread, attr, switch_to_rtm, module);
	return 1;
}

int rtm_pause(struct fins_module *module) {
	PRINT_IMPORTANT("Entered: module=%p", module);
	module->state = FMS_PAUSED;

//TODO
	return 1;
}

int rtm_unpause(struct fins_module *module) {
	PRINT_IMPORTANT("Entered: module=%p", module);
	module->state = FMS_RUNNING;

//TODO
	return 1;
}

int rtm_shutdown(struct fins_module *module) {
	PRINT_IMPORTANT("Entered: module=%p", module);
	module->state = FMS_SHUTDOWN;
	sem_post(module->event_sem);

	struct rtm_data *md = (struct rtm_data *) module->data;
	close(md->server_fd);

	PRINT_IMPORTANT("Joining accept_console_thread");
	pthread_join(md->accept_console_thread, NULL);
	PRINT_IMPORTANT("Joining console_to_rtm_thread");
	pthread_join(md->console_to_rtm_thread, NULL);
	PRINT_IMPORTANT("Joining switch_to_rtm_thread");
	pthread_join(md->switch_to_rtm_thread, NULL);

	return 1;
}

int rtm_release(struct fins_module *module) {
	PRINT_IMPORTANT("Entered: module=%p", module);

	struct rtm_data *md = (struct rtm_data *) module->data;
	list_free(md->console_list, console_free);
	list_free(md->cmd_list, free);

	if (md->link_list != NULL) {
		list_free(md->link_list, free);
	}
	free(md);
	module_destroy_structs(module);
	free(module);
	return 1;
}

int rtm_register_module(struct fins_module *module, struct fins_module *new_mod) {
	PRINT_DEBUG("Entered: module=%p, new_mod=%p, id=%d, name='%s'", module, new_mod, new_mod->id, new_mod->name);
	struct rtm_data *md = (struct rtm_data *) module->data;

	if (new_mod->index >= MAX_MODULES) {
		PRINT_ERROR("todo error");
		return -1;
	}

	if (md->overall->modules[new_mod->index] != NULL) {
		PRINT_IMPORTANT("Replacing: mod=%p, id=%d, name='%s'",
				md->overall->modules[new_mod->index], md->overall->modules[new_mod->index]->id, md->overall->modules[new_mod->index]->name);
	}
	PRINT_IMPORTANT("Registered: new_mod=%p, id=%d, name='%s'", new_mod, new_mod->id, new_mod->name);
	md->overall->modules[new_mod->index] = new_mod;

	PRINT_DEBUG("Exited: module=%p, new_mod=%p, id=%d, name='%s'", module, new_mod, new_mod->id, new_mod->name);
	return 0;
}

int rtm_unregister_module(struct fins_module *module, int index) {
	PRINT_DEBUG("Entered: module=%p, index=%d", module, index);
	struct rtm_data *md = (struct rtm_data *) module->data;

	if (index < 0 || index > MAX_MODULES) {
		PRINT_ERROR("todo error");
		return 0;
	}

	if (md->overall->modules[index] != NULL) {
		PRINT_IMPORTANT("Unregistering: mod=%p, id=%d, name='%s'",
				md->overall->modules[index], md->overall->modules[index]->id, md->overall->modules[index]->name);
		md->overall->modules[index] = NULL;
	} else {
		PRINT_IMPORTANT("No module to unregister: index=%d", index);
	}

	return 1;
}

int rtm_pass_overall(struct fins_module *module, struct fins_overall *overall) {
	PRINT_DEBUG("Entered: module=%p, overall=%p", module, overall);
	struct rtm_data *md = (struct rtm_data *) module->data;

	md->overall = overall;

	return 1;
}

void rtm_dummy(void) {

}

static struct fins_module_admin_ops rtm_ops = { .init = rtm_init, .run = rtm_run, .pause = rtm_pause, .unpause = rtm_unpause, .shutdown = rtm_shutdown,
		.release = rtm_release, .pass_overall = rtm_pass_overall };

struct fins_module *rtm_create(uint32_t index, uint32_t id, uint8_t *name) {
	PRINT_IMPORTANT("Entered: index=%u, id=%u, name='%s'", index, id, name);

	struct fins_module *module = (struct fins_module *) secure_malloc(sizeof(struct fins_module));

	strcpy((char *) module->lib, RTM_LIB);
	module->flows_max = RTM_MAX_FLOWS;
	module->ops = (struct fins_module_ops *) &rtm_ops;
	module->state = FMS_FREE;

	module->index = index;
	module->id = id;
	strcpy((char *) module->name, (char *) name);

	PRINT_IMPORTANT("Exited: index=%u, id=%u, name='%s', module=%p", index, id, name, module);
	return module;
}
