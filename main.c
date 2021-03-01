#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>

#include "network.h"
#include "pcan.h"
#include "doip.h"

struct t_doip_configs
{
  struct t_doip_client *uds_resp;
  uint16_t target;
  /* gw logical address */
  uint16_t  gw_laddr;
  /* ECU mapping */
  uint16_t  ecu_laddr;
  /* 11/29bit address not supported yet */
  uint32_t  ecu_phy_req;
  uint32_t  ecu_phy_resp;
  /* Func address mapping */
  uint16_t  fun_laddr;
  uint32_t  fun_phy;
  /* can baudrate */
  uint32_t  baudrate;
}
gcfg = 
{
  .gw_laddr = 0xE11,

  .ecu_laddr = 0xE01,
  .ecu_phy_req = 0x7D5,
  .ecu_phy_resp = 0x7C5,

  .fun_laddr = 0xEFF,
  .fun_phy = 0x7DF,
};

static int can_recv_uds_data( uint32_t id, uint8_t *p, int size )
{
  uint8_t buffer[DOIP_HEADER_SIZE+4+MAX_UDS_MSG_SIZE];
  struct t_doip_msg *presp = (void*)buffer;

  (void)id;
  assert( size <= MAX_UDS_MSG_SIZE );

  if( !gcfg.uds_resp )
    return 0;

  presp->version = ISO13400_2012;
  presp->iversion = 0xFF ^ presp->version;
 
  presp->type = __swap_u16( DOIP_DIAGNOSTIC_MESSAGE );
  presp->length = __swap_u32( 4 + size );
  presp->uds_data.source_addr = __swap_u16( gcfg.ecu_laddr ); 
  presp->uds_data.target_addr = __swap_u16( gcfg.target ); 
  
  memcpy( presp->uds_data.uds, p, size );
  
  network_doip_send( gcfg.uds_resp, presp, __swap_u32( presp->length ) + DOIP_HEADER_SIZE );
  return 0;
}


