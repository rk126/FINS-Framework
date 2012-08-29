/*  
 * FINS_stack_wedge.c - 
 */

/* License and signing info */
#define M_LICENSE	"GPL"	// READ ABOUT THIS BEFORE CHANGING! Must be some form of GPL.
#define M_DESCRIPTION	"Registers the FINS protocol with the kernel"
#define M_AUTHOR	"Jonathan Reed <jonathanreed07@gmail.com>"

/* Includes needed for LKM overhead */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>		/* Needed for the macros */
/* Includes for the protocol registration part (the main component, not the LKM overhead) */
#include <net/sock.h>		/* Needed for proto and sock struct defs, etc. */
#include <linux/socket.h>	/* Needed for the sockaddr struct def */
#include <linux/errno.h>	/* Needed for error number defines */
#include <linux/aio.h>		/* Needed for FINS_sendmsg */
#include <linux/skbuff.h>	/* Needed for sk_buff struct def, etc. */
#include <linux/net.h>		/* Needed for socket struct def, etc. */
/* Includes for the netlink socket part */
#include <linux/netlink.h>	/* Needed for netlink socket API, macros, etc. */
#include <linux/semaphore.h>	/* Needed to lock/unlock blocking calls with handler */
#include <asm/uaccess.h>	/** Copy from user */
#include <asm/ioctls.h>		/* Needed for FINS_ioctl */
#include <linux/sockios.h>
#include <linux/delay.h>	/* For sleep */
#include <linux/if.h>		/* Needed for FINS_ioctl */
#include <asm/spinlock.h> //might be linux/spin... for rwlock
//##
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/inet_sock.h>
#include <net/protocol.h>
//##
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/thread_info.h>
#include <linux/rcupdate.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/wanrouter.h>
#include <linux/if_bridge.h>
#include <linux/if_frad.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/kmod.h>
#include <linux/audit.h>
#include <linux/wireless.h>
#include <linux/nsproxy.h>
#include <linux/magic.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>

#include <net/compat.h>
#include <net/wext.h>
#include <net/cls_cgroup.h>

#include <net/sock.h>
#include <linux/netfilter.h>

#include <linux/if_tun.h>
#include <linux/ipv6_route.h>
#include <linux/route.h>
#include <linux/sockios.h>
#include <linux/atalk.h>

/*
 #include <linux/netdevice.h>
 #include <linux/inetdevice.h>
 #include <linux/seq_file.h>
 #include <linux/bootmem.h>
 #include <linux/highmem.h>
 #include <linux/swap.h>
 #include <linux/slab.h>
 #include <net/net_namespace.h>
 #include <net/protocol.h>
 #include <net/ip.h>
 #include <net/ipv6.h>
 #include <net/route.h>
 #include <net/sctp/sctp.h>
 #include <net/addrconf.h>
 #include <net/inet_ecn.h>
 */

//#include <sys/socket.h> /* may need to be removed */
#include "FINS_stack_wedge.h"	/* Defs for this module */

#define RECV_BUFFER_SIZE	1024	// Same as userspace, Pick an appropriate value here
//for compiling in non modified kernel, will compile but not run
#ifndef PF_FINS
#define AF_FINS 2
#define PF_FINS AF_FINS
#endif
#ifndef NETLINK_FINS
#define NETLINK_FINS 20
#endif

//commenting stops debug printout
#define DEBUG
#define ERROR

#ifdef DEBUG
#define PRINT_DEBUG(format, args...) printk("FINS: DEBUG: %s, %d: "format"\n", __FUNCTION__, __LINE__, ##args);
#else
#define PRINT_DEBUG(format, args...)
#endif

#ifdef ERROR
#define PRINT_ERROR(format, args...) printk("FINS: ERROR: %s, %d: "format"\n", __FUNCTION__, __LINE__, ##args);
#else
#define PRINT_ERROR(format, args...)
#endif

// Create one semaphore here for every socketcall that is going to block
struct fins_wedge_socket wedge_sockets[MAX_SOCKETS];
struct semaphore sockets_sem;

struct fins_call wedge_calls[MAX_CALLS];
struct semaphore calls_sem; //TODO merge with sockets_sem?
u_int call_count; //TODO fix eventual roll over problem

struct semaphore link_sem;

//extern static const struct net_proto_family __rcu *net_families[NPROTO] __read_mostly;

int print_exit(const char *func, int line, int rc) {
#ifdef DEBUG
	printk(KERN_DEBUG "FINS: DEBUG: %s, %d: Exited: %d\n", func, line, rc);
#endif
	return rc;
}

void init_wedge_calls(void) {
	int i;
	PRINT_DEBUG("Entered.");

	call_count = 0;

	sema_init(&calls_sem, 1);
	for (i = 0; i < MAX_CALLS; i++) {
		wedge_calls[i].call_id = -1;
	}

	PRINT_DEBUG("Exited.");
}

int insert_wedge_call(u_int id, unsigned long long sock_id, int sock_index, u_int type) { //TODO might not need sock
	int i;

	PRINT_DEBUG("Entered for %llu.", sock_id);

	for (i = 0; i < MAX_CALLS; i++) {
		if (wedge_calls[i].call_id == -1) {
			wedge_calls[i].running = 1;

			wedge_calls[i].call_id = id;
			wedge_calls[i].sock_id = sock_id;
			wedge_calls[i].sock_index = sock_index;
			wedge_calls[i].type = type;

			//sema_init(&wedge_calls[i].sem, 1);
			wedge_calls[i].reply = 0;
			sema_init(&wedge_calls[i].wait_sem, 0);

			wedge_calls[i].ret = 0;

			return print_exit(__FUNCTION__, __LINE__, i);
		}
	}

	return print_exit(__FUNCTION__, __LINE__, -1);
}

int find_wedge_call(unsigned long long sock_id, int sock_index, u_int type) {
	u_int i;

	//PRINT_DEBUG("Entered: id=%u", id);

	for (i = 0; i < MAX_CALLS; i++) {
		if (wedge_calls[i].call_id != -1 && wedge_calls[i].sock_id == sock_id && wedge_calls[i].sock_index == sock_index && wedge_calls[i].type == type) { //TODO remove sock_index? maybe unnecessary
			return print_exit(__FUNCTION__, __LINE__, i);
		}
	}

	return print_exit(__FUNCTION__, __LINE__, -1);
}

int remove_wedge_call(u_int id) { //TODO remove? not used since id/index typicall tied, & removal doesn't need locking
	int i;

	PRINT_DEBUG("Entered: id=%u.", id);

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	for (i = 0; i < MAX_CALLS; i++) {
		if (wedge_calls[i].call_id == id) {
			wedge_calls[i].call_id = -1;

			up(&calls_sem);

			//TODO finish
			return (1);
		}
	}
	up(&calls_sem);
	return (-1);
}

void init_wedge_sockets(void) {
	int i;

	PRINT_DEBUG("Entered.");

	sema_init(&sockets_sem, 1);
	for (i = 0; i < MAX_SOCKETS; i++) {
		wedge_sockets[i].sock_id = -1;
	}

	PRINT_DEBUG("Exited.");
}

int insert_wedge_socket(unsigned long long sock_id, struct sock *sk) { //TODO might not need sock
	int i;
	int j;

	PRINT_DEBUG("Entered for %llu.", sock_id);

	for (i = 0; i < MAX_SOCKETS; i++) {
		if ((wedge_sockets[i].sock_id == -1)) {
			wedge_sockets[i].running = 1;

			wedge_sockets[i].sock_id = sock_id;
			wedge_sockets[i].sk = sk;

			for (j = 0; j < MAX_CALL_TYPES; j++) {
				wedge_sockets[i].threads[j] = 0;
			}

			wedge_sockets[i].release_flag = 0;
			wedge_sockets[i].sk_new = NULL;

			return print_exit(__FUNCTION__, __LINE__, i);
			//return i;
		}
	}

	return print_exit(__FUNCTION__, __LINE__, -1);
	//return -1;
}

int find_wedge_socket(unsigned long long sock_id) {
	int i;

	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	for (i = 0; i < MAX_SOCKETS; i++) {
		if (wedge_sockets[i].sock_id == sock_id) {
			return print_exit(__FUNCTION__, __LINE__, i);
		}
	}

	return print_exit(__FUNCTION__, __LINE__, -1);
}

