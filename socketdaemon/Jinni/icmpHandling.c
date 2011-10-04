/*
 * icmphandling.c
 *
 *  Created on: Jun 29, 2011
 *      Author: dell
 */

#include "icmpHandling.h"
#include "finstypes.h"

#define	IP4_PT_ICMP		1

extern struct finssocket jinniSockets[MAX_sockets];
extern finsQueue Jinni_to_Switch_Queue;
extern finsQueue Switch_to_Jinni_Queue;
extern sem_t *meen_channel_semaphore1;
extern sem_t *meen_channel_semaphore2;
extern sem_t Jinni_to_Switch_Qsem;
extern sem_t Switch_to_Jinni_Qsem;
//extern struct socketIdentifier FinsHistory[MAX_sockets];





int jinni_ICMP_to_fins(u_char *dataLocal, int len, uint16_t dstport,
		uint32_t dst_IP_netformat, uint16_t hostport,
		uint32_t host_IP_netformat) {

	struct finsFrame *ff = (struct finsFrame *) malloc(sizeof(struct finsFrame));

	metadata *udpout_meta = (metadata *) malloc(sizeof(metadata));

	PRINT_DEBUG();

	metadata_create(udpout_meta);

	if (udpout_meta == NULL) {
		PRINT_DEBUG("metadata creation failed");
		free(ff);
		exit(1);

	}

	/** metadata_writeToElement() set the value of an element if it already exist
	 * or it creates the element and set its value in case it is new
	 */
	PRINT_DEBUG("%d, %d, %d, %d", dstport,dst_IP_netformat,hostport,
			host_IP_netformat);

	uint32_t dstprt = dstport;
		uint32_t hostprt = hostport;
		int protocol = IP4_PT_ICMP;
	metadata_writeToElement(udpout_meta, "dstport", &dstprt, META_TYPE_INT);
	metadata_writeToElement(udpout_meta, "srcport", &hostprt, META_TYPE_INT);
	metadata_writeToElement(udpout_meta, "dstip", &dst_IP_netformat,
			META_TYPE_INT);
	metadata_writeToElement(udpout_meta, "srcip", &host_IP_netformat,
			META_TYPE_INT);
	metadata_writeToElement(udpout_meta, "protocol", &protocol,
					META_TYPE_INT);
	ff->dataOrCtrl = DATA;
	/**TODO get the address automatically by searching the local copy of the
	 * switch table
	 */
	ff->destinationID.id = IPV4ID;
	ff->destinationID.next = NULL;
	(ff->dataFrame).directionFlag = DOWN;
	(ff->dataFrame).pduLength = len;
	(ff->dataFrame).pdu = dataLocal;
	(ff->dataFrame).metaData = udpout_meta;

	/**TODO insert the frame into jinni_to_switch queue
	 * check if insertion succeeded or not then
	 * return 1 on success, or -1 on failure
	 * */
	PRINT_DEBUG("");
	sem_wait(&Jinni_to_Switch_Qsem);
	if (write_queue(ff, Jinni_to_Switch_Queue)) {

		sem_post(&Jinni_to_Switch_Qsem);
		PRINT_DEBUG("");
		return (1);
	}
	sem_post(&Jinni_to_Switch_Qsem);
	PRINT_DEBUG("");

	return (0);

}
int ICMPreadFrom_fins(int senderid, int sockfd, u_char **buf, int *buflen,
		int symbol, struct sockaddr_in *address, int block_flag){

		/**TODO MUST BE FIXED LATER
		 * force symbol to become zero
		 */
		//symbol = 0;
		struct finsFrame *ff = NULL;
		int index;
		uint16_t srcport;
		uint32_t srcip;
		struct sockaddr_in * addr_in = (struct sockaddr_in *) address;
		index = findjinniSocket(senderid, sockfd);
		PRINT_DEBUG("index = %d",index);
		/**
		 * It keeps looping as a bad method to implement the blocking feature
		 * of recvfrom. In case it is not blocking then the while loop should
		 * be replaced with only a single trial !
		 * TODO Replace the dataqueue with a pipeline (file) this will make it easier
		 * to emulate the file characteristics of the socket such as blocking and non-blocking
		 *
		 */

		PRINT_DEBUG();
		if (block_flag == 1) {
			PRINT_DEBUG();

			do {

				sem_wait(&(jinniSockets[index].Qs));
				//		PRINT_DEBUG();


				ff = read_queue(jinniSockets[index].dataQueue);
				//	ff = get_fake_frame();
				//					PRINT_DEBUG();

				sem_post(&(jinniSockets[index].Qs));
			} while (ff == NULL);
			PRINT_DEBUG();

		} else {
			PRINT_DEBUG();

			sem_wait(&(jinniSockets[index].Qs));
			//ff= read_queue(jinniSockets[index].dataQueue);
			/**	ff = get_fake_frame();
			 print_finsFrame(ff); */
			ff = read_queue(jinniSockets[index].dataQueue);

			sem_post(&(jinniSockets[index].Qs));

		}

		if (ff == NULL) {
			//free(ff);
			return (0);
		}
		PRINT_DEBUG("PDU length %d",ff->dataFrame.pduLength);

		if (metadata_readFromElement(ff->dataFrame.metaData, "portsrc",(uint16_t *) &srcport)
				== 0) {
			addr_in->sin_port = 0;

		}
		if (metadata_readFromElement(ff->dataFrame.metaData, "ipsrc",(uint32_t *) &srcip) == 0) {
			addr_in->sin_addr.s_addr = 0;

		}

		/**
		 * making sure that the datagram coming from the destination we are connected to it
		 * in case of connection previously done
		 */

		if (jinniSockets[index].connection_status > 0){

			if ( (srcport != jinniSockets[index].dstport) || (srcip != jinniSockets[index].dst_IP ) )
				{
					PRINT_DEBUG("Wrong address, the socket is already connected to another destination");
					return(0);


				}


		}



		//*buf = (u_char *)malloc(sizeof(ff->dataFrame.pduLength));
		//memcpy(*buf,ff->dataFrame.pdu,ff->dataFrame.pduLength);
		memcpy(buf, ff->dataFrame.pdu, ff->dataFrame.pduLength);
		*buflen = ff->dataFrame.pduLength;

		PRINT_DEBUG();

		if (symbol == 0) {
			//		address = NULL;
			PRINT_DEBUG();
			//	freeFinsFrame(ff);

			return (1);
		} PRINT_DEBUG();


		addr_in->sin_family =AF_INET;
		addr_in->sin_port = srcport;
		addr_in->sin_addr.s_addr = srcip;


		/**TODO Free the finsFrame
		 * This is the final consumer
		 * call finsFrame_free(Struct finsFrame** ff)
		 */
		PRINT_DEBUG();

		//freeFinsFrame(ff);

		/** Finally succeeded
		 *
		 */
		return (1);

}



