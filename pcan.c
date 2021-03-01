#include "pcan.h"
#include <windows.h>
#include <stdio.h>
#include "./pcan_isotp/PCAN-ISO-TP_2016.h"

#define STATUS_OK_KO(test) CANTP_StatusIsOk(test, PCANTP_STATUS_OK, false)

static cantp_handle can_handle = PCANTP_HANDLE_USBBUS1;
static cantp_mapping mapping = { 0 }, reverse_mapping = { 0 }, func_mapping = { 0 };
static int g_thread_run = 0;
static HANDLE m_hThread = NULL;
static HANDLE m_hEvent = NULL;
static int (*rx_data)( uint32_t id, uint8_t *p, int len );

bool __stdcall (*CANTP_StatusIsOk)(
	const cantp_status status,
	const cantp_status status_expected _DEF_ARG_OK,
	bool strict _DEF_ARG);
cantp_status __stdcall (*CANTP_AddMapping)(
	cantp_handle channel,
	cantp_mapping* mapping);
cantp_status __stdcall (*CANTP_SetValue)(
	cantp_handle channel,
	cantp_parameter parameter,
	void* buffer,
	uint32_t buffer_size);
cantp_status __stdcall (*CANTP_MsgDataAlloc)(
	cantp_msg* msg_buffer,
	cantp_msgtype type);
cantp_status __stdcall (*CANTP_MsgDataFree)(
	cantp_msg* msg_buffer);
cantp_status __stdcall (*CANTP_MsgDataInit)(
	cantp_msg* msg_buffer,
	uint32_t can_id,
	cantp_can_msgtype can_msgtype,
	uint32_t data_length,
	const void* data,
	cantp_netaddrinfo* netaddrinfo _DEF_ARG);
cantp_status __stdcall (*CANTP_Read)(
	cantp_handle channel,
	cantp_msg* msg_buffer,
	cantp_timestamp* timestamp_buffer _DEF_ARG,
	cantp_msgtype msg_type _DEF_ARG_MSGTYPE);
cantp_status __stdcall (*CANTP_Write)(
	cantp_handle channel,
	cantp_msg* msg_buffer);
cantp_status __stdcall (*CANTP_GetMsgProgress)(
	cantp_handle channel,
	cantp_msg* msg_buffer,
	cantp_msgdirection direction,
	cantp_msgprogress* msgprogress_buffer);
cantp_status __stdcall (*CANTP_Initialize)(
	cantp_handle channel,
	cantp_baudrate baudrate,
	cantp_hwtype hw_type _DEF_ARG_HW,
	uint32_t io_port _DEF_ARG,
	uint16_t interrupt _DEF_ARG);
cantp_status __stdcall (*CANTP_Uninitialize)(
	cantp_handle channel);

int remove_me_please( uint8_t *p, int size );

