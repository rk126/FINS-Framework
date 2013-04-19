/*
 * 		@file socketgeni.c
 * *  	@date Nov 26, 2010
 *      @author Abdallah Abdallah
 *      @brief This is the FINS CORE including (the Daemon name pipes based
 *      server)
 *      notice that A read call will normally block; that is, it will cause the process to
 *       wait until data becomes available. If the other end of the pipe has been closed,
 *       then no process has the pipe open for writing, and the read blocks. Because this isn’t
 *       very helpful, a read on a pipe that isn’t open for writing returns zero rather than
 *       blocking. This allows the reading process to detect the pipe equivalent of end of file
 *       and act appropriately. Notice that this isn’t the same as reading an invalid file
 *       descriptor, which read considers an error and indicates by returning –1.
 *       */
#include "core.h"

#include <signal.h>

#include <finsdebug.h>
//#include <finstypes.h>
#include <finstime.h>
//#include <metadata.h>
//#include <finsqueue.h>

#include <switch.h>
#include <dlfcn.h>
//#include <daemon.h>
//#include <interface.h>
//#include <ipv4.h>
//#include <arp.h>
//#include <udp.h>
//#include <tcp.h>
//#include <icmp.h>
//#include <rtm.h>
//#include <logger.h>

extern sem_t control_serial_sem; //TODO remove & change gen process to RNG

struct fins_module *switch_module; //TODO if move fins_modules entirely to switch, use this to get to them, remove otherwise

/**
 * @brief read the core parameters from the configuraions file called fins.cfg
 * @param
 * @return nothing
 */
int read_configurations() {
	//const char *str;

	//config_t cfg;
	//config_init(&cfg);

	metadata *meta = (metadata *) secure_malloc(sizeof(metadata)); //equivalent
	metadata_create(meta);

	/* Read the file. If there is an error, report it and exit. */
	if (!config_read_file(meta, "test.cfg")) {
		PRINT_ERROR("%s:%d - %s\n", config_error_file(meta), config_error_line(meta), config_error_text(meta));
		config_destroy(meta);
		return EXIT_FAILURE;
	}

	metadata_print(meta);

	//int config_setting_lookup_int64 (const config_setting_t * setting, const char * name, long long * value)
	//int config_lookup_int64 (const config_t * config, const char * path, long long * value)
	/*
	 int var1;
	 double var2;
	 const char *var3;

	 //config_lookup_int64
	 if (config.lookupValue("values.var1", var1) && config.lookupValue("values.var2", var2) && config.lookupValue("values.var3", var3)) {
	 // use var1, var2, var3
	 } else {
	 // error handling here
	 }

	 long width = config.lookup("application.window.size.w");

	 bool splashScreen = config.lookup("application.splash_screen");

	 std::string title = config.lookup("application.window.title");
	 title = (const char *)config.lookup("application.window.title");
	 */

	config_destroy(meta);
	return EXIT_SUCCESS;
}

int write_configurations() {

	config_t cfg;
	//config_setting_t *setting;
	//const char *str;

	config_init(&cfg);

	/* Read the file. If there is an error, report it and exit. */
	if (!config_write_file(&cfg, "fins.cfg")) {
		PRINT_ERROR("%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
		config_destroy(&cfg);
		return EXIT_FAILURE;
	}

	config_destroy(&cfg);
	return EXIT_SUCCESS;
}

void core_termination_handler(int sig) {
	PRINT_IMPORTANT("**********Terminating *******");

	//shutdown all module threads in backwards order of startup
	//logger_shutdown();
	//rtm_shutdown();

	//udp_shutdown();
	//tcp_shutdown();
	//icmp_shutdown();
	//ipv4_shutdown();
	//arp_shutdown();

	//interface_shutdown(); //TODO finish
	//daemon_shutdown(); //TODO finish
	//switch_shutdown(); //TODO finish

	//have each module free data & que/sem //TODO finish each of these
	//logger_release();
	//rtm_release();

	//udp_release();
	//tcp_release();
	//icmp_release();
	//ipv4_release();
	//arp_release();

	//interface_release();
	//daemon_release();
	//switch_release();

	sem_destroy(&control_serial_sem);

	PRINT_IMPORTANT("FIN");
	exit(-1);
}

void core_dummy(void) {

}

void set_addr4(struct sockaddr_storage *addr, uint32_t val) {
	struct sockaddr_in *addr4 = (struct sockaddr_in *) addr;
	memset(addr4, 0, sizeof(struct sockaddr_in));
	addr4->sin_family = AF_INET;
	addr4->sin_addr.s_addr = val;
}

int addr_is_addr4(struct addr_record *addr) {
	return addr->family == AF_INET;
}

void set_addr6(struct sockaddr_storage *addr, uint32_t val) {
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) addr;
	memset(addr6, 0, sizeof(struct sockaddr_in6));
	addr6->sin6_family = AF_INET6;
	//addr6->sin6_addr.s_addr = val;
}

int addr_is_addr6(struct addr_record *addr) {
	return addr->family == AF_INET6;
}