int remove_wedge_socket(unsigned long long sock_id, int sock_index, u_int type) {
	u_int i;
	int call_index;

	for (i = 0; i < MAX_CALL_TYPES; i++) {
		while (1) {
			if (wedge_sockets[sock_index].threads[i] < 1 || (i == type && wedge_sockets[sock_index].threads[i] < 2)) {
				break;
			}
			up(&sockets_sem);

			if (down_interruptible(&calls_sem)) {
				PRINT_ERROR("calls_sem acquire fail");
				//TODO error
			}
			call_index = find_wedge_call(sock_id, sock_index, i);
			up(&calls_sem);
			if (call_index == -1) {
				break;
			}
			up(&wedge_calls[call_index].wait_sem);

			msleep(1); //TODO may need to change

			if (down_interruptible(&sockets_sem)) {
				PRINT_ERROR("sockets_sem acquire fail");
				//TODO error
			}
		}
	}

	wedge_sockets[sock_index].sock_id = -1;

	return 0;
}

int threads_incr(int sock_index, u_int call) {
	int ret = 1;

	return ret;
}

int threads_decr(int sock_index, u_int call) {
	int ret = 0;

	return ret;
}

int wait_wedge_socket(unsigned long long sock_id, int sock_index, u_int calltype) {
	//int error = 0;

	PRINT_DEBUG("Entered for sock=%llu sock_index=%d call=%u", sock_id, sock_index, calltype);

	return print_exit(__FUNCTION__, __LINE__, 0);
}

int checkConfirmation(int call_index) {
	//extract msg from reply in wedge_calls[sock_index]
	if (wedge_calls[call_index].ret == ACK) {
		PRINT_DEBUG("recv ACK");
		if (wedge_calls[call_index].len == 0) {
			return 0;
		} else {
			PRINT_ERROR("wedge_calls[sock_index].reply_buf error, wedge_calls[%d].len=%d wedge_calls[%d].buf=%p",
					call_index, wedge_calls[call_index].len, call_index, wedge_calls[call_index].buf);
			return -1;
		}
	} else if (wedge_calls[call_index].ret == NACK) {
		PRINT_DEBUG("recv NACK msg=%u", wedge_calls[call_index].msg);
		return -wedge_calls[call_index].msg;
	} else {
		PRINT_ERROR("error, acknowledgement: %u", wedge_calls[call_index].ret);
		return -1;
	}
}

/* FINS Netlink functions  */
/*
 * Sends len bytes from buffer buf to process pid, and sets the flags.
 * If buf is longer than RECV_BUFFER_SIZE, it's broken into sequential messages.
 * Returns 0 if successful or -1 if an error occurred.
 */

//assumes msg_buf is just the msg, does not have a prepended msg_len
//break msg_buf into parts of size RECV_BUFFER_SIZE with a prepended header (header part of RECV...)
//prepend msg header: total msg length, part length, part starting position
int nl_send_msg(int pid, unsigned int seq, int type, void *buf, ssize_t len, int flags) {
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int ret_val;

	//####################
	u_char *print_buf;
	u_char *print_pt;
	u_char *pt;
	int i;

	PRINT_DEBUG("pid=%d, seq=%d, type=%d, len=%d", pid, seq, type, len);

	print_buf = (u_char *) kmalloc(5 * len, GFP_KERNEL);
	if (print_buf == NULL) {
		PRINT_ERROR("print_buf allocation fail");
	} else {
		print_pt = print_buf;
		pt = buf;
		for (i = 0; i < len; i++) {
			if (i == 0) {
				sprintf(print_pt, "%02x", *(pt + i));
				print_pt += 2;
			} else if (i % 4 == 0) {
				sprintf(print_pt, ":%02x", *(pt + i));
				print_pt += 3;
			} else {
				sprintf(print_pt, " %02x", *(pt + i));
				print_pt += 3;
			}
		}
		PRINT_DEBUG("buf='%s'", print_buf);
		kfree(print_buf);
	}
	//####################

	// Allocate a new netlink message
	skb = nlmsg_new(len, 0); // nlmsg_new(size_t payload, gfp_t flags)
	if (skb == NULL) {
		PRINT_ERROR("netlink Failed to allocate new skb");
		return -1;
	}

	// Load nlmsg header
	// nlmsg_put(struct sk_buff *skb, u32 pid, u32 seq, int type, int payload, int flags)
	nlh = nlmsg_put(skb, KERNEL_PID, seq, type, len, flags);
	NETLINK_CB(skb).dst_group = 0; // not in a multicast group

	// Copy data into buffer
	memcpy(NLMSG_DATA(nlh), buf, len);

	// Send the message
	ret_val = nlmsg_unicast(FINS_nl_sk, skb, pid);
	if (ret_val < 0) {
		PRINT_ERROR("netlink error sending to user");
		return -1;
	}

	return 0;
}

int nl_send(int pid, void *msg_buf, ssize_t msg_len, int flags) {
	int ret;
	void *part_buf;
	u_char *msg_pt;
	int pos;
	u_int seq;
	u_char *hdr_msg_len;
	u_char *hdr_part_len;
	u_char *hdr_pos;
	u_char *msg_start;
	ssize_t header_size;
	ssize_t part_len;

	//#################### Debug
	u_char *print_buf;
	u_char *print_pt;
	u_char *pt;
	int i;
	//####################

	if (down_interruptible(&link_sem)) {
		PRINT_ERROR("link_sem acquire fail");
	}

	//#################### Debug
	print_buf = (u_char *) kmalloc(5 * msg_len, GFP_KERNEL);
	if (print_buf == NULL) {
		PRINT_ERROR("print_buf allocation fail");
	} else {
		print_pt = print_buf;
		pt = msg_buf;
		for (i = 0; i < msg_len; i++) {
			if (i == 0) {
				sprintf(print_pt, "%02x", *(pt + i));
				print_pt += 2;
			} else if (i % 4 == 0) {
				sprintf(print_pt, ":%02x", *(pt + i));
				print_pt += 3;
			} else {
				sprintf(print_pt, " %02x", *(pt + i));
				print_pt += 3;
			}
		}
		PRINT_DEBUG("msg_buf='%s'", print_buf);
		kfree(print_buf);
	}
	//####################

	part_buf = (u_char *) kmalloc(RECV_BUFFER_SIZE, GFP_KERNEL);
	if (part_buf == NULL) {
		PRINT_ERROR("part_buf allocation fail");
		up(&link_sem);
		return -1;
	}

	msg_pt = msg_buf;
	pos = 0;
	seq = 0;

	hdr_msg_len = part_buf;
	hdr_part_len = hdr_msg_len + sizeof(ssize_t);
	hdr_pos = hdr_part_len + sizeof(ssize_t);
	msg_start = hdr_pos + sizeof(int);

	header_size = msg_start - hdr_msg_len;
	part_len = RECV_BUFFER_SIZE - header_size;

	*(ssize_t *) hdr_msg_len = msg_len;
	*(ssize_t *) hdr_part_len = part_len;

	while (msg_len - pos > part_len) {
		PRINT_DEBUG("pos=%d", pos);

		*(int *) hdr_pos = pos;

		memcpy(msg_start, msg_pt, part_len);

		PRINT_DEBUG("seq=%d", seq);

		ret = nl_send_msg(pid, seq, 0x0, part_buf, RECV_BUFFER_SIZE, flags/*| NLM_F_MULTI*/);
		if (ret < 0) {
			PRINT_ERROR("netlink error sending seq %d to user", seq);
			up(&link_sem);
			return -1;
		}

		msg_pt += part_len;
		pos += part_len;
		seq++;
	}

	part_len = msg_len - pos;
	*(ssize_t *) hdr_part_len = part_len;
	*(int *) hdr_pos = pos;

	memcpy(msg_start, msg_pt, part_len);

	ret = nl_send_msg(pid, seq, NLMSG_DONE, part_buf, header_size + part_len, flags);
	if (ret < 0) {
		PRINT_ERROR("netlink error sending seq %d to user", seq);
		up(&link_sem);
		return -1;
	}

	kfree(part_buf);
	up(&link_sem);

	return 0;
}

/*
 * This function is automatically called when the kernel receives a datagram on the corresponding netlink socket.
 */