void socket_icmp(int domain, int type, int protocol, int sockfd, int fakeID,pid_t processid){

	char clientName[200];
		int index;
		int pipe_desc;
		int tester;
		/** TODO lock the pipe semaphore then open the pipe*/

		insertjinniSocket(processid, sockfd, fakeID, type, protocol);

		PRINT_DEBUG();
		if ( sprintf(clientName, CLIENT_CHANNEL_RX, processid, fakeID) < 0 ){
			PRINT_DEBUG("sprintf Failed");
		}

		/** Crashing at error because there is no way to send a NACK
		 * application process
		 */
		if  (mkfifo(clientName, 0777) == -1 ){
			PRINT_DEBUG("mkfifo  Failed CRASH");
			exit(1);
		}
		pipe_desc = open(clientName, O_WRONLY);
		index = findjinniSocket(processid, sockfd);

		if (index < 0) {
			PRINT_DEBUG("incorrect index !! Crash");
			exit(1);

		} PRINT_DEBUG("0000");

		jinniSockets[index].jinniside_pipe_ds = pipe_desc;
		/** Now the client can proceed to next step after openning the pipe */
		PRINT_DEBUG("0002");
		sem_getvalue(jinniSockets[index].s, &tester);
		PRINT_DEBUG("tester = %d", tester);

		PRINT_DEBUG("0001");

		sem_wait(jinniSockets[index].s);
		ack_write(pipe_desc, processid, sockfd);
		sem_post(jinniSockets[index].as);
		/** TODO unlock the semaphore */
		sem_post(jinniSockets[index].s);
		PRINT_DEBUG("0003");

		return;





}

void write_icmp(int senderid, int sockfd, int datalen, u_char *data){



}


void recv_icmp(int senderid, int sockfd, int datalen, int flags){




}