int ifr_index_test(struct if_record *ifr, uint32_t *index) {
	return ifr->index == *index;
}

int lib_name_test(struct fins_library *lib, uint8_t *name) {
	return strcmp((char *) lib->name, (char *) name) == 0;
}

int mod_id_test(struct fins_module *mod, uint32_t *id) {
	return mod->id == *id;
}

int link_involved_test(struct link_record *link, uint32_t *index) {
	if (link->src_index == *index) {
		return 1;
	} else {
		int i;
		for (i = 0; i < link->dsts_num; i++) {
			if (link->dsts_index[i] == *index) {
				return 1;
			}
		}
		return 0;
	}
}

struct fins_library *library_load(uint8_t *lib, uint8_t *base_path) {
	PRINT_IMPORTANT("Entered: lib='%s', base_path='%s'", lib, base_path);

	struct fins_library *library = (struct fins_library *) secure_malloc(sizeof(struct fins_library));
	strcpy((char *) library->name, (char *) lib);

	uint8_t *error;
	uint8_t lib_path[MAX_BASE_PATH + MOD_NAME_SIZE + 7]; // +7 for "/lib<>.so"
	sprintf((char *) lib_path, "%s/lib%s.so", (char *) base_path, (char *) lib);
	library->handle = dlopen((char *) lib_path, RTLD_NOW); //RTLD_LAZY | RTLD_GLOBAL?
	if (library->handle == NULL) {
		fputs(dlerror(), stderr);
		PRINT_IMPORTANT("Entered: lib='%s', base_path='%s', library=%p", lib, base_path, NULL);
		return NULL;
	}

	uint8_t lib_create[MOD_NAME_SIZE + 7]; // +7 for "_create"
	sprintf((char *) lib_create, "%s_create", (char *) lib);
	library->create = (mod_create_type) dlsym(library->handle, (char *) lib_create);
	if ((error = (uint8_t *) dlerror()) != NULL) {
		fputs((char *) error, stderr);
		PRINT_IMPORTANT("Entered: lib='%s', base_path='%s', library=%p", lib, base_path, NULL);
		return NULL;
	}

	library->num_mods = 0;

	PRINT_IMPORTANT("Entered: lib='%s', base_path='%s', library=%p", lib, base_path, library);
	return library;
}
//################################ move above code to finsmodule.h or maybe finstypes.h

