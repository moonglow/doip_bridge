#pragma once
#include <stdint.h>

struct t_init_can
{
  uint32_t  ecu_req;
  uint32_t  ecu_resp;
  uint32_t  fun_req;
};

int can_deinit_interface( void );
int can_init_interface( struct t_init_can *p, int (*rx_cb)( uint32_t id, uint8_t *p, int len ) );
int can_uds_data( uint32_t id, uint8_t *pdata, int size );