void nl_data_ready(struct sk_buff *skb) {
	struct nlmsghdr *nlh;
	u_char *buf; // Pointer to data in payload
	ssize_t len; // Payload length
	int pid; // pid of sending process
	struct nl_daemon_to_wedge *hdr;

	u_int reply_call; // a number corresponding to the type of socketcall this packet is in response to

	PRINT_DEBUG("Entered: skb=%p", skb);

	if (skb == NULL) {
		PRINT_DEBUG("Exiting: skb NULL \n");
		return;
	}
	nlh = (struct nlmsghdr *) skb->data;
	pid = nlh->nlmsg_pid; // get pid from the header

	// Get a pointer to the start of the data in the buffer and the buffer (payload) length
	buf = (u_char *) (NLMSG_DATA(nlh));
	len = NLMSG_PAYLOAD(nlh, 0);

	PRINT_DEBUG("nl_pid=%d nl_len=%d", pid, len);

	// **** Remember the LKM must be up first, then the daemon,
	// but the daemon must make contact before any applications try to use socket()

	if (pid == -1) { // if the socket daemon hasn't made contact before
		// Print what we received
		PRINT_DEBUG("No msg pID, received='%p'", buf);
	} else {
		if (len >= sizeof(struct nl_daemon_to_wedge)) {
			hdr = (struct nl_daemon_to_wedge *) buf;
			len -= sizeof(struct nl_daemon_to_wedge);

			/*
			 * extract common values and pass rest to shared buffer
			 * reply_call & sock_id, are to verify buf goes to the right sock & call
			 * This is preemptive as with multithreading we may have to add a shared queue
			 */

			PRINT_DEBUG("Reply: type=%u, id=%u, sock_index=%d, sock_id=%llu, sock_index=%d, ret=%u, msg=%u len=%d",
					hdr->call_type, hdr->call_id, hdr->call_index, hdr->sock_id, hdr->sock_index, hdr->ret, hdr->msg, len);

			if (hdr->call_type == 0) { //set to different calls
				if (hdr->sock_index == -1 || hdr->sock_index > MAX_SOCKETS) {
					PRINT_ERROR("invalid sock_index: sock_index=%d", hdr->sock_index);
					goto end;
				}
				if (down_interruptible(&sockets_sem)) {
					PRINT_ERROR("sockets_sem acquire fail");
					//TODO error
				}
				if (wedge_sockets[hdr->sock_index].sock_id != hdr->sock_id) {
					up(&sockets_sem);
					PRINT_ERROR("socket removed: sock_index=%d sock_id=%llu", hdr->sock_index, hdr->sock_id);
					goto end;
				}

				PRINT_DEBUG("sock_index=%d type=%u threads=%d", hdr->sock_index, hdr->call_type, wedge_sockets[hdr->sock_index].threads[hdr->call_type]);
				if (wedge_sockets[hdr->sock_index].threads[hdr->call_type] < 1) { //TODO may be unnecessary, since have call_index/call_id
					up(&sockets_sem);
					PRINT_ERROR("Exiting: no waiting threads found: sock_index=%d type=%u", hdr->sock_index, hdr->call_type);
					goto end;
				}
				up(&sockets_sem);

				if (wedge_sockets[hdr->sock_index].release_flag && (hdr->call_type != release_call)) { //TODO: may be unnecessary & can be removed (flag, etc)
					PRINT_DEBUG("socket released, dropping for sock_index=%d sock_id=%llu type=%d", hdr->sock_index, hdr->sock_id, hdr->call_type);
					//goto end; //TODO uncomment or remove
				}
			} else if (hdr->call_type < MAX_CALL_TYPES) {
				//This wedge version relies on the fact that each call gets a unique call ID and that value is only sent to the wedge once
				//Under this assumption a lock-less implementation can be used
				if (hdr->call_index == -1 || hdr->call_index > MAX_CALLS) {
					PRINT_ERROR("invalid call_index: call_index=%d", hdr->call_index);
					goto end;
				}
				if (down_interruptible(&calls_sem)) {
					PRINT_ERROR("calls_sem acquire fail");
					//TODO error
				}
				if (wedge_calls[hdr->call_index].call_id == hdr->call_id) {
					if (wedge_calls[hdr->call_index].type != hdr->call_type) { //TODO remove type check ~ unnecessary? shouldn't ever happen
						PRINT_ERROR("call mismatched: call_index=%d type=%u hdr->type=%u", hdr->call_index, wedge_calls[hdr->call_index].type, hdr->call_type);
					}
					wedge_calls[hdr->call_index].ret = hdr->ret;
					wedge_calls[hdr->call_index].msg = hdr->msg;
					wedge_calls[hdr->call_index].buf = buf + sizeof(struct nl_daemon_to_wedge);
					wedge_calls[hdr->call_index].len = len;
					wedge_calls[hdr->call_index].reply = 1;
					PRINT_DEBUG("shared created: sockID=%llu, call_id=%d, ret=%u, msg=%u, len=%d",
							wedge_calls[hdr->call_index].sock_id, wedge_calls[hdr->call_index].call_id, wedge_calls[hdr->call_index].ret, wedge_calls[hdr->call_index].msg, wedge_calls[hdr->call_index].len);
					up(&wedge_calls[hdr->call_index].wait_sem); //DON"T reference wedge_calls[hdr->call_index] after this

				} else {
					PRINT_ERROR("call mismatched: call_index=%d id=%u hdr->id=%u", hdr->call_index, wedge_calls[hdr->call_index].call_id, hdr->call_id);
				}
				up(&calls_sem);
			} else {
				//TODO error
			}
		} else if (len == sizeof(u_int)) {
			reply_call = *(u_int *) buf;
			if (reply_call == daemon_start_call) {
				if (FINS_daemon_pid != -1) {
					PRINT_DEBUG("Daemon pID changed, old pid=%d\n", FINS_daemon_pid);
				}
				FINS_daemon_pid = pid;
				PRINT_DEBUG("Daemon connected, pid=%d\n", FINS_daemon_pid);
			} else if (reply_call == daemon_stop_call) {
				FINS_daemon_pid = -1; //TODO expand this functionality
			} else {
				//TODO drop?
			}
		} else {
			//TODO error
			PRINT_DEBUG("Exiting: len too small: len=%d hdr=%d", len, sizeof(struct nl_daemon_to_wedge));
		}
	}

	end: PRINT_DEBUG("Exited: skb=%p", skb);
}

/* This function is called from within FINS_release and is modeled after ipx_destroy_socket() */
/*static void FINS_destroy_socket(struct sock *sk) {
 PRINT_DEBUG("called.");
 skb_queue_purge(&sk->sk_receive_queue);
 sk_refcnt_debug_dec(sk);
 }*/

int FINS_sk_create(struct net *net, struct socket *sock) {
	struct sock *sk;

	sk = sk_alloc(net, PF_FINS, GFP_KERNEL, &FINS_proto);
	if (sk == NULL) {
		return -ENOMEM;
	}

	sk_refcnt_debug_inc(sk);
	sock_init_data(sock, sk);

	sk->sk_no_check = 1;
	sock->ops = &FINS_proto_ops;

	return 0;
}

void FINS_sk_destroy(struct socket *sock) {
	struct sock *sk;
	sk = sock->sk;

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);

	sock_set_flag(sk, SOCK_DEAD);
	sock->sk = NULL;

	sk_refcnt_debug_release(sk);
	skb_queue_purge(&sk->sk_receive_queue);
	sk_refcnt_debug_dec(sk);

	sock_put(sk);
}

/* Wedge core functions (Protocol Registration) */
/*
 * This function tests whether the FINS data passthrough has been enabled or if the original stack is to be used
 * and passes data through appropriately.  This function is called when socket() call is made from userspace
 * (specified in struct net_proto_family FINS_net_proto)
 */
static int FINS_wedge_create(struct net *net, struct socket *sock, int protocol, int kern) {
	//int err;
	//const struct net_proto_family *pf;
	//int family = AF_INET;

	if (FINS_stack_passthrough_enabled == 1) {
		return FINS_create(net, sock, protocol, kern);
	} else { // Use original inet stack
		/*
		 rcu_read_lock();
		 pf = rcu_dereference(net_families[family]);
		 err = -EAFNOSUPPORT;
		 if (!pf)
		 return -1;
		 if (!try_module_get(pf->owner))
		 return -1;
		 rcu_read_unlock();
		 return pf->create(net, sock, protocol, kern);
		 */
		return -1;
		//return inet_family_ops.inet_create(net, sock, protocol, kern); //doesn't work? does internal number checking with AF_INET
		//return inet_stream_ops.bind(NULL, NULL, 0);
	}
}

/*
 * If the FINS stack passthrough is enabled, this function is called when socket() is called from userspace.
 * See FINS_wedge_create_socket for details.
 */
