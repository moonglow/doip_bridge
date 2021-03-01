#include "network.h"

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#define closesocket( _x ) close( _x )
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <threads.h>

#define SHOW_PARTIAL (32)
static void network_dump( uint8_t *p, int size )
{
  if( size > SHOW_PARTIAL )
  {
    printf( "%d/%d bytes\n", SHOW_PARTIAL, size );
    size = SHOW_PARTIAL;
  }
  for( int i = 0 ; i < size; i++ )
  {
    if( ( i % 16 ) == 0 )
      printf( "\n\t" );
    printf( "%.2X ", p[i] );
  }
  printf( "\n" );
}

static int highest_fd( fd_set *set )
{
  int max = 0;
  for( int i = 0; i < (int)set->fd_count; i++ )
  {
    if( (int)set->fd_array[i] > max )
      max = set->fd_array[i]; 
  }

  return max;
}

static struct t_doip_client *find_client( struct t_doip_server *pserver, int fd )
{
  for( int i = 0; i < pserver->nclients; i++ )
  {
    if( pserver->pclients[i].socket == fd )
      return &pserver->pclients[i];
  }
  return (struct t_doip_client *)0;
}

int network_doip_init( void )
{
#if _WIN32
  WSADATA wsaData;
  return WSAStartup( MAKEWORD(2, 2), &wsaData );
#else
  return 0;
#endif
}

int network_doip_deinit( void )
{
#if _WIN32
  return WSACleanup();
#else
  return 0;
#endif
}

void network_delay_us( uint32_t us )
{
  struct timespec ts = 
  {
    .tv_sec = us/1000000u,
    .tv_nsec = (us%1000000u)*1000u
  };

  (void)thrd_sleep( &ts, 0 );
}

int network_doip_server_create( struct t_doip_server *pserver )
{
  int sck, res;
  struct sockaddr_in src = { 0 };

  sck = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
  if( sck < 0 )
  {
    printf( "udp socket creation failed\n" );
    return -1;
  }

  src.sin_family = AF_INET;
  src.sin_port = htons( pserver->port );
  src.sin_addr.s_addr = htonl( INADDR_ANY );

  res = 1;
  /* allow bind to the same address */
  res = setsockopt( sck, SOL_SOCKET, SO_REUSEADDR, (char*)&res, sizeof(res) );
  if( res < 0 )
  {
    closesocket( sck );
    return -2;
  }

  res = bind( sck, (struct sockaddr *)&src, sizeof (src) );
  if ( res < 0)
  {
    closesocket( sck );
    return -3;
  }

  pserver->udp_br_socket = sck;

  sck = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
  if( sck < 0 )
    return -1;

  src.sin_family = AF_INET;
  src.sin_port = htons( pserver->port );
  src.sin_addr.s_addr = htonl( INADDR_ANY );
  
  res = bind( sck, (struct sockaddr *)&src, sizeof (src) );
  if ( res < 0)
  {
    closesocket( sck );
    printf( "bind failed\n" );
    return -2;
  }

  if( listen( sck, pserver->nclients ) != 0 )
  {
    closesocket( sck );
    printf( "listen failed\n" );
    return -3;
  }
  
  res = sizeof( struct t_doip_client )*pserver->nclients; 
  pserver->pclients = malloc( res );
  memset( pserver->pclients, 0x00, res );
  pserver->server_socket = sck;    
  
  /* add UDP client */
  struct t_doip_client *pclient = find_client( pserver, 0 );
  assert( pclient );

  pclient->socket = pserver->udp_br_socket;
  pclient->is_udp = 1;
  pserver->nactive++;
  return 0;
}

int socket_ip_to_string( uint32_t ip, char *out )
{
  struct in_addr addr = { .S_un.S_addr = ip };
  char *s = inet_ntoa( addr );
  if( !s )
    return -1;
  
  return strlen( strcpy( out, s ) );
}

int network_doip_send( struct t_doip_client *pc, void *p, int len )
{
  int res;

  if( pc->is_udp )
  {
    struct sockaddr_in dst = { 0 };

    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl( pc->ip );
    dst.sin_port = htons( pc->port );

    res = sendto( pc->socket, (void*)p, len, 0, (struct sockaddr*)&dst, sizeof(dst) );
  }
  else
  {
    res = send( pc->socket, p, len, 0 );
  }

  if( res > 0 )
  {
    network_dump( p, len );
  }
  return res;
}