void core_main() {
	PRINT_IMPORTANT("Entered");

	register_to_signal(SIGRTMIN);

	sem_init(&control_serial_sem, 0, 1); //TODO remove after gen_control_serial_num() converted to RNG

	signal(SIGINT, core_termination_handler); //register termination handler

	int status;
	int i, j, k;

	//######################################################################
	struct envi_record *envi = (struct envi_record *) secure_malloc(sizeof(struct envi_record));

	PRINT_IMPORTANT("loading environment");
	metadata *meta_envi = (metadata *) secure_malloc(sizeof(metadata));
	metadata_create(meta_envi);

	status = config_read_file(meta_envi, "envi.cfg");
	if (status == META_FALSE) {
		PRINT_ERROR("%s:%d - %s\n", config_error_file(meta_envi), config_error_line(meta_envi), config_error_text(meta_envi));
		metadata_destroy(meta_envi);
		PRINT_ERROR("todo error");
		exit(-1);
	}

	//############# any ip address
	PRINT_IMPORTANT("Any Addr");
	envi->any_ip_addr = IP4_ADR_P2H(0,0,0,0); //TODO change to addr_in?
	//TODO get from environment.any_addr

	//############# if_list
	PRINT_IMPORTANT("interface list");
	envi->if_list = list_create(MAX_INTERFACES);

	metadata_element *list_elem = config_lookup(meta_envi, "environment.interfaces");
	if (list_elem == NULL) {
		PRINT_ERROR("todo error");
		exit(-1);
	}
	int list_num = config_setting_length(list_elem);

	metadata_element *elem;
	uint32_t if_index;
	uint8_t *name;
	uint64_t mac;
	uint32_t type;
	uint32_t if_status;
	uint32_t mtu;
	uint32_t flags;

	struct if_record *ifr;

	for (i = 0; i < list_num; i++) {
		elem = config_setting_get_elem(list_elem, i);
		if (elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "index", (int *) &if_index);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_string(elem, "name", (const char **) &name);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int64(elem, "mac", (long long *) &mac);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "type", (int *) &type);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "status", (int *) &if_status);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "mtu", (int *) &mtu);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "flags", (int *) &flags);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		//#############
		ifr = (struct if_record *) list_find1(envi->if_list, ifr_index_test, &if_index);
		if (ifr == NULL) {
			ifr = (struct if_record *) secure_malloc(sizeof(struct if_record));
			ifr->index = if_index;
			strcpy((char *) ifr->name, (char *) name);
			ifr->mac = mac;
			ifr->type = (uint16_t) type;

			ifr->status = (uint8_t) if_status;
			ifr->mtu = mtu;
			ifr->flags = flags;

			ifr->addr_list = list_create(MAX_FAMILIES);

			if (list_has_space(envi->if_list)) {
				list_append(envi->if_list, ifr);
			} else {
				//TODO error
				PRINT_ERROR("todo error");
				exit(-1);
			}

			if (flags & IFF_LOOPBACK) {
				envi->if_loopback = ifr;
			}
		} else {
			PRINT_ERROR("todo error");
			exit(-1);
		}
	}

	//############# if_main
	PRINT_IMPORTANT("main interface");
	uint32_t if_main;

	status = config_lookup_int(meta_envi, "environment.main_interface", (int *) &if_main);
	if (status == META_FALSE) {
		PRINT_ERROR("todo error");
		exit(-1);
	}

	envi->if_main = (struct if_record *) list_find1(envi->if_list, ifr_index_test, &if_main);
	if (envi->if_main == NULL) {
		PRINT_ERROR("todo");
	}

	//############# addr_list
	PRINT_IMPORTANT("address list");
	//envi->addr_list = list_create(MAX_INTERFACES * MAX_FAMILIES); //TODO use?

	list_elem = config_lookup(meta_envi, "environment.addresses");
	if (list_elem == NULL) {
		PRINT_ERROR("todo error");
		exit(-1);
	}
	list_num = config_setting_length(list_elem);

	uint32_t family; //atm only AF_INET, but eventually also AF_INET6

	metadata_element *ip_elem;
	uint32_t ip_num;

	uint32_t ip[4]; //SIOCGIFADDR //ip
	uint32_t mask[4]; //SIOCGIFNETMASK //mask
	uint32_t gw[4]; //? //(ip & mask) | 1;
	uint32_t bdc[4]; //SIOCGIFBRDADDR //(ip & mask) | ~mask
	uint32_t dst[4]; //SIOCGIFDSTADDR //dst

	struct addr_record *addr;

	for (i = 0; i < list_num; i++) {
		elem = config_setting_get_elem(list_elem, i);
		if (elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "if_index", (int *) &if_index);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "family", (int *) &family);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		ip_elem = config_setting_get_member(elem, "ip");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			ip[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		ip_elem = config_setting_get_member(elem, "mask");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			mask[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		ip_elem = config_setting_get_member(elem, "gw");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			gw[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		ip_elem = config_setting_get_member(elem, "bdc");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			bdc[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		ip_elem = config_setting_get_member(elem, "dst");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			dst[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		//############
		ifr = (struct if_record *) list_find1(envi->if_list, ifr_index_test, &if_index);
		if (ifr) {
			if (ifr->flags & IFF_RUNNING) {
				if (family == AF_INET) {
					addr = (struct addr_record *) list_find(ifr->addr_list, addr_is_addr4);
				} else {
					addr = (struct addr_record *) list_find(ifr->addr_list, addr_is_addr6);
				}

				if (addr == NULL) {
					addr = (struct addr_record *) secure_malloc(sizeof(struct addr_record));
					addr->if_index = if_index;
					addr->family = AF_INET;

					if (family == AF_INET) {
						set_addr4(&addr->ip, IP4_ADR_P2H(ip[0], ip[1], ip[2],ip[3]));
						set_addr4(&addr->mask, IP4_ADR_P2H(mask[0], mask[1], mask[2],mask[3]));
						set_addr4(&addr->gw, IP4_ADR_P2H(gw[0], gw[1], gw[2], gw[3]));
						set_addr4(&addr->bdc, IP4_ADR_P2H(bdc[0], bdc[1], bdc[2], bdc[3]));
						set_addr4(&addr->dst, IP4_ADR_P2H(dst[0], dst[1], dst[2], dst[3]));
					} else if (family == AF_INET6) {
						//TODO
						//set_addr6(&addr->ip, ip);
						PRINT_ERROR("todo");
					} else {
						//TODO error?
						PRINT_ERROR("todo error");
						exit(-1);
					}

					if (list_has_space(ifr->addr_list)) {
						list_append(ifr->addr_list, addr);
					} else {
						//TODO error
						PRINT_ERROR("todo error");
						exit(-1);
					}
				} else {
					//TODO error
					PRINT_ERROR("todo: replace or add new?");
				}
			} else {
				//TODO error
				PRINT_ERROR("todo: decide just drop or add?");
			}
		} else {
			//TODO error
			PRINT_ERROR("todo error");
			exit(-1);
		}
	}

	//############# route_list
	PRINT_IMPORTANT("route list");
	envi->route_list = list_create(MAX_ADDRESSES);

	list_elem = config_lookup(meta_envi, "environment.routes");
	if (list_elem == NULL) {
		PRINT_ERROR("todo error");
		exit(-1);
	}
	list_num = config_setting_length(list_elem);

	uint32_t metric; //SIOCGIFMETRIC
	uint32_t timeout;
	//struct timeval route_stamp;

	struct route_record *route;

	for (i = 0; i < list_num; i++) {
		elem = config_setting_get_elem(list_elem, i);
		if (elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "if_index", (int *) &if_index);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "family", (int *) &family);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		ip_elem = config_setting_get_member(elem, "dst");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			dst[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		ip_elem = config_setting_get_member(elem, "mask");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			mask[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}

		ip_elem = config_setting_get_member(elem, "gw");
		if (ip_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		ip_num = config_setting_length(ip_elem);

		for (j = 0; j < ip_num; j++) {
			gw[j] = (uint32_t) config_setting_get_int_elem(ip_elem, j);
		}
		////ip = IP4_ADR_P2H(192,168,1,5);

		status = config_setting_lookup_int(elem, "metric", (int *) &metric);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(elem, "timeout", (int *) &timeout);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		//############
		ifr = (struct if_record *) list_find1(envi->if_list, ifr_index_test, &if_index);
		if (ifr) {
			if (ifr->flags & IFF_RUNNING) {
				route = (struct route_record *) secure_malloc(sizeof(struct route_record));
				route->if_index = if_index;
				route->family = family;

				if (family == AF_INET) {
					set_addr4(&addr->dst, IP4_ADR_P2H(dst[0], dst[1], dst[2], dst[3]));
					set_addr4(&addr->mask, IP4_ADR_P2H(mask[0], mask[1], mask[2],mask[3]));
					set_addr4(&addr->gw, IP4_ADR_P2H(gw[0], gw[1], gw[2], gw[3]));
					//set_addr4(&addr->ip, IP4_ADR_P2H(ip[0], ip[1], ip[2],ip[3]));
				} else if (family == AF_INET6) {
					//TODO
					//set_addr6(&route->ip, ip);
				} else {
					//TODO error?
				}

				route->metric = metric;
				route->timeout = timeout;

				if (list_has_space(envi->route_list)) {
					list_append(envi->route_list, route);
				} else {
					//TODO error
				}
			} else {
				//TODO error
			}
		}
	}

	//######################################################################
	PRINT_IMPORTANT("loading stack");
	metadata *meta_stack = (metadata *) secure_malloc(sizeof(metadata));
	metadata_create(meta_stack);

	status = config_read_file(meta_stack, "stack.cfg");
	if (status == META_FALSE) {
		PRINT_ERROR("%s:%d - %s\n", config_error_file(meta_stack), config_error_line(meta_stack), config_error_text(meta_stack));
		metadata_destroy(meta_stack);
		PRINT_ERROR("todo error");
		exit(-1);
	}

	//############# module_list
	PRINT_IMPORTANT("module list");
	struct linked_list *lib_list = list_create(MAX_MODULES);
	struct fins_module *fins_modules[MAX_MODULES];
	memset(fins_modules, 0, MAX_MODULES * sizeof(struct fins_module *));

	uint8_t base_path[100];
	memset((char *) base_path, 0, 100);
	strcpy((char *) base_path, ".");

	metadata_element *mods_elem = config_lookup(meta_stack, "stack.modules");
	if (mods_elem == NULL) {
		PRINT_ERROR("todo error");
		exit(-1);
	}
	int mods_num = config_setting_length(mods_elem);

	metadata_element *mod_elem;
	uint32_t mod_id;
	uint8_t *mod_lib;
	uint8_t *mod_name;
	metadata_element *flows_elem;
	uint32_t mod_flows[MAX_FLOWS];
	uint32_t mod_flows_num;
	metadata_element *mod_params;

	struct fins_library *library;
	struct fins_module *module;

	for (i = 0; i < mods_num; i++) {
		mod_elem = config_setting_get_elem(mods_elem, i);
		if (mod_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(mod_elem, "id", (int *) &mod_id);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_string(mod_elem, "lib", (const char **) &mod_lib);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_string(mod_elem, "name", (const char **) &mod_name);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		flows_elem = config_setting_get_member(mod_elem, "flows");
		if (flows_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		mod_flows_num = config_setting_length(flows_elem);

		for (j = 0; j < mod_flows_num; j++) {
			mod_flows[j] = (uint32_t) config_setting_get_int_elem(flows_elem, j);
		}

		mod_params = config_setting_get_member(mod_elem, "params");
		if (mod_params == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		//############
		//library = library_get(lib_list, mod_lib, base_path);if (library == NULL) {PRINT_ERROR("todo error");exit(-1);}
		library = (struct fins_library *) list_find1(lib_list, lib_name_test, mod_lib);
		if (library == NULL) {
			library = library_load(mod_lib, base_path);
			if (library == NULL) {
				PRINT_ERROR("todo error");
				exit(-1);
			}
		}

		module = library->create(i, mod_id, mod_name);
		if (module == NULL) {
			//TODO error
			PRINT_ERROR("todo error");
			exit(-1);
		}
		library->num_mods++;

		//TODO move flow to update? or links here?
		status = module->ops->init(module, mod_flows, mod_flows_num, mod_params, envi); //TODO merge init into create?
		if (status) {
			if (i == SWITCH_INDEX) {
				switch_module = module; //TODO remove? unnecessary
			}
			fins_modules[i] = module;
			switch_register_module(fins_modules[SWITCH_INDEX], module);
		} else {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		free(mod_lib);
		free(mod_name);
	}

	//############# linking_list
	PRINT_IMPORTANT("link list");
	struct linked_list *link_list = list_create(MAX_LINKS);

	metadata_element *links_elem = config_lookup(meta_stack, "stack.links");
	if (links_elem == NULL) {
		PRINT_ERROR("todo error");
		exit(-1);
	}
	int links_num = config_setting_length(links_elem);

	metadata_element *link_elem;
	uint32_t link_id;
	uint32_t link_src;
	metadata_element *dsts_elem;
	uint32_t link_dsts[MAX_MODULES];
	int link_dsts_num;

	struct link_record *link;

	for (i = 0; i < links_num; i++) {
		link_elem = config_setting_get_elem(links_elem, i);
		if (link_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(link_elem, "id", (int *) &link_id);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		status = config_setting_lookup_int(link_elem, "src", (int *) &link_src);
		if (status == META_FALSE) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		dsts_elem = config_setting_get_member(link_elem, "dsts");
		if (dsts_elem == NULL) {
			PRINT_ERROR("todo error");
			exit(-1);
		}
		link_dsts_num = config_setting_length(dsts_elem);

		for (j = 0; j < link_dsts_num; j++) {
			link_dsts[j] = (uint32_t) config_setting_get_int_elem(dsts_elem, j);
		}

		//############
		link = (struct link_record *) secure_malloc(sizeof(struct link_record));
		link->id = link_id;

		//module = (struct fins_module *) list_find1(envi->module_list, mod_id_test, &link_src);
		link->src_index = -1;
		for (j = 0; j < MAX_MODULES; j++) {
			if (fins_modules[j] != NULL && fins_modules[j]->id == link_src) {
				link->src_index = fins_modules[j]->index;
			}
		}
		if (link->src_index == -1) {
			PRINT_ERROR("todo error");
			exit(-1);
		}

		link->dsts_num = link_dsts_num;
		for (j = 0; j < link_dsts_num; j++) {
			//module = (struct fins_module *) list_find1(envi->module_list, mod_id_test, &link_dsts[j]);
			link->dsts_index[j] = -1;
			for (k = 0; k < MAX_MODULES; k++) {
				if (fins_modules[k] != NULL && fins_modules[k]->id == link_dsts[j]) {
					link->dsts_index[j] = fins_modules[k]->index;
				}
			}
			if (link->dsts_index[j] == (uint32_t) -1) {
				PRINT_ERROR("todo error");
				exit(-1);
			}
		}

		if (list_has_space(link_list)) {
			list_append(link_list, link);
		} else {
			//TODO error
			PRINT_ERROR("todo error");
			exit(-1);
		}
	}

	//############# update
	PRINT_IMPORTANT("update modules");
	//send out subset of linking table to each module as update
	//TODO table subset update

	struct linked_list *link_subset_list;
	metadata *meta_update;
	struct finsFrame *ff_update;

	for (i = 0; i < MAX_MODULES; i++) {
		if (fins_modules[i] != NULL) {
			link_subset_list = list_filter1(link_list, link_involved_test, &fins_modules[i]->index, link_copy);
			PRINT_IMPORTANT("i=%d, link_subset_list=%p, len=%d", i, link_subset_list, link_subset_list->len);

			meta_update = (metadata *) secure_malloc(sizeof(metadata));
			metadata_create(meta_update);

			//TODO decide on metadata params?
			//uint32_t host_ip = IP4_ADR_P2H(192,168,1,8);
			//secure_metadata_writeToElement(meta_update, "send_src_ip", &host_ip, META_TYPE_INT32);

			ff_update = (struct finsFrame*) secure_malloc(sizeof(struct finsFrame));
			ff_update->dataOrCtrl = CONTROL;
			ff_update->destinationID = i;
			ff_update->metaData = meta_update;

			ff_update->ctrlFrame.senderID = SWITCH_INDEX;
			ff_update->ctrlFrame.serial_num = gen_control_serial_num();
			ff_update->ctrlFrame.opcode = CTRL_SET_PARAM;
			ff_update->ctrlFrame.param_id = PARAM_LINKS;

			ff_update->ctrlFrame.data_len = sizeof(struct linked_list);
			ff_update->ctrlFrame.data = (uint8_t *) link_subset_list;

			if (module_to_switch(fins_modules[SWITCH_INDEX], ff_update)) {

			} else {
				PRINT_ERROR("todo error");
				freeFinsFrame(ff_update);
				list_free(link_subset_list, free);
				exit(-1);
			}
		}
	}

	//############ say by this point envi var completely init'd
	//assumed always connect/init to switch first

	pthread_attr_t attr;
	pthread_attr_init(&attr);

	PRINT_IMPORTANT("modules: run");

	for (i = 0; i < MAX_MODULES; i++) {
		if (fins_modules[i] != NULL) {
			fins_modules[i]->ops->run(fins_modules[i], &attr);
		}
	}

	//############ mini test
	//sleep(5);
	char recv_data[4000];

	//while (1) {
	PRINT_IMPORTANT("waiting...");
	gets(recv_data);

	if (0) {
		metadata *meta = (metadata *) secure_malloc(sizeof(metadata));
		metadata_create(meta);

		uint32_t host_ip = IP4_ADR_P2H(192,168,1,8);
		uint32_t host_port = 55454;
		uint32_t dst_ip = IP4_ADR_P2H(192,168,1,3);
		uint32_t dst_port = 44444;
		uint32_t ttl = 64;
		uint32_t tos = 64;

		secure_metadata_writeToElement(meta, "send_src_ip", &host_ip, META_TYPE_INT32);
		secure_metadata_writeToElement(meta, "send_src_port", &host_port, META_TYPE_INT32);
		secure_metadata_writeToElement(meta, "send_dst_ip", &dst_ip, META_TYPE_INT32);
		secure_metadata_writeToElement(meta, "send_dst_port", &dst_port, META_TYPE_INT32);
		secure_metadata_writeToElement(meta, "send_ttl", &ttl, META_TYPE_INT32);
		secure_metadata_writeToElement(meta, "send_tos", &tos, META_TYPE_INT32);

		struct finsFrame *ff = (struct finsFrame *) secure_malloc(sizeof(struct finsFrame));
		ff->dataOrCtrl = DATA;
		ff->destinationID = 2;
		ff->metaData = meta;

		ff->dataFrame.directionFlag = DIR_UP;
		ff->dataFrame.pduLength = 10;
		ff->dataFrame.pdu = (uint8_t *) secure_malloc(10);

		PRINT_IMPORTANT("sending: ff=%p, meta=%p", ff, meta);
		if (module_to_switch(fins_modules[3], ff)) {
			//i++;
		} else {
			PRINT_ERROR("freeing: ff=%p", ff);
			freeFinsFrame(ff);
			return;
		}
	}

	if (1) {
		PRINT_DEBUG("Sending ARP req");

		metadata *meta_req = (metadata *) secure_malloc(sizeof(metadata));
		metadata_create(meta_req);

		uint32_t dst_ip = IP4_ADR_P2H(192, 168, 1, 1);
		//uint32_t dst_ip = IP4_ADR_P2H(172, 31, 54, 169);
		uint32_t src_ip = IP4_ADR_P2H(192, 168, 1, 20);
		//uint32_t src_ip = IP4_ADR_P2H(172, 31, 50, 160);

		secure_metadata_writeToElement(meta_req, "dst_ip", &dst_ip, META_TYPE_INT32);
		secure_metadata_writeToElement(meta_req, "src_ip", &src_ip, META_TYPE_INT32);

		struct finsFrame *ff_req = (struct finsFrame*) secure_malloc(sizeof(struct finsFrame));
		ff_req->dataOrCtrl = CONTROL;
		ff_req->destinationID = 3;
		ff_req->metaData = meta_req;

		ff_req->ctrlFrame.senderID = IPV4_ID;
		ff_req->ctrlFrame.serial_num = gen_control_serial_num();
		ff_req->ctrlFrame.opcode = CTRL_EXEC;
		ff_req->ctrlFrame.param_id = 0; //EXEC_ARP_GET_ADDR;

		ff_req->ctrlFrame.data_len = 0;
		ff_req->ctrlFrame.data = NULL;

		if (module_to_switch(fins_modules[3], ff_req)) {
			//i++;
		} else {
			PRINT_ERROR("freeing: ff=%p", ff_req);
			freeFinsFrame(ff_req);
			return;
		}
	}

	while (1)
		;

	sleep(5);
	PRINT_IMPORTANT("waiting...");
	char recv_data2[4000];
	gets(recv_data2);

	//############ terminating
	PRINT_IMPORTANT("modules: shutdown");
	for (i = MAX_MODULES - 1; i >= 0; i--) {
		if (fins_modules[i] != NULL) {
			fins_modules[i]->ops->shutdown(fins_modules[i]);
		}
	}

	PRINT_IMPORTANT("modules: release");
	for (i = MAX_MODULES - 1; i >= 0; i--) {
		if (fins_modules[i] != NULL) {
			fins_modules[i]->ops->release(fins_modules[i]);
		}
	}

	PRINT_IMPORTANT("libraries: close");
	while (1) {
		library = (struct fins_library *) list_remove_front(lib_list);
		if (library == NULL) {
			break;
		}
		PRINT_IMPORTANT("closing library: library=%p, name='%s'", library, library->name);
		dlclose(library->handle);
		free(library);
	}

	metadata_destroy(meta_stack);
	exit(1);
}

void core_main_old() {
//###################################################################### //TODO get this from config file eventually
//host interface
//strcpy((char *)my_host_if_name, "lo");
//strcpy((char *)my_host_if_name, "eth0");
//strcpy((char *)my_host_if_name, "eth1");
//strcpy((char *)my_host_if_name, "eth2");
	strcpy((char *) my_host_if_name, "wlan0");
//strcpy((char *)my_host_if_name, "wlan4");

//my_host_if_num = 1; //laptop lo //phone wlan0
//my_host_if_num = 2; //laptop eth0
//my_host_if_num = 3; //laptop wlan0
//my_host_if_num = 4; //laptop wlan4
//my_host_if_num = 10; //phone0 wlan0
	my_host_if_num = 17; //phone1 wlan0
//my_host_if_num = 6; //tablet1 wlan0

//my_host_mac_addr = 0x080027445566ull; //vbox eth2
//my_host_mac_addr = 0x001d09b35512ull; //laptop eth0
//my_host_mac_addr = 0x001cbf86d2daull; //laptop wlan0
//my_host_mac_addr = 0x00184d8f2a32ull; //laptop wlan4 card
//my_host_mac_addr = 0xa00bbae94bb0ull; //phone0 wlan0
	my_host_mac_addr = 0x10683f4f7467ull; //phone1 wlan0
//my_host_mac_addr = 0x50465d14e07full; //tablet1 wlan0

	my_host_ip_addr = IP4_ADR_P2H(192,168,1,8); //home testing
	my_host_mask = IP4_ADR_P2H(255,255,255,0); //home testing
//my_host_ip_addr = IP4_ADR_P2H(172,31,51,55); //lab testing
//my_host_mask = IP4_ADR_P2H(255,255,248,0); //lab testing

//loopback interface
	loopback_ip_addr = IP4_ADR_P2H(127,0,0,1);
	loopback_mask = IP4_ADR_P2H(255,0,0,0);

//any
	any_ip_addr = IP4_ADR_P2H(0,0,0,0);
//######################################################################

	switch_dummy();
//daemon_dummy();
//interface_dummy();

//arp_dummy();
//ipv4_dummy();
//icmp_dummy();
//tcp_dummy();
//udp_dummy();

//rtm_dummy();
//logger_dummy();

// Start the driving thread of each module
	PRINT_IMPORTANT("Initialize Modules");
//switch_init(); //should always be first
//daemon_init(); //TODO improve how sets mac/ip
//interface_init();

//arp_init();
//arp_register_interface(my_host_mac_addr, my_host_ip_addr);

//ipv4_init();
//ipv4_register_interface(my_host_mac_addr, my_host_ip_addr);

//icmp_init();
//tcp_init();
//udp_init();

//rtm_init(); //TODO when updated/fully implemented
//logger_init();

	pthread_attr_t fins_pthread_attr;
	pthread_attr_init(&fins_pthread_attr);

	PRINT_IMPORTANT("Run/start Modules");
//switch_run(&fins_pthread_attr);
//daemon_run(&fins_pthread_attr);
//interface_run(&fins_pthread_attr);

//arp_run(&fins_pthread_attr);
//ipv4_run(&fins_pthread_attr);
//icmp_run(&fins_pthread_attr);
//tcp_run(&fins_pthread_attr);
//udp_run(&fins_pthread_attr);

//rtm_run(&fins_pthread_attr);
//logger_run(&fins_pthread_attr);

//############################# //TODO custom test, remove later
	/*
	 if (1) {
	 //char recv_data[4000];

	 while (1) {
	 //gets(recv_data);

	 PRINT_DEBUG("Sending ARP req");

	 metadata *meta_req = (metadata *) secure_malloc(sizeof(metadata));
	 metadata_create(meta_req);

	 uint32_t dst_ip = IP4_ADR_P2H(192, 168, 1, 1);
	 //uint32_t dst_ip = IP4_ADR_P2H(172, 31, 54, 169);
	 uint32_t src_ip = my_host_ip_addr; //IP4_ADR_P2H(192, 168, 1, 20);
	 //uint32_t src_ip = IP4_ADR_P2H(172, 31, 50, 160);

	 secure_metadata_writeToElement(meta_req, "dst_ip", &dst_ip, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta_req, "src_ip", &src_ip, META_TYPE_INT32);

	 struct finsFrame *ff_req = (struct finsFrame*) secure_malloc(sizeof(struct finsFrame));
	 ff_req->dataOrCtrl = CONTROL;
	 ff_req->destinationID.id = ARP_ID;
	 ff_req->destinationID.next = NULL;
	 ff_req->metaData = meta_req;

	 ff_req->ctrlFrame.senderID = IPV4_ID;
	 ff_req->ctrlFrame.serial_num = gen_control_serial_num();
	 ff_req->ctrlFrame.opcode = CTRL_EXEC;
	 ff_req->ctrlFrame.param_id = EXEC_ARP_GET_ADDR;

	 ff_req->ctrlFrame.data_len = 0;
	 ff_req->ctrlFrame.data = NULL;

	 arp_to_switch(ff_req); //doesn't matter which queue
	 }
	 }
	 if (0) {
	 //char recv_data[4000];
	 while (1) {
	 //gets(recv_data);
	 sleep(15);

	 PRINT_IMPORTANT("start timing");

	 struct timeval start, end;
	 gettimeofday(&start, 0);

	 int its = 2; //30000;
	 int len = 10; //1000;

	 int i = 0;
	 while (i < its) {
	 uint8_t *data = (uint8_t *) secure_malloc(len);
	 memset(data, 74, len);

	 metadata *meta = (metadata *) secure_malloc(sizeof(metadata));
	 metadata_create(meta);

	 //uint32_t host_ip = IP4_ADR_P2H(192,168,1,8);
	 uint32_t host_ip = my_host_ip_addr;
	 uint32_t host_port = 55454;
	 uint32_t dst_ip = IP4_ADR_P2H(192,168,1,3);
	 //uint32_t dst_ip = IP4_ADR_P2H(172, 31, 54, 169);
	 uint32_t dst_port = 44444;
	 uint32_t ttl = 64;
	 uint32_t tos = 64;

	 secure_metadata_writeToElement(meta, "send_src_ip", &host_ip, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_src_port", &host_port, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_dst_ip", &dst_ip, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_dst_port", &dst_port, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_ttl", &ttl, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_tos", &tos, META_TYPE_INT32);

	 struct finsFrame *ff = (struct finsFrame *) secure_malloc(sizeof(struct finsFrame));
	 ff->dataOrCtrl = DATA;
	 ff->destinationID.id = UDP_ID;
	 ff->destinationID.next = NULL;
	 ff->metaData = meta;

	 ff->dataFrame.directionFlag = DIR_DOWN;
	 ff->dataFrame.pduLength = len;
	 ff->dataFrame.pdu = data;

	 PRINT_IMPORTANT("sending: ff=%p, meta=%p", ff, meta);
	 if (1) {
	 if (arp_to_switch(ff)) {
	 i++;
	 } else {
	 PRINT_ERROR("freeing: ff=%p", ff);
	 freeFinsFrame(ff);
	 return;
	 }
	 }
	 sleep(5);

	 if (0) {
	 if (daemon_fdf_to_switch(UDP_ID, data, len, meta)) {
	 i++;
	 } else {
	 PRINT_ERROR("error sending");
	 metadata_destroy(meta);
	 free(data);
	 break;
	 }
	 }
	 }

	 //struct timeval start, end;
	 //gettimeofday(&start, 0);
	 if (0) {
	 gettimeofday(&end, 0);
	 double diff = time_diff(&start, &end);
	 PRINT_IMPORTANT("diff=%f, len=%d, avg=%f ms, calls=%f, bits=%f", diff, len, diff/its, 1000/(diff/its), 8*1000/(diff/its)*len);
	 }
	 break;
	 }
	 }
	 if (0) {
	 //char recv_data[4000];
	 while (1) {
	 //gets(recv_data);
	 sleep(15);

	 PRINT_IMPORTANT("start timing");

	 struct timeval start, end;
	 gettimeofday(&start, 0);

	 int its = 1; //30000;
	 int len = 10; //1000;

	 int i = 0;
	 while (i < its) {
	 uint8_t *data = (uint8_t *) secure_malloc(len);
	 memset(data, 74, len);

	 metadata *meta = (metadata *) secure_malloc(sizeof(metadata));
	 metadata_create(meta);

	 uint32_t host_ip = IP4_ADR_P2H(192,168,1,7);
	 uint32_t host_port = 55454;
	 uint32_t dst_ip = IP4_ADR_P2H(192,168,1,8);
	 uint32_t dst_port = 44444;
	 uint32_t ttl = 64;
	 uint32_t tos = 64;

	 secure_metadata_writeToElement(meta, "send_src_ip", &host_ip, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_src_port", &host_port, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_dst_ip", &dst_ip, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_dst_port", &dst_port, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_ttl", &ttl, META_TYPE_INT32);
	 secure_metadata_writeToElement(meta, "send_tos", &tos, META_TYPE_INT32);

	 struct finsFrame *ff = (struct finsFrame *) secure_malloc(sizeof(struct finsFrame));
	 ff->dataOrCtrl = DATA;
	 ff->destinationID.id = UDP_ID;
	 ff->destinationID.next = NULL;
	 ff->metaData = meta;

	 ff->dataFrame.directionFlag = DIR_DOWN;
	 ff->dataFrame.pduLength = len;
	 ff->dataFrame.pdu = data;

	 PRINT_DEBUG("sending: ff=%p, meta=%p", ff, meta);
	 if (arp_to_switch(ff)) {
	 i++;
	 } else {
	 PRINT_ERROR("freeing: ff=%p", ff);
	 freeFinsFrame(ff);
	 return;
	 }

	 if (0) {
	 if (daemon_fdf_to_switch(UDP_ID, data, len, meta)) {
	 i++;
	 } else {
	 PRINT_ERROR("error sending");
	 metadata_destroy(meta);
	 free(data);
	 break;
	 }
	 }
	 }

	 //struct timeval start, end;
	 //gettimeofday(&start, 0);
	 gettimeofday(&end, 0);
	 double diff = time_diff(&start, &end);
	 PRINT_IMPORTANT("diff=%f, len=%d, avg=%f ms, calls=%f, bits=%f", diff, len, diff/its, 1000/(diff/its), 8*1000/(diff/its)*len);
	 break;
	 }
	 }
	 //*/
//#############################
	PRINT_IMPORTANT("Just waiting");
	while (1) {
		//sleep(1);
	}
}

#ifndef BUILD_FOR_ANDROID
int main() {
	core_main();
	return 0;
}
#endif