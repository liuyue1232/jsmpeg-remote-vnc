/***
*  服务器端程序，用websocket实现推流
*/
#include <stdlib.h>
#include <stdio.h>
#include "server.h"

#pragma comment(lib, "websockets_static.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Ws2_32.lib")
//函数功能： 插入新客户端（websocket每建立一次连接，插入一次，在154行调用）
client_t *client_insert(client_t **head, libwebsocket *socket) {
	client_t *client = (server_client_t *)malloc(sizeof(server_client_t));
	client->socket = socket;
	client->next = *head;
	*head = client;
	return client;
}
//函数功能： 移除客户端（websocket每断开一次连接，移除一次，在168行调用）
void client_remove(client_t **head, libwebsocket *socket) {
	for( client_t **current = head; *current; current = &(*current)->next ) {
		if( (*current)->socket == socket ) {
			client_t* next = (*current)->next;
			free(*current);
			*current = next;
			break;
		}
	}
}

//函数声明： 两个回调函数
static int callback_http(struct libwebsocket_context *, struct libwebsocket *, enum libwebsocket_callback_reasons, void *, void *, size_t);
static int callback_websockets(struct libwebsocket_context *, struct libwebsocket *, enum libwebsocket_callback_reasons,void *, void *, size_t);
//ws服务端在同一个端口支持2种协议,http和ws(具体是ws-protocol-example,实际上可以再多几个ws协议)
//结构体声明通讯协议：  此处为http 
static struct libwebsocket_protocols server_protocols[] = {
	{ "http-only", callback_http, 0 },//第一个协议必须是http协议
	{ NULL, callback_websockets, sizeof(int), 1024*1024 },
	{ NULL, NULL, 0 /* End of list */ }
};

//函数功能： 创建WebSocket连接对象
server_t *server_create(int port, size_t buffer_size) {
	server_t *self = (server_t *)malloc(sizeof(server_t));      //分配内存空间
	memset(self, 0, sizeof(server_t));                                 //赋初值，初始化为0
	
	self->buffer_size = buffer_size;                                  //设置连接对象缓存区大小
	size_t full_buffer_size = LWS_SEND_BUFFER_PRE_PADDING + buffer_size + LWS_SEND_BUFFER_POST_PADDING;
	self->send_buffer_with_padding = (unsigned char *)malloc(full_buffer_size);
	self->send_buffer = &self->send_buffer_with_padding[LWS_SEND_BUFFER_PRE_PADDING];

	self->port = port;                                                       //设置端口
	self->clients = NULL;

	lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

	struct lws_context_creation_info info = {0};
	info.port = port;
	info.gid = -1;
	info.uid = -1;
	info.user = (void *)self;
	info.protocols = server_protocols;
	self->context = libwebsocket_create_context(&info);

	if( !self->context ) {
		server_destroy(self);
		return NULL;
	}

	return self;
}
//函数功能： 关闭WebSocket连接对象
void server_destroy(server_t *self) {
	if( self == NULL ) { return; }

	if( self->context ) {
		libwebsocket_context_destroy(self->context); //断开数据连接
	}
	
	free(self->send_buffer_with_padding);//释放内存
	free(self);
}
//函数功能： 获取服务器本地ip地址,此处获取的是内网ip，和端口号
char *server_get_host_address(server_t *self) {
	char host_name[80];
	hostent *host;
	if( 
		gethostname(host_name, sizeof(host_name)) == SOCKET_ERROR ||
		!(host = gethostbyname(host_name))
	) {
		return "127.0.0.1";
	}
	return "127.0.0.1";
	//return inet_ntoa( *(IN_ADDR *)(host->h_addr_list[0]) );
}

//函数功能： 获取客户端ip地址,用于往客户端推流
char *server_get_client_address(server_t *self, libwebsocket *wsi) {
	static char ip_buffer[32];
	static char name_buffer[32];

	libwebsockets_get_peer_addresses(
		self->context, wsi, libwebsocket_get_socket_fd(wsi), 
		name_buffer, sizeof(name_buffer), 
		ip_buffer, sizeof(ip_buffer)
	);
	return ip_buffer;
}

void server_update(server_t *self) {
	libwebsocket_callback_on_writable_all_protocol(&(server_protocols[1]));
	libwebsocket_service(self->context, 0);
}
//函数功能：服务器端发送桌面视频流到单一客户端
void server_send(server_t *self, libwebsocket *socket, void *data, size_t size, server_data_type_t type) {
	// Caution, this may explode! The libwebsocket docs advise against ever calling libwebsocket_write()
	// outside of LWS_CALLBACK_SERVER_WRITEABLE. Honoring this advise would complicate things quite
	// a bit - and it seems to work just fine on my system as it is anyway.
	// This won't work reliably on mobile systems where network buffers are typically much smaller.
	// ¯\_(ツ)_/¯

	if( size > self->buffer_size ) {//如果数据大于缓存，则报错
		printf("Cant send %d bytes; exceeds buffer size (%d bytes)\n", size, self->buffer_size);
		return;
	}
	memcpy(self->send_buffer, data, size);//将数据拷贝至发送区缓存
	libwebsocket_write(socket, self->send_buffer, size, (libwebsocket_write_protocol)type);//启动发送
}

//函数功能：服务器端广播发送桌面视频流，到多个客户端
void server_broadcast(server_t *self, void *data, size_t size, server_data_type_t type) {
	if( size > self->buffer_size ) {
		printf("Cant send %d bytes; exceeds buffer size (%d bytes)\n", size, self->buffer_size);
		return;
	}
	memcpy(self->send_buffer, data, size);

	client_foreach(self->clients, client) {//client_foreach为头文件中宏定义的函数， 功能为轮询所有客户端
		libwebsocket_write(client->socket, self->send_buffer, size, (libwebsocket_write_protocol)type);
	}
	libwebsocket_callback_on_writable_all_protocol(&(server_protocols[1]));
}
//函数功能：回调函数，对各种异常情况做回应
static int callback_websockets(
	struct libwebsocket_context *context,
	struct libwebsocket *wsi,//套接字
	enum libwebsocket_callback_reasons reason,
	void *user, void *in, size_t len
) {
	server_t *self = (server_t *)libwebsocket_context_user(context);

	switch( reason ) {
		case LWS_CALLBACK_ESTABLISHED:// 当服务器和客户端每完成一次握手后
			client_insert(&self->clients, wsi);//插入一个客户端

			if( self->on_connect ) {
				self->on_connect(self, wsi);
			}
			break;

		case LWS_CALLBACK_RECEIVE:// 当服务器接收到客户端发来的数据帧以后
			if( self->on_message ) {
				self->on_message(self, wsi, in, len);
			}
			break;

		case LWS_CALLBACK_CLOSED:// 当服务器与客户端每断开一次连接
			client_remove(&self->clients, wsi);//移除一个客户端		
		
			if( self->on_close ) {
				self->on_close(self, wsi);
			}
			break;
	}
	
	return 0;
}
//函数功能：回调函数，对各种异常情况做回应
static int callback_http(
	struct libwebsocket_context *context,
	struct libwebsocket *wsi,
	enum libwebsocket_callback_reasons reason, void *user,
	void *in, size_t len
) {
	server_t *self = (server_t *)libwebsocket_context_user(context);
	
	if( reason == LWS_CALLBACK_HTTP ) {
		if( self->on_http_req && self->on_http_req(self, wsi, (char *)in) ) {
			return 0;
		}
		libwebsockets_return_http_status(context, wsi, HTTP_STATUS_NOT_FOUND, NULL);
		return 0;
	}
	
	return 0;
}

