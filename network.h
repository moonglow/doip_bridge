#pragma once
#include <stdint.h>

#define MAX_CLIENT_BUFFER_SIZE (5*1024)

uint16_t __inline __swap_u16( uint16_t v )
{
  return (v>>8)|(v<<8);
}

uint32_t __inline __swap_u32( uint32_t v )
{
  return (v>>24)|(v<<24)|((v<<8)&0x00FF0000)|((v>>8)&0x0000FF00);
}

struct t_doip_client
{
  void *user_data;
  int socket;
  int is_udp;
  uint16_t port;
  uint32_t ip;
  uint8_t  data[MAX_CLIENT_BUFFER_SIZE];
  int      pos;
};

struct t_doip_server
{
  struct t_doip_client *pclients;
  int nactive;
  int nclients;
  int stop_polling;
  int server_socket;
  int udp_br_socket;
  uint32_t ip;
  uint16_t port;
  int (*on_connect)( struct t_doip_client *pclient );
  int (*on_disconnect)( struct t_doip_client *pclient );
  int (*on_receive)( struct t_doip_client *pclient );
};

int network_doip_init( void );
int network_doip_deinit( void );
void network_delay_us( uint32_t us );
int network_doip_send( struct t_doip_client *pc, void *p, int len );
int network_doip_recv( struct t_doip_client *pc, void *p, int len );
int network_doip_server_create( struct t_doip_server *pserver );
int network_doip_server_start( struct t_doip_server *pserver );



