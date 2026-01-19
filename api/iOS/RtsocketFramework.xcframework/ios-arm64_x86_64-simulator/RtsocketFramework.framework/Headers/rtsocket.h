#if !defined(RTSOCKET_H__FB063327_573C_4D70_A0B4_ECC067B6F4FD__INCLUDED_)
#define RTSOCKET_H__FB063327_573C_4D70_A0B4_ECC067B6F4FD__INCLUDED_

//mode
#define RTSM_LOW_LATENCY		0
#define RTSM_HIGH_THROUGHPUT	1

//
#define RTSO_MTU				0x10001
#define RTSO_FEC				0x10002
#define RTSO_FAST_ACK			0x10003
#define RTSO_RCVBUF				0x10004

//get only param
#define RTSO_RTT				0x20001
#define RTSO_LOST_RATE			0x20002
#define RTSO_RECENT_LOST_RATE	0x20003

//set only param
#define RTSO_MODE				0x30001
#define RTSO_FEC_INTERVAL		0x30002

#define RTTP_EWOULDBLOCK			-1

#define RTTP_EINVAL					-100
#define RTTP_ENOTCONN				-101
#define RTTP_ECONNABORTED			-102
#define RTTP_ETIMEDOUT				-103
#define RTTP_ECONNRESET				-104
#define RTTP_EINVALID_SOCKET		-105
#define RTTP_EINSUFFICIENT_BUFFER	-106
#define RTTP_ESTATE_ERROR			-107

#if defined(RTDLL)
#if defined(_MSC_VER)
    #define RTEXPORT __declspec(dllexport)
#else
    #define RTEXPORT __attribute__((visibility("default")))
#endif
#else
#define RTEXPORT
#endif

#define RTTP_EVENT_CONNECT	1
#define RTTP_EVENT_READ		2
#define RTTP_EVENT_WRITE	3
#define RTTP_EVENT_ERROR	4


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
typedef int SOCKET;
#define SOCKET_ERROR -1
#endif

#if defined(__cplusplus)
extern "C"
{
#endif
	typedef void* RTEngine;
	typedef void* RTSOCKET;

	typedef void socket_event_callback(RTEngine engine, RTSOCKET socket, int event);
	typedef void send_data_callback(RTEngine engine, RTSOCKET socket, const char * buffer, int len, const struct sockaddr * addr, int addr_len);

	
	RTEXPORT RTEngine rt_init(const char* init_str);
	RTEXPORT void rt_set_callback(RTEngine engine, socket_event_callback *event_callback, send_data_callback *sendproc);

	RTEXPORT RTSOCKET rt_socket(RTEngine engine, int mode);
	RTEXPORT int rt_connect(RTEngine engine, RTSOCKET s, const struct sockaddr *to, int tolen);
	RTEXPORT int rt_recv(RTEngine engine, RTSOCKET s, char * buffer, int len, int flag);
	RTEXPORT int rt_send(RTEngine engine, RTSOCKET s, const char * buffer, int len, int flag);
	RTEXPORT void rt_close(RTEngine engine, RTSOCKET s);

	RTEXPORT RTSOCKET rt_incoming_packet(RTEngine engine, const char * buffer, int len, const struct sockaddr *from, int from_len);
	RTEXPORT int rt_accept(RTEngine engine, RTSOCKET s);

	RTEXPORT int rt_tick(RTEngine engine);

	RTEXPORT int rt_getpeername(RTEngine engine, RTSOCKET s, struct sockaddr *name, int len);
	RTEXPORT int rt_get_error(RTEngine engine, RTSOCKET s);
	RTEXPORT int rt_connected(RTEngine engine, RTSOCKET s);
	RTEXPORT int rt_writable(RTEngine engine, RTSOCKET s);
	RTEXPORT int rt_readable(RTEngine engine, RTSOCKET s);
	RTEXPORT int rt_setsockopt(RTEngine engine, RTSOCKET s, int optname, void *optval, int optlen);
	RTEXPORT int rt_getsockopt(RTEngine engine, RTSOCKET s, int optname, void *optval, int optlen);

	RTEXPORT void* rt_get_userdata(RTEngine engine, RTSOCKET s, int index);
	RTEXPORT void rt_set_userdata(RTEngine engine, RTSOCKET s, int index, void *userdata);

	RTEXPORT int rt_state_desc(RTEngine engine, RTSOCKET s, char * desc, int len);

	/* Version information */
	RTEXPORT const char* rt_get_version(void);

	RTEXPORT void rt_uninit(RTEngine engine);

#if defined(__cplusplus)
}
#endif


#endif