DWORD __stdcall can_read_thread( LPVOID p )
{
  cantp_handle handle = (cantp_handle)p;
  cantp_status res;
  g_thread_run = 1;
	cantp_msgprogress progress = { 0 };
  cantp_msg rx_msg = { 0 };

  res = CANTP_SetValue( handle, PCANTP_PARAMETER_RECEIVE_EVENT, &m_hEvent, sizeof(m_hEvent) );
  if( !STATUS_OK_KO( res ) )
  {
    return -1;
  }

  res = CANTP_MsgDataAlloc( &rx_msg, PCANTP_MSGTYPE_ISOTP );
  if( !STATUS_OK_KO( res ) )
    return -1;

  while( g_thread_run == 1 )
  {
    DWORD result = WaitForSingleObject( m_hEvent, 100 );
    if (result != WAIT_OBJECT_0)
      continue;

    res = CANTP_Read( handle, &rx_msg, NULL, PCANTP_MSGTYPE_ISOTP );
    if( !STATUS_OK_KO( res ) )
      continue;
    
    if( !( rx_msg.type & PCANTP_MSGTYPE_ISOTP ) )
      continue;

    if( rx_msg.msgdata.isotp->flags & PCANTP_MSGFLAG_LOOPBACK )
      continue;
    
    /* if RX in progress */
    if( rx_msg.msgdata.isotp->netaddrinfo.msgtype & PCANTP_ISOTP_MSGTYPE_FLAG_INDICATION_RX )
    {
      //printf( "q\n" );
      for(;;Sleep( 0 ) )
      {
        res = CANTP_GetMsgProgress( handle, &rx_msg, PCANTP_MSGDIRECTION_RX, &progress );
        if( !STATUS_OK_KO( res ) )
          continue;
        if( progress.state != PCANTP_MSGPROGRESS_STATE_PROCESSING )
          break;
      }
      if( progress.state != PCANTP_MSGPROGRESS_STATE_COMPLETED )
      {
        printf( "multi rx error\n" );
        continue;
      }
      
      /* reread data */
      continue;
    }

    /* process message */
#if 0
    printf( "rx_msg, size=%d :\n", rx_msg.msgdata.isotp->length );
    for( int i = 0; i < (int)rx_msg.msgdata.isotp->length; i++ )
    {
      printf( "%02X ", rx_msg.msgdata.isotp->data[i] );
    }
    printf( "\n" );
#endif
    if( rx_data )
    {
      rx_data( rx_msg.can_info.can_id, rx_msg.msgdata.isotp->data, rx_msg.msgdata.isotp->length );
    }
  }
  
  DWORD dwNothing = 0;
  CANTP_SetValue( handle, PCANTP_PARAMETER_RECEIVE_EVENT ,&dwNothing, sizeof(dwNothing) );

  (void)CANTP_MsgDataFree( &rx_msg );
  return 0;
}

int can_deinit_interface( void )
{
  g_thread_run = 0;
  if( m_hThread )
  {
    WaitForSingleObject( m_hThread, -1);
    m_hThread = NULL;
  }

  if( m_hEvent )
  {
    CloseHandle( m_hEvent );
  }

  CANTP_Uninitialize( can_handle );
  
  return -1;
}