int network_doip_recv( struct t_doip_client *pc, void *p, int len )
{
  int res;

  if( !pc )
    return -1;

  if( pc->is_udp )
  {
    struct sockaddr_in who = { 0 };
    res = sizeof( struct sockaddr_in );
    res = recvfrom( pc->socket, (void*)p, len, 0, (void*)&who, (void*)&res );
    if( res > 0 )
    {
      pc->port = ntohs( who.sin_port );
      pc->ip = ntohl( who.sin_addr.s_addr );
    }
  }
  else
  {
    res = recv( pc->socket, p, len, 0 );
  }

  if( res > 0 )
  {
    network_dump( p, len );
  }
  return res;
}

int network_doip_server_start( struct t_doip_server *pserver )
{
  struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 /* 100ms */ };
  fd_set main_set;
  fd_set read_set;
  int fd_hi;
  int res;

  FD_ZERO( &main_set );
  FD_SET( pserver->server_socket, &main_set );
  FD_SET( pserver->udp_br_socket, &main_set );
  
  for( ;pserver->stop_polling == 0; )
  {
    read_set = main_set;
    fd_hi = highest_fd( &read_set );

    res = select( fd_hi+1, &read_set, 0, 0, &tv );
    
    if( res == 0 )
      continue;
    
    if( res < 0 )
      break;

    for( int i = 0; i < (int)read_set.fd_count; i++ )
    {
      struct t_doip_client *pclient;
      struct sockaddr_in who = { 0 };
      int fd = read_set.fd_array[i];
      
      if( !FD_ISSET( fd, &read_set ) )
        continue;
      
      if( fd == pserver->server_socket )
      {
        res = sizeof( who );
        int client;

        client = accept( fd, (struct sockaddr*)&who, &res );
        if( client < 0 )
        {
          printf( "accept error\n" );
          continue; 
        }
        /* try to find free client */
        pclient = find_client( pserver, 0 );
        if( !pclient )
        {
          printf( "no free client socket\n" );
          shutdown( client, SD_BOTH );
          closesocket( client );
          continue;
        }
        pclient->socket = client;
        pclient->pos = 0;
        pclient->port = htons( who.sin_port );
        pclient->ip = htonl( who.sin_addr.S_un.S_addr );
        FD_SET( client, &main_set );
        pserver->nactive++;
        printf( "clients %d/%d\n", pserver->nactive, pserver->nclients );
        if( pserver->on_connect )
        {
          pserver->on_connect( pclient );
        }
      }
      else
      {
        uint8_t buffer[MAX_CLIENT_BUFFER_SIZE];

        pclient = find_client( pserver, fd );

        res = network_doip_recv( pclient, (void*)buffer, sizeof( buffer ) );
        if( res <= 0 || !pclient )
        {
          pserver->nactive--;
          printf( "clients %d/%d\n", pserver->nactive, pserver->nclients );
          if( pserver->on_disconnect )
          {
            pserver->on_disconnect( pclient );
          }
          shutdown( fd, SD_BOTH );
          closesocket( fd );
          /* remove from set */
          FD_CLR( fd, &main_set );
          if( pclient )
          {
            /* mark as unused */
            pclient->socket = 0;
          }
          continue;
        }

        if( ( pclient->pos + res ) < MAX_CLIENT_BUFFER_SIZE )
        {
          memcpy( &pclient->data[pclient->pos], buffer, res );
          pclient->pos += res;
        }
        if( pserver->on_receive )
        {
          res = pserver->on_receive( pclient );
          if( ( res >= pclient->pos ) || ( res < 0 ) )
          {
            pclient->pos = 0;
          }
          else
          {
            pclient->pos -= res;
            memmove( pclient->data, &pclient->data[res], pclient->pos );
          }
        }
      }
    }
  }

  return 0;
}

int network_doip_server_destroy( struct t_doip_server *pserver )
{

  return 0;
}