int process_doip_request( struct t_doip_client *pc )
{
  uint8_t buffer[DOIP_HEADER_SIZE+5+MAX_UDS_MSG_SIZE];
  int size = pc->pos;
  struct t_doip_msg *presp = (void*)buffer;
  struct t_doip_msg *preq = (void*)pc->data;

  if( size < DOIP_HEADER_SIZE )
    return 0;

  if( ( 0xFF^preq->version ) != (preq->iversion) )
    return -1;

  switch( preq->version )
  {
    default:
      return -1;
    case DEFAULT_VALUE:
      if( !pc->is_udp )
        return -1;
      preq->version = ISO13400_2012;
    /* fall through */
    case ISO13400_2010:
    case ISO13400_2012:
      break;
  }

  if( size < (int)( __swap_u32( preq->length ) + DOIP_HEADER_SIZE ) )
    return 0;

  preq->type = __swap_u16( preq->type );
  preq->length = __swap_u32( preq->length );
  
  /* complete payload here */
  //printf( "Type: 0x%04X, Size: %d\n", preq->type, preq->length );
  
  presp->version = preq->version;
  presp->iversion = 0xFF ^ presp->version;

  switch( preq->type )
  {
    default:
      return -1;
    case DOIP_POWER_INFORMATION_REQUEST:
      presp->type = __swap_u16( DOIP_POWER_INFORMATION_RESPONSE );
      presp->length = __swap_u32( sizeof( presp->power_info_resp ) );
      
      presp->power_info_resp.mode = 0x00;

      network_doip_send( pc, presp, __swap_u32( presp->length ) + DOIP_HEADER_SIZE );

      return preq->length + DOIP_HEADER_SIZE;
    case DOIP_ENTITY_STATUS_REQUEST:
      presp->type = __swap_u16( DOIP_ENTITY_STATUS_RESPONSE );
      presp->length = __swap_u32( sizeof( presp->entity_status_resp ) );
      
      presp->entity_status_resp.current_opne_sockets = 0;
      presp->entity_status_resp.max_open_sockets = 3;
      presp->entity_status_resp.node_type = 0x00; /* gateway */
      presp->entity_status_resp.max_data_size = sizeof( buffer );
    
      network_doip_send( pc, presp, __swap_u32( presp->length ) + DOIP_HEADER_SIZE );

      return preq->length + DOIP_HEADER_SIZE;
    case DOIP_VEHICLE_IDENTIFICATION_REQ:
    case DOIP_VEHICLE_IDENTIFICATION_REQ_EID:
    case DOIP_VEHICLE_IDENTIFICATION_REQ_VIN:
    {
      presp->type = __swap_u16( DOIP_VEHICLE_ANNOUNCEMENT_MESSAGE );
      presp->length = __swap_u32( sizeof( presp->veh_id_resp ) );

      memset( &presp->veh_id_resp, 0x00, sizeof( presp->veh_id_resp ) );
      
      presp->veh_id_resp.logical_addr = __swap_u16( gcfg.ecu_laddr );
      presp->veh_id_resp.action = 0x00;
      presp->veh_id_resp.status = 0x00;
      
      /* random 'VIN' */
      for( int i = 0; i < sizeof( presp->veh_id_resp.vin ); i++ )
      {
        presp->veh_id_resp.vin[i] = (rand()%10)+'0';
      }

      /* random MAC */
      for( int i = 0; i < sizeof( presp->veh_id_resp.eid ); i++ )
      {
        presp->veh_id_resp.eid[i] = rand()&0xFF;
        presp->veh_id_resp.gid[i] = presp->veh_id_resp.eid[i];
      }

      network_doip_send( pc, presp, __swap_u32( presp->length ) + DOIP_HEADER_SIZE );

      return preq->length + DOIP_HEADER_SIZE;
    }
    case DOIP_DIAGNOSTIC_MESSAGE:
    {
      uint16_t source_addr, target_addr;

      source_addr = __swap_u16( preq->uds_data.source_addr );
      target_addr = __swap_u16( preq->uds_data.target_addr );
      int uds_payload_size = preq->length - 4;
      if( uds_payload_size <= 0 )
        return -1;
      
      presp->type = __swap_u16( DOIP_DIAGNOSTIC_MESSAGE_ACK );
      presp->length = __swap_u32( 5 );//__swap_u32( 5 + uds_payload_size );
      presp->uds_ack_nack.source_addr = __swap_u16( target_addr ); 
      presp->uds_ack_nack.target_addr = __swap_u16( source_addr ); 
      presp->uds_ack_nack.ack_nack = 0; /* ok */
      memcpy( presp->uds_ack_nack.uds_prev, preq->uds_data.uds, uds_payload_size );

      if( !pc->is_udp )
      {
        gcfg.uds_resp = pc;
        gcfg.target = source_addr;
      }

      /* send data to CAN */
      if( target_addr == gcfg.ecu_laddr )
        can_uds_data( gcfg.ecu_phy_req, preq->uds_data.uds,  uds_payload_size );
      else if( target_addr == gcfg.fun_laddr )
        can_uds_data( gcfg.fun_phy, preq->uds_data.uds,  uds_payload_size );
      else
        printf( "TODO: invalid target\r\n" );
      
      network_doip_send( pc, presp, __swap_u32( presp->length ) + DOIP_HEADER_SIZE );
      return preq->length + DOIP_HEADER_SIZE;
    }
    case DOIP_ROUTING_ACTIVATION_REQUEST:
    {
      uint16_t source_addr, act_type;
      if( preq->version == ISO13400_2010 )
      {
        source_addr = __swap_u16( preq->route_act_req_v1.source_addr );
        act_type = __swap_u16( preq->route_act_req_v1.act_type );
      }
      else
      {
        source_addr = __swap_u16( preq->route_act_req_v2.source_addr );
        act_type = preq->route_act_req_v2.act_type;
      }
      presp->type = __swap_u16( DOIP_ROUTING_ACTIVATION_RESPONSE );
      presp->length = __swap_u32( sizeof( presp->route_act_resp ) );

      presp->route_act_resp.logic_addr = __swap_u16( 0x111 );
      presp->route_act_resp.doip_addr = __swap_u16( 0x555 );
      presp->route_act_resp.code = 0x10;
      presp->route_act_resp.oem = 0;
      presp->route_act_resp.reserved = 0;
 
      printf( "source %X, type %d\n", source_addr, act_type );

      network_doip_send( pc, presp, sizeof( presp->route_act_resp ) + DOIP_HEADER_SIZE );
      return preq->length + DOIP_HEADER_SIZE;
    }
  }

  return 0;
}

int on_receive( struct t_doip_client *pclient )
{
  int res;

  printf( "+on_receive\n");
  res = process_doip_request( pclient );
  
  return res;
}

int on_connect( struct t_doip_client *pclient )
{
  printf( "+on_connect\n");
  return 0;
}

int on_disconnect( struct t_doip_client *pclient )
{
  printf( "+on_disconnect\n");
  if( !pclient->is_udp );
    gcfg.uds_resp = 0;
  return 0;
}

static struct t_doip_server server = { 0 };

void __cdecl do_exit( int sig )
{
  server.stop_polling = 1;
}

int main(int argc, char *argv[])
{
  int res;
  
  signal( SIGTERM, do_exit );
  signal( SIGINT, do_exit );
  signal( SIGABRT, do_exit );
  
  network_doip_init();
  
  struct t_init_can can = 
  {
    .ecu_req = gcfg.ecu_phy_req,
    .ecu_resp = gcfg.ecu_phy_resp,
    .fun_req = gcfg.fun_phy,
  };

  can_init_interface( &can, can_recv_uds_data );

  server.port = 13400;
  server.nclients = 2;

  server.on_connect = on_connect;
  server.on_disconnect = on_disconnect;
  server.on_receive = on_receive;

  res = network_doip_server_create( &server );
  if( res < 0 )
  {
    printf( "server create failed %d\n", res );
    return -1;
  }

  printf( "wait for clients...\n" );
  network_doip_server_start( &server );

  network_doip_deinit();
  
  can_deinit_interface();
  return 0;
}