int can_init_interface( struct t_init_can *p, int (*rx_cb)( uint32_t id, uint8_t *p, int len ) )
{
  cantp_status res;
  uint32_t st_min = 1; /* 1ms */

  rx_data = rx_cb;

  /* load libs function */
  HANDLE hLib = LoadLibrary( "PCAN-ISO-TP.dll" );
  if( !hLib )
  {
    printf( "Library not found, can interface not be initialized\n" );
    return -1;
  }
  
  CANTP_StatusIsOk = (void*)GetProcAddress( hLib, "CANTP_StatusIsOk_2016" );
  CANTP_AddMapping = (void*)GetProcAddress( hLib, "CANTP_AddMapping_2016" );
  CANTP_SetValue = (void*)GetProcAddress( hLib, "CANTP_SetValue_2016" );
  CANTP_MsgDataAlloc = (void*)GetProcAddress( hLib, "CANTP_MsgDataAlloc_2016" );
  CANTP_MsgDataFree = (void*)GetProcAddress( hLib, "CANTP_MsgDataFree_2016" );
  CANTP_MsgDataInit = (void*)GetProcAddress( hLib, "CANTP_MsgDataInit_2016" );
  CANTP_Read = (void*)GetProcAddress( hLib, "CANTP_Read_2016" );
  CANTP_Write = (void*)GetProcAddress( hLib, "CANTP_Write_2016" );
  CANTP_MsgDataInit = (void*)GetProcAddress( hLib, "CANTP_MsgDataInit_2016" );
  CANTP_GetMsgProgress = (void*)GetProcAddress( hLib, "CANTP_GetMsgProgress_2016" );
  CANTP_Initialize = (void*)GetProcAddress( hLib, "CANTP_Initialize_2016" );
  CANTP_Uninitialize = (void*)GetProcAddress( hLib, "CANTP_Uninitialize_2016" );

  res = CANTP_Initialize( can_handle, PCANTP_BAUDRATE_500K, (cantp_hwtype)0, 0, 0 );
  
  if( !STATUS_OK_KO( res ) )
    return -1;

	res = CANTP_SetValue( can_handle, PCANTP_PARAMETER_SEPARATION_TIME, &st_min, sizeof(st_min) );
  if( !STATUS_OK_KO( res ) )
    return can_deinit_interface();

  /* add network mapping */
	mapping.can_id = p->ecu_req;
	mapping.can_id_flow_ctrl = p->ecu_resp;
	mapping.can_msgtype = PCANTP_CAN_MSGTYPE_STANDARD;

	mapping.netaddrinfo.extension_addr = 0x00;
	mapping.netaddrinfo.format = PCANTP_ISOTP_FORMAT_NORMAL;
	mapping.netaddrinfo.msgtype = PCANTP_ISOTP_MSGTYPE_DIAGNOSTIC;
	mapping.netaddrinfo.source_addr = 0xF1;
	mapping.netaddrinfo.target_addr = 0x01;
	mapping.netaddrinfo.target_type = PCANTP_ISOTP_ADDRESSING_PHYSICAL;

	res = CANTP_AddMapping( can_handle, &mapping );
  if( !STATUS_OK_KO( res ) )
    return can_deinit_interface();

	reverse_mapping = mapping;
	reverse_mapping.can_id = mapping.can_id_flow_ctrl;
	reverse_mapping.can_id_flow_ctrl = mapping.can_id;
	reverse_mapping.netaddrinfo.source_addr = mapping.netaddrinfo.target_addr;
	reverse_mapping.netaddrinfo.target_addr = mapping.netaddrinfo.source_addr;

	res = CANTP_AddMapping( can_handle, &reverse_mapping );
  if( !STATUS_OK_KO( res ) )
    return can_deinit_interface();

	func_mapping.can_id = p->fun_req;
	func_mapping.can_id_flow_ctrl = 0;
	func_mapping.can_msgtype = PCANTP_CAN_MSGTYPE_STANDARD;

	func_mapping.netaddrinfo.extension_addr = 0x00;
	func_mapping.netaddrinfo.format = PCANTP_ISOTP_FORMAT_NORMAL;
	func_mapping.netaddrinfo.msgtype = PCANTP_ISOTP_MSGTYPE_DIAGNOSTIC;
	func_mapping.netaddrinfo.source_addr = 0xF1;
	func_mapping.netaddrinfo.target_addr = 0x13;
	func_mapping.netaddrinfo.target_type = PCANTP_ISOTP_ADDRESSING_FUNCTIONAL;

	res = CANTP_AddMapping( can_handle, &func_mapping );
  if( !STATUS_OK_KO( res ) )
    return can_deinit_interface();

  m_hEvent = CreateEvent(NULL, FALSE, FALSE, "");
  if( !m_hEvent )
    return can_deinit_interface();

  m_hThread = CreateThread( 0, 0, can_read_thread, (LPVOID)can_handle, 0, 0 );
  if( !m_hThread )
    return can_deinit_interface();

  return 0;
}

int can_uds_data( uint32_t id, uint8_t *pdata, int size )
{
  cantp_status res;
  static cantp_msg tx_msg = { 0 };

  (void)CANTP_MsgDataFree( &tx_msg );
  res = CANTP_MsgDataAlloc( &tx_msg, PCANTP_MSGTYPE_ISOTP );
  if( !STATUS_OK_KO( res ) )
    return -1;
  
  cantp_netaddrinfo *paddr = ( id == mapping.can_id ) ? &mapping.netaddrinfo:&func_mapping.netaddrinfo;

	res = CANTP_MsgDataInit( &tx_msg, id,
                                PCANTP_CAN_MSGTYPE_STANDARD,
                                size, pdata,
                                paddr
                              );
  if( !STATUS_OK_KO( res ) )
  {
    (void)CANTP_MsgDataFree( &tx_msg );
    return -1;
  }

	res = CANTP_Write( can_handle, &tx_msg );
  (void)res;
  return 0;
}