static int FINS_create(struct net *net, struct socket *sock, int protocol, int kern) {
	int rc = -ESOCKTNOSUPPORT;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	// Required stuff for kernel side
	rc = FINS_sk_create(net, sock);
	if (rc) {
		PRINT_ERROR("allocation failed");
		return print_exit(__FUNCTION__, __LINE__, rc);
	}

	sk = sock->sk;
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = insert_wedge_socket(sock_id, sk);
	PRINT_DEBUG("insert: sock_id=%llu sock_index=%d", sock_id, sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		FINS_sk_destroy(sock);
		return print_exit(__FUNCTION__, __LINE__, -ENOMEM);
	}

	call_threads = ++wedge_sockets[sock_index].threads[socket_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, socket_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto removeSocket;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + 3 * sizeof(int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto removeSocket;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = socket_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = AF_FINS; //~2, since this overrides AF_INET (39)
	pt += sizeof(int);

	*(int *) pt = sock->type;
	pt += sizeof(int);

	*(int *) pt = protocol;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto removeSocket;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", socket_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto removeSocket;
	}
	//release_sock(sk); //no one else can use, since socket creates

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
	}
	PRINT_DEBUG("relocked my semaphore");

	//lock_sock(sk); //no one else can use, since socket creates
	//wedge_calls[call_index].sem
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%d, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;
	//wedge_calls[call_index].sem

	if (rc) {
		removeSocket: //
		if (down_interruptible(&sockets_sem)) {
			PRINT_ERROR("sockets_sem acquire fail");
			//TODO error
		}
		ret = remove_wedge_socket(sock_id, sock_index, socket_call);
		up(&sockets_sem);

		FINS_sk_destroy(sock);
		return print_exit(__FUNCTION__, __LINE__, rc);
	}

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[socket_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, socket_call, wedge_sockets[sock_index].threads[socket_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, 0);
}

static int FINS_bind(struct socket *sock, struct sockaddr *addr, int addr_len) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, addr_len=%d", sock_id, addr_len);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[bind_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, bind_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(int) + addr_len + sizeof(u_int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = bind_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = addr_len;
	pt += sizeof(int);

	memcpy(pt, addr, addr_len);
	pt += addr_len;

	*(u_int *) pt = sk->sk_reuse;
	pt += sizeof(u_int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", bind_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[bind_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, bind_call, wedge_sockets[sock_index].threads[bind_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_listen(struct socket *sock, int backlog) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, backlog=%d", sock_id, backlog);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[listen_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, listen_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = listen_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = backlog;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", listen_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[listen_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, listen_call, wedge_sockets[sock_index].threads[listen_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_connect(struct socket *sock, struct sockaddr *addr, int addr_len, int flags) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, addr_len=%d flags=%x", sock_id, addr_len, flags);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[connect_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, connect_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + 2 * sizeof(int) + addr_len;
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = connect_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = addr_len;
	pt += sizeof(int);

	memcpy(pt, addr, addr_len);
	pt += addr_len;

	*(int *) pt = flags;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", connect_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[connect_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, connect_call, wedge_sockets[sock_index].threads[connect_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_accept(struct socket *sock, struct socket *sock_new, int flags) { //TODO fix, two blocking accept calls
	int rc;
	struct sock *sk, *sk_new;
	unsigned long long sock_id, sock_id_new;
	int sock_index, index_new;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, flags=%x", sock_id, flags);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk_new = wedge_sockets[sock_index].sk_new;
	if (sk_new == NULL) {
		rc = FINS_sk_create(sock_net(sock->sk), sock_new);
		if (rc) {
			PRINT_ERROR("allocation failed");
			up(&sockets_sem);
			release_sock(sk);
			return print_exit(__FUNCTION__, __LINE__, rc);
		}
		sk_new = sock_new->sk;
		lock_sock(sk_new);

		//sock_new->sk = NULL; //TODO comment?

		sock_id_new = getUniqueSockID(sk_new);
		PRINT_DEBUG("Created new=%llu", sock_id_new);

		index_new = insert_wedge_socket(sock_id_new, sk_new);
		PRINT_DEBUG("insert new: sock_id=%llu sock_index=%d", sock_id_new, index_new);
		if (index_new == -1) {
			up(&sockets_sem);
			FINS_sk_destroy(sock_new);
			release_sock(sk);
			return print_exit(__FUNCTION__, __LINE__, rc);
		}

		wedge_sockets[sock_index].sk_new = sk_new;
	} else {
		lock_sock(sk_new);

		sock_id_new = getUniqueSockID(sk_new);
		PRINT_DEBUG("Created new=%llu", sock_id_new);

		index_new = find_wedge_socket(sock_id_new);
		PRINT_DEBUG("sock_index new=%d", sock_index);
		if (sock_index == -1) {
			/*
			 index_new = insert_wedge_socket(uniqueSockID_new, sk_new);
			 PRINT_DEBUG("insert new: sock_id=%llu sock_index=%d", uniqueSockID_new, index_new);
			 if (index_new == -1) {
			 up(&sockets_sem);
			 FINS_sk_destroy(sock_new);
			 release_sock(sk);
			 return print_exit(__FUNCTION__, __LINE__, rc);
			 }
			 */
			up(&sockets_sem);
			release_sock(sk_new);
			release_sock(sk);
			return print_exit(__FUNCTION__, __LINE__, -1);
		}
	}

	call_threads = ++wedge_sockets[sock_index].threads[accept_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, accept_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(unsigned long long) + 2 * sizeof(int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = accept_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(unsigned long long *) pt = sock_id_new;
	pt += sizeof(unsigned long long);

	*(int *) pt = index_new;
	pt += sizeof(int);

	*(int *) pt = flags;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", accept_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk_new);
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	lock_sock(sk_new);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
		if (rc == 0) {
			sock_new->sk = sk_new;
			if (down_interruptible(&sockets_sem)) {
				PRINT_ERROR("sockets_sem acquire fail");
				//TODO error
			}
			wedge_sockets[sock_index].sk_new = NULL;

			//TODO create new sk_new

			up(&sockets_sem);
		}
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[accept_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, accept_call, wedge_sockets[sock_index].threads[accept_call]);
	up(&sockets_sem);

	release_sock(sk_new);
	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_getname(struct socket *sock, struct sockaddr *addr, int *len, int peer) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;
	struct sockaddr_in *addr_in;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, len=%d peer=%x", sock_id, *len, peer);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[getname_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, getname_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = getname_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = peer;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", getname_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		if (wedge_calls[call_index].ret == ACK) {
			PRINT_DEBUG("recv ACK");

			if (wedge_calls[call_index].buf && wedge_calls[call_index].len >= sizeof(int)) {
				pt = wedge_calls[call_index].buf;

				*len = *(int *) pt;
				pt += sizeof(int);

				PRINT_DEBUG("len=%d", *len);
				memset(addr, 0, sizeof(struct sockaddr));
				memcpy(addr, pt, *len);
				pt += *len;

				//########
				addr_in = (struct sockaddr_in *) addr;
				//addr_in->sin_port = ntohs(4000); //causes end port to be 4000
				PRINT_DEBUG("address: %u/%d", (addr_in->sin_addr).s_addr, ntohs(addr_in->sin_port));
				//########

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				} else {
					rc = 0;
				}
			} else {
				PRINT_ERROR("wedge_calls[call_index].buf error, wedge_calls[call_index].len=%d wedge_calls[call_index].buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
		} else if (wedge_calls[call_index].ret == NACK) {
			PRINT_DEBUG("recv NACK msg=%u", wedge_calls[call_index].msg);
			rc = -wedge_calls[call_index].msg;
		} else {
			PRINT_ERROR("error, acknowledgement: %d", wedge_calls[call_index].ret);
			rc = -1;
		}
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[getname_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, getname_call, wedge_sockets[sock_index].threads[getname_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;

	int i = 0;
	u_int data_len = 0;
	char *temp;

	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, len=%d", sock_id, len);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[sendmsg_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, sendmsg_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	for (i = 0; i < (msg->msg_iovlen); i++) {
		data_len += msg->msg_iov[i].iov_len;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(int) + (msg->msg_namelen > 0 ? msg->msg_namelen : 0) + 3 * sizeof(u_int) + msg->msg_controllen
			+ data_len;
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = sendmsg_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = msg->msg_namelen;
	pt += sizeof(int);

	if (msg->msg_namelen > 0) {
		memcpy(pt, msg->msg_name, msg->msg_namelen);
		pt += msg->msg_namelen;
	}

	*(u_int *) pt = msg->msg_flags; //stores sendmsg call flags
	pt += sizeof(u_int);

	*(u_int *) pt = msg->msg_controllen;
	pt += sizeof(u_int);

	memcpy(pt, msg->msg_control, msg->msg_controllen);
	pt += msg->msg_controllen;
	//Notice that the compiler takes  (msg->msg_iov[i]) as a struct not a pointer to struct

	*(u_int *) pt = data_len;
	pt += sizeof(u_int);

	temp = pt;

	i = 0;
	for (i = 0; i < msg->msg_iovlen; i++) {
		memcpy(pt, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
		pt += msg->msg_iov[i].iov_len;
		//PRINT_DEBUG("current element %d , element length = %d", i ,(msg->msg_iov[i]).iov_len );
	}

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("data_len=%d", data_len);
	PRINT_DEBUG("data='%s'", temp);

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", sendmsg_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
		if (rc == 0) {
			rc = data_len;
		}
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[sendmsg_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, sendmsg_call, wedge_sockets[sock_index].threads[sendmsg_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len, int flags) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	struct sockaddr_in *addr_in;
	ssize_t buf_len;
	u_char * buf;
	struct nl_wedge_to_daemon *hdr;
	u_char * pt;
	int ret;
	int i;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[recvmsg_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, recvmsg_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + 2 * sizeof(int) + 2 * sizeof(u_int) + msg->msg_controllen;
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = recvmsg_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = (int) len;
	pt += sizeof(int);

	*(int *) pt = flags;
	pt += sizeof(int);

	*(u_int *) pt = msg->msg_flags;
	pt += sizeof(u_int);

	*(u_int *) pt = msg->msg_controllen;
	pt += sizeof(u_int);

	memcpy(pt, msg->msg_control, msg->msg_controllen);
	pt += msg->msg_controllen;

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", recvmsg_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		if (wedge_calls[call_index].ret == ACK) {
			PRINT_DEBUG("recv ACK");
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len >= 0) {
				pt = wedge_calls[call_index].buf;

				//TODO: find out if this is right! udpHandling writes sockaddr_in here
				msg->msg_namelen = *(int *) pt; //reuse var since not needed anymore
				pt += sizeof(int);

				memcpy(msg->msg_name, pt, msg->msg_namelen);
				pt += msg->msg_namelen;

				//########
				addr_in = (struct sockaddr_in *) msg->msg_name;
				//addr_in->sin_port = ntohs(4000); //causes end port to be 4000
				PRINT_DEBUG("address: %d/%d", (addr_in->sin_addr).s_addr, ntohs(addr_in->sin_port));
				//########

				buf_len = *(int *) pt; //reuse var since not needed anymore
				pt += sizeof(int);

				if (buf_len >= 0) {
					//########
					u_char *temp = (u_char *) kmalloc(buf_len + 1, GFP_KERNEL);
					memcpy(temp, pt, buf_len);
					temp[buf_len] = '\0';
					PRINT_DEBUG("msg='%s'", temp);
					//########

					ret = buf_len; //reuse as counter
					i = 0;
					while (ret > 0 && i < msg->msg_iovlen) {
						if (ret > msg->msg_iov[i].iov_len) {
							copy_to_user(msg->msg_iov[i].iov_base, pt, msg->msg_iov[i].iov_len);
							pt += msg->msg_iov[i].iov_len;
							ret -= msg->msg_iov[i].iov_len;
							i++;
						} else {
							copy_to_user(msg->msg_iov[i].iov_base, pt, ret);
							pt += ret;
							ret = 0;
							break;
						}
					}
					if (ret) {
						//throw buffer overflow error?
						PRINT_ERROR("user buffer overflow error, overflow=%d", ret);
					}
					rc = buf_len;
				} else {
					PRINT_ERROR("iov_base alloc failure");
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
		} else if (wedge_calls[call_index].ret == NACK) {
			PRINT_DEBUG("recv NACK msg=%u", wedge_calls[call_index].msg);
			rc = -wedge_calls[call_index].msg;
		} else {
			PRINT_ERROR("error, acknowledgement: %u %u", wedge_calls[call_index].ret, wedge_calls[call_index].msg);
			rc = -1;
		}
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[recvmsg_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, recvmsg_call, wedge_sockets[sock_index].threads[recvmsg_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg) {
	int rc = 0;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	struct ifconf ifc;
	struct ifreq ifr;
	struct ifreq *ifr_pt;

	void __user
	*arg_pt = (void __user *)arg;

	int len;
	char __user
	*pos;

	//char *name;
	struct sockaddr_in *addr;

	//http://lxr.linux.no/linux+v2.6.39.4/net/core/dev.c#L4905 - ioctl

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	sk = sock->sk;
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, cmd=%u", sock_id, cmd);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[ioctl_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, ioctl_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	switch (cmd) {
	case SIOCGIFCONF:
		PRINT_DEBUG("cmd=%d ==SIOCGIFCONF", cmd);

		if (copy_from_user(&ifc, arg_pt, sizeof(struct ifconf))) {
			PRINT_ERROR("ERROR: cmd=%d ==SIOCGIFDSTADDR", cmd);
			//TODO error
			release_sock(sk);
			return print_exit(__FUNCTION__, __LINE__, -1);
		}

		pos = ifc.ifc_buf;
		if (ifc.ifc_buf == NULL) {
			len = 0;
		} else {
			len = ifc.ifc_len;
		}
		ifr_pt = ifc.ifc_req; //TODO figure out what this is used for

		PRINT_DEBUG("len=%d, pos=%d, ifr=%d", len, (int)pos, (int)ifr_pt);

		// Build the message
		buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(u_int) + sizeof(int);
		buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
		if (buf == NULL) {
			PRINT_ERROR("buffer allocation error");
			wedge_calls[call_index].call_id = -1;
			rc = -ENOMEM;
			goto end;
		}

		hdr = (struct nl_wedge_to_daemon *) buf;
		hdr->sock_id = sock_id;
		hdr->sock_index = sock_index;
		hdr->call_type = ioctl_call;
		hdr->call_threads = call_threads;
		hdr->call_id = call_id;
		hdr->call_index = call_index;
		pt = buf + sizeof(struct nl_wedge_to_daemon);

		*(u_int *) pt = cmd;
		pt += sizeof(u_int);

		*(int *) pt = len;
		pt += sizeof(int);

		if (pt - buf != buf_len) {
			PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
			kfree(buf);
			wedge_calls[call_index].call_id = -1;
			rc = -1;
			goto end;
		}
		break;
	case SIOCGIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCGIFNETMASK:
		if (copy_from_user(&ifr, arg_pt, sizeof(struct ifreq))) {
			PRINT_ERROR("ERROR: cmd=%d ==SIOCGIFDSTADDR", cmd);
			//TODO error
			release_sock(sk);
			return print_exit(__FUNCTION__, __LINE__, -1);
		}

		PRINT_DEBUG("cmd=%d name='%s'", cmd, ifr.ifr_name);

		// Build the message
		buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(u_int) + sizeof(int) + IFNAMSIZ;
		buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
		if (buf == NULL) {
			PRINT_ERROR("buffer allocation error");
			wedge_calls[call_index].call_id = -1;
			rc = -ENOMEM;
			goto end;
		}

		hdr = (struct nl_wedge_to_daemon *) buf;
		hdr->sock_id = sock_id;
		hdr->sock_index = sock_index;
		hdr->call_type = ioctl_call;
		hdr->call_threads = call_threads;
		hdr->call_id = call_id;
		hdr->call_index = call_index;
		pt = buf + sizeof(struct nl_wedge_to_daemon);

		*(u_int *) pt = cmd;
		pt += sizeof(u_int);

		*(int *) pt = IFNAMSIZ;
		pt += sizeof(int);

		memcpy(pt, ifr.ifr_name, IFNAMSIZ);
		pt += IFNAMSIZ;

		if (pt - buf != buf_len) {
			PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
			kfree(buf);
			wedge_calls[call_index].call_id = -1;
			rc = -1;
			goto end;
		}
		break;
	case FIONREAD:
		PRINT_DEBUG("cmd=FIONREAD");

		// Build the message
		buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(u_int);
		buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
		if (buf == NULL) {
			PRINT_ERROR("buffer allocation error");
			wedge_calls[call_index].call_id = -1;
			rc = -ENOMEM;
			goto end;
		}

		hdr = (struct nl_wedge_to_daemon *) buf;
		hdr->sock_id = sock_id;
		hdr->sock_index = sock_index;
		hdr->call_type = ioctl_call;
		hdr->call_threads = call_threads;
		hdr->call_id = call_id;
		hdr->call_index = call_index;
		pt = buf + sizeof(struct nl_wedge_to_daemon);

		*(u_int *) pt = cmd;
		pt += sizeof(u_int);

		if (pt - buf != buf_len) {
			PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
			kfree(buf);
			wedge_calls[call_index].call_id = -1;
			rc = -1;
			goto end;
		}
		break;
	case TIOCOUTQ:
		//case TIOCINQ: //equiv to FIONREAD??
	case SIOCADDRT:
	case SIOCDELRT:
	case SIOCSIFADDR:
		//case SIOCAIPXITFCRT:
		//case SIOCAIPXPRISLT:
		//case SIOCIPXCFGDATA:
		//case SIOCIPXNCPCONN:
	case SIOCGSTAMP:
	case SIOCSIFDSTADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFNETMASK:
		//TODO implement
		PRINT_DEBUG("cmd=%d not implemented", cmd);

		// Build the message
		buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(u_int);
		buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
		if (buf == NULL) {
			PRINT_ERROR("buffer allocation error");
			wedge_calls[call_index].call_id = -1;
			rc = -ENOMEM;
			goto end;
		}

		hdr = (struct nl_wedge_to_daemon *) buf;
		hdr->sock_id = sock_id;
		hdr->sock_index = sock_index;
		hdr->call_type = ioctl_call;
		hdr->call_threads = call_threads;
		hdr->call_id = call_id;
		hdr->call_index = call_index;
		pt = buf + sizeof(struct nl_wedge_to_daemon);

		*(u_int *) pt = cmd;
		pt += sizeof(u_int);

		if (pt - buf != buf_len) {
			PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
			kfree(buf);
			wedge_calls[call_index].call_id = -1;
			rc = -1;
			goto end;
		}
		break;
	default:
		PRINT_DEBUG("cmd=%d default", cmd);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", ioctl_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);

	if (!wedge_calls[call_index].reply) {
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	if (wedge_calls[call_index].ret == ACK) {
		PRINT_DEBUG("ioctl ACK");
		switch (cmd) {
		case SIOCGIFCONF:
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len >= sizeof(int)) {
				//values stored in ifr.ifr_addr
				pt = wedge_calls[call_index].buf;

				len = *(int *) pt;
				pt += sizeof(int);

				PRINT_DEBUG("SIOCGIFCONF len=%d ifc_len=%d", len, ifc.ifc_len);
				ifc.ifc_len = len;
				PRINT_DEBUG("SIOCGIFCONF len=%d ifc_len=%d", len, ifc.ifc_len);

				if (copy_to_user(ifc.ifc_buf, pt, len)) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}
				pt += len;

				//len = ifc.ifc_len;
				//pos = ifc.ifc_buf;
				//ifr_pt = ifc.ifc_req;

				if (copy_to_user(arg_pt, &ifc, sizeof(struct ifconf))) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			//rc = -1;
			break;
		case SIOCGIFADDR:
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len == sizeof(struct sockaddr_in)) {
				pt = wedge_calls[call_index].buf;

				memcpy(&ifr.ifr_addr, pt, sizeof(struct sockaddr_in));
				pt += sizeof(struct sockaddr_in);

				//#################
				addr = (struct sockaddr_in *) &ifr.ifr_addr;
				//memcpy(addr, pt, sizeof(struct sockaddr));
				PRINT_DEBUG("name=%s, addr=%d (%d/%d)", ifr.ifr_name, (int)addr, addr->sin_addr.s_addr, addr->sin_port);
				//#################

				if (copy_to_user(arg_pt, &ifr, sizeof(struct ifreq))) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			break;
		case SIOCGIFDSTADDR:
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len == sizeof(struct sockaddr_in)) {
				pt = wedge_calls[call_index].buf;

				memcpy(&ifr.ifr_dstaddr, pt, sizeof(struct sockaddr_in));
				pt += sizeof(struct sockaddr_in);

				//#################
				addr = (struct sockaddr_in *) &ifr.ifr_dstaddr;
				PRINT_DEBUG("name=%s, addr=%d (%d/%d)", ifr.ifr_name, (int)addr, addr->sin_addr.s_addr, addr->sin_port);
				//#################

				if (copy_to_user(arg_pt, &ifr, sizeof(struct ifreq))) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			break;
		case SIOCGIFBRDADDR:
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len == sizeof(struct sockaddr_in)) {
				pt = wedge_calls[call_index].buf;

				memcpy(&ifr.ifr_broadaddr, pt, sizeof(struct sockaddr_in));
				pt += sizeof(struct sockaddr_in);

				//#################
				addr = (struct sockaddr_in *) &ifr.ifr_broadaddr;
				PRINT_DEBUG("name=%s, addr=%d (%d/%d)", ifr.ifr_name, (int)addr, addr->sin_addr.s_addr, addr->sin_port);
				//#################

				if (copy_to_user(arg_pt, &ifr, sizeof(struct ifreq))) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			break;
		case SIOCGIFNETMASK:
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len == sizeof(struct sockaddr_in)) {
				pt = wedge_calls[call_index].buf;

				memcpy(&ifr.ifr_addr, pt, sizeof(struct sockaddr_in));
				pt += sizeof(struct sockaddr_in);

				//#################
				addr = (struct sockaddr_in *) &ifr.ifr_addr;
				PRINT_DEBUG("name=%s, addr=%d (%d/%d)", ifr.ifr_name, (int)addr, addr->sin_addr.s_addr, addr->sin_port);
				//#################
				if (copy_to_user(arg_pt, &ifr, sizeof(struct ifreq))) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}

			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			break;
		case FIONREAD:
			if (wedge_calls[call_index].buf && wedge_calls[call_index].len == sizeof(int)) {
				pt = wedge_calls[call_index].buf;

				len = *(int *) pt;
				pt += sizeof(int);

				//#################
				PRINT_DEBUG("len=%d", len);
				//#################

				if (copy_to_user(arg_pt, &len, sizeof(int))) {
					PRINT_ERROR("ERROR: cmd=%d", cmd);
					//TODO error
					rc = -1;
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("READING ERROR! diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = -1;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			break;
			//-----------------------------------
		case TIOCOUTQ:
			//case TIOCINQ: //equiv to FIONREAD??
		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCSIFADDR:
			//case SIOCAIPXITFCRT:
			//case SIOCAIPXPRISLT:
			//case SIOCIPXCFGDATA:
			//case SIOCIPXNCPCONN:
		case SIOCGSTAMP:
		case SIOCSIFDSTADDR:
		case SIOCSIFBRDADDR:
		case SIOCSIFNETMASK:
			//TODO implement cases above
			if (wedge_calls[call_index].buf && (wedge_calls[call_index].len == 0)) {
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = -1;
			}
			break;
		default:
			PRINT_DEBUG("cmd=%d default", cmd);
			rc = -1;
			break;
		}
	} else if (wedge_calls[call_index].ret == NACK) {
		PRINT_DEBUG("recv NACK msg=%u", wedge_calls[call_index].msg);
		//rc = -1;
		rc = -wedge_calls[call_index].msg;
	} else {
		PRINT_ERROR("error, acknowledgement: %d", wedge_calls[call_index].ret);
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;
	//##

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[ioctl_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, ioctl_call, wedge_sockets[sock_index].threads[ioctl_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

/*
 * This function is called automatically to cleanup when a program that
 * created a socket terminates.
 * Or manually via close()?????
 * Modeled after ipx_release().
 */
static int FINS_release(struct socket *sock) {
	int rc = 0;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, 0); //TODO should be -1, done to prevent stalls
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, 0);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		FINS_sk_destroy(sock);
		return print_exit(__FUNCTION__, __LINE__, 0);
	}

	if (wedge_sockets[sock_index].release_flag) {
		//check such that successive release calls return immediately, affectively release only performed once
		up(&sockets_sem);
		return print_exit(__FUNCTION__, __LINE__, 0);
	}

	wedge_sockets[sock_index].release_flag = 1;
	call_threads = ++wedge_sockets[sock_index].threads[release_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, release_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = release_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", release_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	ret = remove_wedge_socket(sock_id, sock_index, release_call);
	up(&sockets_sem);

	FINS_sk_destroy(sock);
	return print_exit(__FUNCTION__, __LINE__, 0); //TODO should be rc, 0 to prevent stalling
}

static unsigned int FINS_poll(struct file *file, struct socket *sock, poll_table *table) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	int events;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	//struct socket *sock = file->private_data;
	if (table) {
		PRINT_DEBUG("file=%p sock=%p table=%p key=%lu", file, sock, table, table->key);
	} else {
		PRINT_DEBUG("file=%p sock=%p table=%p key=NULL", file, sock, table);
	}

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, 0);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, 0);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == 0) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, 0);
	}

	call_threads = ++wedge_sockets[sock_index].threads[poll_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, poll_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = 0;
		goto end;
	}

	if (table) {
		events = table->key;
	} else {
		//events = 0;
		rc = 0;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = 0;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = poll_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = events;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = 0;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", poll_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = 0;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		if (wedge_calls[call_index].ret == ACK) {
			PRINT_DEBUG("recv ACK");

			if (wedge_calls[call_index].buf && (wedge_calls[call_index].len == sizeof(u_int))) {
				pt = wedge_calls[call_index].buf;

				rc = *(u_int *) pt;
				pt += sizeof(u_int);

				PRINT_DEBUG("rc=%x", rc);

				//rc = POLLIN | POLLPRI | POLLOUT | POLLERR | POLLHUP | POLLNVAL | POLLRDNORM | POLLRDBAND | POLLWRNORM | POLLWRBAND;
				//rc = POLLOUT;
				//rc = -1;
				//PRINT_DEBUG("rc=%x", rc);

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_DEBUG("READING ERROR! CRASH, diff=%d len=%d", pt - wedge_calls[call_index].buf, wedge_calls[call_index].len);
					rc = 0;
				}
			} else {
				PRINT_ERROR("wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
						wedge_calls[call_index].len, wedge_calls[call_index].buf);
				rc = 0;
			}
		} else if (wedge_calls[call_index].ret == NACK) {
			PRINT_DEBUG("recv NACK msg=%u", wedge_calls[call_index].msg);
			//rc = -wedge_calls[call_index].msg; //TODO put in sk error value
			rc = 0;
		} else {
			PRINT_ERROR("error, acknowledgement: %d", wedge_calls[call_index].ret);
			rc = 0;
		}
	} else {
		rc = 0;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[poll_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, poll_call, wedge_sockets[sock_index].threads[poll_call]);
	up(&sockets_sem);

	//poll_wait(file, sk_sleep(sk), table);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

//TODO figure out when this is called
static int FINS_shutdown(struct socket *sock, int how) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu, how=%d", sock_id, how);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[shutdown_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, shutdown_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + sizeof(int);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = shutdown_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = how;
	pt += sizeof(int);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", shutdown_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[shutdown_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, shutdown_call, wedge_sockets[sock_index].threads[shutdown_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_socketpair(struct socket *sock1, struct socket *sock2) {
	struct sock *sk1, *sk2;
	unsigned long long uniqueSockID1, uniqueSockID2;
	int ret;
	char *buf; // used for test
	ssize_t buffer_length; // used for test

	PRINT_DEBUG("Called");
	return -1;

	sk1 = sock1->sk;
	uniqueSockID1 = getUniqueSockID(sk1);

	sk2 = sock2->sk;
	uniqueSockID2 = getUniqueSockID(sk2);

	PRINT_DEBUG("Entered for %llu, %llu.", uniqueSockID1, uniqueSockID2);

	// Notify FINS daemon
	if (FINS_daemon_pid == -1) { // FINS daemon has not made contact yet, no idea where to send message
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	//TODO: finish this & daemon side

	// Build the message
	buf = "FINS_socketpair() called.";
	buffer_length = strlen(buf) + 1;

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buffer_length, 0);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	return 0;
}

static int FINS_mmap(struct file *file, struct socket *sock, struct vm_area_struct *vma) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[mmap_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, mmap_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = mmap_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", mmap_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[mmap_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, mmap_call, wedge_sockets[sock_index].threads[mmap_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static ssize_t FINS_sendpage(struct socket *sock, struct page *page, int offset, size_t size, int flags) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
		return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
	if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[sendpage_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
		//TODO error
	}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, sendpage_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
	if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
		wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = sendpage_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
		kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", sendpage_call, sock_id, buf_len);

	// Send message to FINS_daemon
	ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
	if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
		//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
	}
	PRINT_DEBUG("relocked my semaphore");

	lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
			wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
	if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
		//TODO error
	}
	wedge_sockets[sock_index].threads[sendpage_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, sendpage_call, wedge_sockets[sock_index].threads[sendpage_call]);
	up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_getsockopt(struct socket *sock, int level, int optname, char __user*optval, int __user *optlen) {
	//static int FINS_getsockopt(struct socket *sock, int level, int optname, char *optval, int *optlen) {
	int rc = 0;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char *buf;
	struct nl_wedge_to_daemon *hdr;
	u_char *pt;
	int ret;

	int len;

	//SO_REUSEADDR
	//SO_ERROR
	//SO_PRIORITY
	//SO_SNDBUF
	//SO_RCVBUF

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
//TODO error
}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[getsockopt_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
//TODO error
}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, getsockopt_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	ret = copy_from_user(&len, optlen, sizeof(int));
	if (ret) {
		PRINT_ERROR("copy_from_user fail ret=%d", ret);
goto end;
	}
	PRINT_DEBUG("len=%d", len);

// Build the message
buf_len = sizeof(struct nl_wedge_to_daemon) + 3 * sizeof(int) + (len > 0 ? len : 0);
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = getsockopt_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = level;
	pt += sizeof(int);

	*(int *) pt = optname;
	pt += sizeof(int);

	*(int *) pt = len;
	pt += sizeof(int);

	if (len > 0) { //TODO prob don't need
		ret = copy_from_user(pt, optval, len);
		pt += len;
		if (ret) {
			PRINT_ERROR("copy_from_user fail ret=%d", ret);
kfree(buf);
			wedge_calls[call_index].call_id = -1;
			rc = -1;
			goto end;
		}
	}

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", getsockopt_call, sock_id, buf_len);

// Send message to FINS_daemon
ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
}
	PRINT_DEBUG("relocked my semaphore");

lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
		wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
if (wedge_calls[call_index].reply) {
		if (wedge_calls[call_index].ret == ACK) {
			PRINT_DEBUG("recv ACK");
if (wedge_calls[call_index].buf && wedge_calls[call_index].len >= sizeof(int)) {
				pt = wedge_calls[call_index].buf;
				rc = 0;

				//re-using len var
				len = *(int *) pt;
				pt += sizeof(int);
				ret = copy_to_user(optlen, &len, sizeof(int));
				if (ret) {
					PRINT_ERROR("copy_from_user fail ret=%d", ret);
rc = -1;
				}

				if (len > 0) {
					ret = copy_to_user(optval, pt, len);
					pt += len;
					if (ret) {
						PRINT_ERROR("copy_from_user fail ret=%d", ret);
rc = -1;
					}
				}

				if (pt - wedge_calls[call_index].buf != wedge_calls[call_index].len) {
					PRINT_ERROR("write error: diff=%d len=%d", pt-wedge_calls[call_index].buf, wedge_calls[call_index].len);
rc = -1;
				}
			} else {
				PRINT_ERROR( "wedgeSockets[sock_index].reply_buf error, wedgeSockets[sock_index].reply_len=%d wedgeSockets[sock_index].reply_buf=%p",
		wedge_calls[call_index].len, wedge_calls[call_index].buf);
rc = -1;
			}
		} else if (wedge_calls[call_index].ret == NACK) {
			PRINT_DEBUG("recv NACK msg=%u", wedge_calls[call_index].msg);
rc = -wedge_calls[call_index].msg;
		} else {
			PRINT_ERROR("error, acknowledgement: %d", wedge_calls[call_index].ret);
rc = -1;
		}
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
//TODO error
}
	wedge_sockets[sock_index].threads[getsockopt_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, getsockopt_call, wedge_sockets[sock_index].threads[getsockopt_call]);
up(&sockets_sem);

	release_sock(sk);
	return print_exit(__FUNCTION__, __LINE__, rc);
}

static int FINS_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int optlen) {
	//static int FINS_setsockopt(struct socket *sock, int level, int optname, char *optval, unsigned int optlen) {
	int rc;
	struct sock *sk;
	unsigned long long sock_id;
	int sock_index;
	int call_threads;
	u_int call_id;
	int call_index;
	ssize_t buf_len;
	u_char * buf;
	struct nl_wedge_to_daemon *hdr;
	u_char * pt;
	int ret;

	if (FINS_daemon_pid == -1) { // FINS daemon not connected, nowhere to send msg
		PRINT_ERROR("daemon not connected");
return print_exit(__FUNCTION__, __LINE__, -1);
	}

	sk = sock->sk;
	if (sk == NULL) {
		PRINT_ERROR("sk null");
return print_exit(__FUNCTION__, __LINE__, -1);
	}
	lock_sock(sk);

	sock_id = getUniqueSockID(sk);
	PRINT_DEBUG("Entered: sock_id=%llu", sock_id);

if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
//TODO error
}
	sock_index = find_wedge_socket(sock_id);
	PRINT_DEBUG("sock_index=%d", sock_index);
if (sock_index == -1) {
		up(&sockets_sem);
		release_sock(sk);
		return print_exit(__FUNCTION__, __LINE__, -1);
	}

	call_threads = ++wedge_sockets[sock_index].threads[setsockopt_call]; //TODO change to single int threads?
	up(&sockets_sem); //TODO move to later? lock_sock should guarantee

	if (down_interruptible(&calls_sem)) {
		PRINT_ERROR("calls_sem acquire fail");
//TODO error
}
	call_id = call_count++;
	call_index = insert_wedge_call(call_id, sock_id, sock_index, setsockopt_call);
	up(&calls_sem);
	PRINT_DEBUG("call_id=%u call_index=%d", call_id, call_index);
if (call_index == -1) {
		rc = -ENOMEM;
		goto end;
	}

	// Build the message
	buf_len = sizeof(struct nl_wedge_to_daemon) + 3 * sizeof(int) + optlen;
	buf = (u_char *) kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL) {
		PRINT_ERROR("buffer allocation error");
wedge_calls[call_index].call_id = -1;
		rc = -ENOMEM;
		goto end;
	}

	hdr = (struct nl_wedge_to_daemon *) buf;
	hdr->sock_id = sock_id;
	hdr->sock_index = sock_index;
	hdr->call_type = setsockopt_call;
	hdr->call_threads = call_threads;
	hdr->call_id = call_id;
	hdr->call_index = call_index;
	pt = buf + sizeof(struct nl_wedge_to_daemon);

	*(int *) pt = level;
	pt += sizeof(int);

	*(int *) pt = optname;
	pt += sizeof(int);

	*(u_int *) pt = optlen;
	pt += sizeof(u_int);

	ret = copy_from_user(pt, optval, optlen);
	pt += optlen;
	if (ret) {
		PRINT_ERROR("copy_from_user fail ret=%d", ret);
kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	if (pt - buf != buf_len) {
		PRINT_ERROR("write error: diff=%d len=%d", pt-buf, buf_len);
kfree(buf);
		wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}

	PRINT_DEBUG("socket_call=%d sock_id=%llu buf_len=%d", setsockopt_call, sock_id, buf_len);

// Send message to FINS_daemon
ret = nl_send(FINS_daemon_pid, buf, buf_len, 0);
	kfree(buf);
	if (ret) {
		PRINT_ERROR("nl_send failed");
wedge_calls[call_index].call_id = -1;
		rc = -1;
		goto end;
	}
	release_sock(sk);

	PRINT_DEBUG("waiting for reply: sk=%p, sock_id=%llu, sock_index=%d, call_id=%u, call_index=%d", sk, sock_id, sock_index, call_id, call_index);
if (down_interruptible(&wedge_calls[call_index].wait_sem)) {
		PRINT_ERROR("wedge_calls[%d].wait_sem acquire fail", call_index);
//TODO potential problem with wedge_calls[call_index].id = -1: frees call after nl_data_ready verify & filling info, 3rd thread inserting call
}
	PRINT_DEBUG("relocked my semaphore");

lock_sock(sk);
	PRINT_DEBUG("shared recv: sockID=%llu, call_id=%d, reply=%u, ret=%u, msg=%u, len=%d",
		wedge_calls[call_index].sock_id, wedge_calls[call_index].call_id, wedge_calls[call_index].reply, wedge_calls[call_index].ret, wedge_calls[call_index].msg, wedge_calls[call_index].len);
if (wedge_calls[call_index].reply) {
		rc = checkConfirmation(call_index);
	} else {
		rc = -1;
	}
	wedge_calls[call_index].call_id = -1;

	end: //
	if (down_interruptible(&sockets_sem)) {
		PRINT_ERROR("sockets_sem acquire fail");
//TODO error
}
	wedge_sockets[sock_index].threads[setsockopt_call]--;
	PRINT_DEBUG("wedge_sockets[%d].threads[%u]=%u", sock_index, setsockopt_call, wedge_sockets[sock_index].threads[setsockopt_call]);
up(&sockets_sem);

release_sock(sk);
return print_exit(__FUNCTION__, __LINE__, rc);
}

/* Data structures needed for protocol registration */
/* A proto struct for the dummy protocol */
static struct proto FINS_proto = { .name = "FINS_PROTO", .owner = THIS_MODULE, .obj_size = sizeof(struct FINS_sock), };

/* see IPX struct net_proto_family ipx_family_ops for comparison */
static struct net_proto_family FINS_net_proto = { .family = PF_FINS, .create = FINS_wedge_create, // This function gets called when socket() is called from userspace
	.owner = THIS_MODULE, };

/* Defines which functions get called when corresponding calls are made from userspace */
static struct proto_ops FINS_proto_ops = { .owner = THIS_MODULE, .family = PF_FINS, .release = FINS_release, .bind = FINS_bind, //sock_no_bind,
	.connect = FINS_connect, //sock_no_connect,
	.socketpair = FINS_socketpair, //sock_no_socketpair,
	.accept = FINS_accept, //sock_no_accept,
	.getname = FINS_getname, //sock_no_getname,
	.poll = FINS_poll, //sock_no_poll,
	.ioctl = FINS_ioctl, //sock_no_ioctl,
	.listen = FINS_listen, //sock_no_listen,
	.shutdown = FINS_shutdown, //sock_no_shutdown,
	.setsockopt = FINS_setsockopt, //sock_no_setsockopt,
	.getsockopt = FINS_getsockopt, //sock_no_getsockopt,
	.sendmsg = FINS_sendmsg, //sock_no_sendmsg,
	.recvmsg = FINS_recvmsg, //sock_no_recvmsg,
	.mmap = FINS_mmap, //sock_no mmap,
	.sendpage = FINS_sendpage, //sock_no_sendpage,
	};

/* Helper function to extract a unique socket ID from a given struct sock */
inline unsigned long long getUniqueSockID(struct sock *sk) {
return (unsigned long long) &(sk->__sk_common); // Pointer to sock_common struct as unique ident
}

/* Functions to initialize and teardown the protocol */
static void setup_FINS_protocol(void) {
int rc; // used for reporting return value

// Changing this value to 0 disables the FINS passthrough by default
// Changing this value to 1 enables the FINS passthrough by default
FINS_stack_passthrough_enabled = 1; // Initialize kernel wide FINS data passthrough

/* Call proto_register and report debugging info */
rc = proto_register(&FINS_proto, 1);
PRINT_DEBUG("proto_register returned: %d", rc);
PRINT_DEBUG("Made it through FINS proto_register()");

/* Call sock_register to register the handler with the socket layer */
rc = sock_register(&FINS_net_proto);
PRINT_DEBUG("sock_register returned: %d", rc);
PRINT_DEBUG("Made it through FINS sock_register()");
}

static void teardown_FINS_protocol(void) {
/* Call sock_unregister to unregister the handler with the socket layer */
sock_unregister(FINS_net_proto.family);
PRINT_DEBUG("Made it through FINS sock_unregister()");

/* Call proto_unregister and report debugging info */
proto_unregister(&FINS_proto);
PRINT_DEBUG("Made it through FINS proto_unregister()");
}

/* Functions to initialize and teardown the netlink socket */
static int setup_FINS_netlink(void) {
// nl_data_ready is the name of the function to be called when the kernel receives a datagram on this netlink socket.
FINS_nl_sk = netlink_kernel_create(&init_net, NETLINK_FINS, 0, nl_data_ready, NULL, THIS_MODULE);
if (FINS_nl_sk == NULL) {
	PRINT_ERROR("Error creating socket.");
	return -10;
}

sema_init(&link_sem, 1);

return 0;
}

static void teardown_FINS_netlink(void) {
// closes the netlink socket
if (FINS_nl_sk != NULL) {
	sock_release(FINS_nl_sk->sk_socket);
}
}

/* LKM specific functions */
/*
 * Note: the init and exit functions must be defined (or declared/declared in header file) before the macros are called
 */
static int __init FINS_stack_wedge_init(void) {
	PRINT_DEBUG("############################################");
PRINT_DEBUG("Loading the FINS_stack_wedge module");
setup_FINS_protocol();
	setup_FINS_netlink();
	init_wedge_calls();
	init_wedge_sockets();
	PRINT_DEBUG("Made it through the FINS_stack_wedge initialization");
return 0;
}

static void __exit FINS_stack_wedge_exit(void) {
	PRINT_DEBUG("Unloading the FINS_stack_wedge module");
teardown_FINS_netlink();
	teardown_FINS_protocol();
	PRINT_DEBUG("Made it through the FINS_stack_wedge removal");
 // the system call wrapped by rmmod frees all memory that is allocated in the module
}

/* Macros defining the init and exit functions */
module_init( FINS_stack_wedge_init);
module_exit( FINS_stack_wedge_exit);

/* Set the license and signing info for the module */
MODULE_LICENSE(M_LICENSE);
MODULE_DESCRIPTION(M_DESCRIPTION);
MODULE_AUTHOR(M_AUTHOR);