void sendto_icmp(int senderid, int sockfd, int datalen, u_char *data, int flags,
		struct sockaddr *addr, socklen_t addrlen){


//	int index;
//
//		index = findjinniSocket(senderid, sockfd);
//		PRINT_DEBUG("");
//
//		/** TODO unlock access to the jinnisockets */
//		if (index == -1) {
//			PRINT_DEBUG("CRASH !! socket descriptor not found into jinni sockets");
//			exit(1);
//		}
//
//
//		sem_wait(jinniSockets[index].s);
//		PRINT_DEBUG("");
//
//			ack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
//			sem_post(jinniSockets[index].as);
//
//		sem_post(jinniSockets[index].s);
//			PRINT_DEBUG("");
//			return;
	uint16_t hostport;
	uint16_t dstport;
	uint32_t host_IP;
	uint32_t dst_IP;

	int len = datalen;
	int index;

	PRINT_DEBUG();

	/** TODO handle flags cases */
	switch (flags) {
	case MSG_CONFIRM:
	case MSG_DONTROUTE:
	case MSG_DONTWAIT:
	case MSG_EOR:
	case MSG_MORE:
	case MSG_NOSIGNAL:
	case MSG_OOB: /** case of recieving a (write call)*/
	default:
		break;

	} PRINT_DEBUG("");

	struct sockaddr_in *address;
	address = (struct sockaddr_in *) addr;
	/** TODO lock access to the jinnisockets */

	index = findjinniSocket(senderid, sockfd);
	PRINT_DEBUG("");

	/** TODO unlock access to the jinnisockets */
	if (index == -1) {
		PRINT_DEBUG("CRASH !! socket descriptor not found into jinni sockets");
		exit(1);
	}

	if (address->sin_family != AF_INET) {
		PRINT_DEBUG("Wrong address family"); PRINT_DEBUG("");

		sem_wait(jinniSockets[index].s);
		PRINT_DEBUG("");

		nack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
		sem_post(jinniSockets[index].as);

		sem_post(jinniSockets[index].s);
		PRINT_DEBUG("");

	}

	/** copying the data passed to be able to free the old memory location
	 * the new created location is the one to be included into the newly created finsFrame*/
	PRINT_DEBUG("");

	/** Keep all ports and addresses in host order until later  action taken */
	dstport = ntohs(address->sin_port); /** reverse it since it is in network order after application used htons */

	dst_IP = ntohl(address-> sin_addr.s_addr);/** it is in network format since application used htonl */
	/** addresses are in host format given that there are by default already filled
	 * host IP and host port. Otherwise, a port and IP has to be assigned explicitly below */

	/**
	 * Default current host port to be assigned is 58088
	 * It is supposed to be randomly selected from the range found in
	 * /proc/sys/net/ipv4/ip_local_port_range
	 * default range in Ubuntu is 32768 - 61000
	 * The value has been chosen randomly when the socket firstly inserted into the jinnisockets
	 * check insertjinniSocket(processid, sockfd, fakeID, type, protocol);
	 */
	hostport = jinniSockets[index].hostport;
	/**
	 * the current value of host_IP is zero but to be filled later with
	 * the current IP using the IPv4 modules unless a binding has occured earlier
	 */
	host_IP = jinniSockets[index].host_IP;

	PRINT_DEBUG("");

	PRINT_DEBUG("%d,%d,%d,%d", dst_IP, dstport, host_IP,hostport);
	//free(data);
	//free(addr);
	PRINT_DEBUG("");

	/** the meta-data paraters are all passes by copy starting from this point
	 *
	 */
	if (jinni_ICMP_to_fins(data, len, dstport, dst_IP, hostport, host_IP) == 1)

	{
		PRINT_DEBUG("");
		/** TODO prevent the socket interceptor from holding this semaphore before we reach this point */
		sem_wait(jinniSockets[index].s);
		PRINT_DEBUG("");

		ack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
		sem_post(jinniSockets[index].as);

		sem_post(jinniSockets[index].s);
		PRINT_DEBUG("");

	} else {
		PRINT_DEBUG("socketjinni failed to accomplish sendto");
		sem_wait(jinniSockets[index].s);
		nack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
		sem_post(jinniSockets[index].as);

		sem_post(jinniSockets[index].s);

	}

	return;

}
void recvfrom_icmp(int senderid, int sockfd, int datalen, int flags, int symbol){


	/** symbol parameter is the one to tell if an address has been passed from the
		 * application to get the sender address or not
		 */

		//	u_char *buf=NULL;
		//	buf = (u_char *)malloc(MAX_DATA_PER_UDP);
		u_char buf[MAX_DATA_PER_UDP];

		u_char *bufptr;
		bufptr = buf;
		struct sockaddr_in *address;
		int buflen = 0;
		int index;
		int i;
		int blocking_flag;
		int addressLen = sizeof(struct sockaddr_in);
		/** TODO lock access to the jinnisockets */

			index = findjinniSocket(senderid, sockfd);
			/** TODO unlock access to the jinnisockets */
			if (index == -1) {
				PRINT_DEBUG("socket descriptor not found into jinni sockets");
				exit(1);
			}
			if (jinniSockets[index].protocol == IPPROTO_ICMP)
				symbol = 1;


		if (symbol == 1)
			address = (struct sockaddr_in *) malloc(sizeof(struct sockaddr_in));
		address = NULL;
		/** TODO handle flags cases */
		switch (flags) {

		default:
			break;

		}



		PRINT_DEBUG("index = %d",index); PRINT_DEBUG();
		blocking_flag = jinniSockets[index].blockingFlag;

		/** the meta-data parameters are all passed by copy starting from this point
		 *
		 */

		if (ICMPreadFrom_fins(senderid, sockfd, bufptr, &buflen, symbol, address,
				blocking_flag) == 1) {

			if (symbol == 0) {
				sem_wait(jinniSockets[index].s);

				ack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
				buf[buflen] = '\0';
				PRINT_DEBUG("%d",buflen ); PRINT_DEBUG("%s",buf);
				write(jinniSockets[index].jinniside_pipe_ds, &buflen, sizeof(int));
				write(jinniSockets[index].jinniside_pipe_ds, buf, buflen);
				sem_post(jinniSockets[index].as);
				sem_post(jinniSockets[index].s);
				PRINT_DEBUG();

				//	free(buf);
				PRINT_DEBUG();

			} else if (symbol == 1) {

				sem_wait(jinniSockets[index].s);

				ack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
				PRINT_DEBUG();
				write(jinniSockets[index].jinniside_pipe_ds, &addressLen, sizeof(socklen_t));
				write(jinniSockets[index].jinniside_pipe_ds, address,sizeof(struct sockaddr_in));
				write(jinniSockets[index].jinniside_pipe_ds, &buflen, sizeof(int));
				write(jinniSockets[index].jinniside_pipe_ds, buf, buflen);

				sem_post(jinniSockets[index].as);

				sem_post(jinniSockets[index].s);
			} else {

			}

		} else {
			PRINT_DEBUG("socketjinni failed to accomplish recvfrom");
			sem_wait(jinniSockets[index].s);
			nack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);

			sem_post(jinniSockets[index].as);

			sem_post(jinniSockets[index].s);
		} PRINT_DEBUG();

		/** TODO find a way to release these buffers later
		 * using free here causing a segmentation fault
		 */
		//free(address);
		//free(buf);

		return;











}
void sendmsg_icmp(){



}
void recvmsg_icmp(){



}
void getsockopt_icmp(int senderid, int sockfd, int level, int optname, int optlen, void *optval){

	int index;


	int optvalue=-1;
		index = findjinniSocket(senderid, sockfd);
		/** TODO unlock access to the jinnisockets */
		if (index == -1) {
			PRINT_DEBUG("socket descriptor not found into jinni sockets");
			exit(1);
		}

		PRINT_DEBUG("index = %d",index); PRINT_DEBUG();

		sem_wait(jinniSockets[index].s);

			ack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);
			write(jinniSockets[index].jinniside_pipe_ds,&optlen,sizeof(socklen_t));


			switch (level){
			case SOL_SOCKET:
				switch (optname){
				case SO_RCVBUF :
					/** This is just a dummy value taken from strace but this will change to actu
					 * value once the socket options get fully implemented
					 */

					optvalue = 131072;
					write(jinniSockets[index].jinniside_pipe_ds,&optvalue,optlen);

				}
				default:
					break;

			}


	sem_post(jinniSockets[index].as);
			sem_post(jinniSockets[index].s);


			return;




}
void setsockopt_icmp(int senderid, int sockfd, int level, int optname, int optlen, void *optval){

	int index;



	index = findjinniSocket(senderid, sockfd);
	/** TODO unlock access to the jinnisockets */
	if (index == -1) {
		PRINT_DEBUG("socket descriptor not found into jinni sockets");
		exit(1);
	}

	PRINT_DEBUG("index = %d",index); PRINT_DEBUG();


	sem_wait(jinniSockets[index].s);

		ack_write(jinniSockets[index].jinniside_pipe_ds, senderid, sockfd);


	sem_post(jinniSockets[index].as);
	sem_post(jinniSockets[index].s);


	return;




}


void shutdown_icmp(int senderid,int sockfd,int  how){







}