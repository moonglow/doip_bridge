#pragma once

#define DOIP_HEADER_SIZE  8
#define MAX_UDS_MSG_SIZE (4095)

#define RESERVED_VER    0x00
#define ISO13400_2010   0x01
#define ISO13400_2012   0x02
#define ISO13400_2019   0x03
#define DEFAULT_VALUE   0xFF

#define DOIP_GENERIC_NACK                          0x0000
#define DOIP_VEHICLE_IDENTIFICATION_REQ            0x0001
#define DOIP_VEHICLE_IDENTIFICATION_REQ_EID        0x0002
#define DOIP_VEHICLE_IDENTIFICATION_REQ_VIN        0x0003
#define DOIP_VEHICLE_ANNOUNCEMENT_MESSAGE          0x0004
#define DOIP_ROUTING_ACTIVATION_REQUEST            0x0005
#define DOIP_ROUTING_ACTIVATION_RESPONSE           0x0006
#define DOIP_ALIVE_CHECK_REQUEST                   0x0007
#define DOIP_ALIVE_CHECK_RESPONSE                  0x0008
#define DOIP_ENTITY_STATUS_REQUEST                 0x4001
#define DOIP_ENTITY_STATUS_RESPONSE                0x4002
#define DOIP_POWER_INFORMATION_REQUEST             0x4003
#define DOIP_POWER_INFORMATION_RESPONSE            0x4004
#define DOIP_DIAGNOSTIC_MESSAGE                    0x8001
#define DOIP_DIAGNOSTIC_MESSAGE_ACK                0x8002
#define DOIP_DIAGNOSTIC_MESSAGE_NACK               0x8003

#pragma pack( push, 1 )
struct t_doip_msg
{
  uint8_t version;
  uint8_t iversion;
  uint16_t  type;
  uint32_t  length;
  union
  {
    struct
    {
      uint8_t code;
    }
    generic_nack;
    struct
    {
      uint8_t eid[6];
    }
    veh_id_req_eid;
    struct
    {
      uint8_t vin[17];
    }
    veh_id_req_vid;
    struct
    {
      uint8_t vin[17];
      uint16_t  logical_addr;
      uint8_t eid[6];
      uint8_t gid[6];
      uint8_t action;
      /* version 2 */
      uint8_t status;
    }
    veh_id_resp;
    struct
    {
      uint16_t  source_addr;
      uint16_t  act_type;
      uint32_t  reserved;
      uint32_t  oem;
    }
    route_act_req_v1;
    struct
    {
      uint16_t  source_addr;
      uint8_t act_type;
      uint32_t  reserved;
      uint32_t  oem;
    }
    route_act_req_v2;
    struct
    {
      uint16_t  logic_addr;
      uint16_t  doip_addr;
      uint8_t code;
      uint32_t  reserved;
      uint32_t  oem;
    }
    route_act_resp;
    struct
    {
      uint16_t  source_addr;
    }
    alive_resp;
    struct
    {
      uint8_t mode;
    }
    power_info_resp;
    struct
    {
      uint8_t node_type;
      uint8_t max_open_sockets;
      uint8_t current_opne_sockets;
      uint32_t  max_data_size;
    }
    entity_status_resp;
    struct
    {
      uint16_t  source_addr;
      uint16_t  target_addr;
      uint8_t   uds[];
    }
    uds_data;
    struct
    {
      uint16_t  source_addr;
      uint16_t  target_addr;
      uint8_t   ack_nack;
      uint8_t   uds_prev[];
    }
    uds_ack_nack;
  };
};
#pragma pack( pop )
