/*
 * usbd_host.c
 *
 *  Created on: Jan 19, 2013
 *      Author: hathach
 */

/*
 * Software License Agreement (BSD License)
 * Copyright (c) 2013, hathach (tinyusb.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the tiny usb stack.
 */

#include "common/common.h"

#if MODE_HOST_SUPPORTED

#define _TINY_USB_SOURCE_FILE_

//--------------------------------------------------------------------+
// INCLUDE
//--------------------------------------------------------------------+
#include "tusb.h"
#include "usbh_hcd.h"

//TODO temporarily
#if TUSB_CFG_OS == TUSB_OS_NONE && !defined(_TEST_)
void tusb_tick_tock(void)
{
  osal_tick_tock();
}
#endif

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+
#define ENUM_QUEUE_DEPTH  5

// TODO fix number of class driver
class_driver_t const usbh_class_drivers[TUSB_CLASS_MAX_CONSEC_NUMBER] =
{
    [TUSB_CLASS_HID] = {
        .init = hidh_init,
        .open_subtask = hidh_open_subtask,
        .isr = hidh_isr,
        .close = hidh_close
    },

    [TUSB_CLASS_MSC] = {
        .init = msch_init,
        .open_subtask = msch_open_subtask,
        .isr = msch_isr,
        .close = msch_close
    }
};

//--------------------------------------------------------------------+
// INTERNAL OBJECT & FUNCTION DECLARATION
//--------------------------------------------------------------------+
usbh_device_info_t usbh_device_info_pool[TUSB_CFG_HOST_DEVICE_MAX+1] TUSB_CFG_ATTR_USBRAM; // including zero-address

//------------- Enumeration Task Data -------------//
OSAL_TASK_DEF(enum_task, usbh_enumeration_task, 128, OSAL_PRIO_HIGH);
OSAL_QUEUE_DEF(enum_queue, ENUM_QUEUE_DEPTH, uin32_t);
osal_queue_handle_t enum_queue_hdl;
STATIC_ uint8_t enum_data_buffer[TUSB_CFG_HOST_ENUM_BUFFER_SIZE] TUSB_CFG_ATTR_USBRAM;

//------------- Reporter Task Data -------------//

//------------- Helper Function Prototypes -------------//
static inline uint8_t get_new_address(void) ATTR_ALWAYS_INLINE;
static inline uint8_t get_configure_number_for_device(tusb_descriptor_device_t* dev_desc) ATTR_ALWAYS_INLINE;

//--------------------------------------------------------------------+
// PUBLIC API (Parameter Verification is required)
//--------------------------------------------------------------------+
tusbh_device_status_t tusbh_device_status_get (tusb_handle_device_t const device_hdl)
{
  ASSERT(device_hdl <= TUSB_CFG_HOST_DEVICE_MAX, 0);
  return usbh_device_info_pool[device_hdl].status;
}

//--------------------------------------------------------------------+
// CLASS-USBD API (don't require to verify parameters)
//--------------------------------------------------------------------+
tusb_error_t usbh_init(void)
{
  memclr_(usbh_device_info_pool, sizeof(usbh_device_info_t)*(TUSB_CFG_HOST_DEVICE_MAX+1));

  ASSERT_STATUS( hcd_init() );

  //------------- Semaphore for Control Pipe -------------//
  for(uint8_t i=0; i<TUSB_CFG_HOST_DEVICE_MAX+1; i++) // including address zero
  {
    usbh_device_info_pool[i].sem_hdl = osal_semaphore_create( OSAL_SEM_REF(usbh_device_info_pool[i].semaphore) );
    ASSERT_PTR(usbh_device_info_pool[i].sem_hdl, TUSB_ERROR_OSAL_SEMAPHORE_FAILED);
  }

  //------------- Enumeration & Reporter Task init -------------//
  ASSERT_STATUS( osal_task_create(&enum_task) );
  enum_queue_hdl = osal_queue_create(&enum_queue);
  ASSERT_PTR(enum_queue_hdl, TUSB_ERROR_OSAL_QUEUE_FAILED);

  //------------- class init -------------//
  for (uint8_t class_code = 1; class_code < TUSB_CLASS_MAX_CONSEC_NUMBER; class_code++)
  {
    if (usbh_class_drivers[class_code].init)
      usbh_class_drivers[class_code].init();
  }

  return TUSB_ERROR_NONE;
}

// function called within a task, requesting os blocking services, subtask input parameter must be static/global variables
tusb_error_t usbh_control_xfer_subtask(uint8_t dev_addr, tusb_std_request_t const* p_request, uint8_t* data)
{
  tusb_error_t error;

  OSAL_SUBTASK_BEGIN

  usbh_device_info_pool[dev_addr].control_request = *p_request;
  (void) hcd_pipe_control_xfer(dev_addr, &usbh_device_info_pool[dev_addr].control_request, data);

  osal_semaphore_wait(usbh_device_info_pool[dev_addr].sem_hdl, OSAL_TIMEOUT_NORMAL, &error); // careful of local variable without static
  SUBTASK_ASSERT_STATUS_WITH_HANDLER(error, tusbh_device_mount_failed_cb(TUSB_ERROR_USBH_MOUNT_DEVICE_NOT_RESPOND, NULL) );

  OSAL_SUBTASK_END
}

tusb_error_t usbh_pipe_control_open(uint8_t dev_addr, uint8_t max_packet_size) ATTR_ALWAYS_INLINE;
tusb_error_t usbh_pipe_control_open(uint8_t dev_addr, uint8_t max_packet_size)
{
  osal_semaphore_reset( usbh_device_info_pool[dev_addr].sem_hdl );

  ASSERT_STATUS( hcd_pipe_control_open(dev_addr, max_packet_size) );

  return TUSB_ERROR_NONE;
}

static inline tusb_error_t usbh_pipe_control_close(uint8_t dev_addr) ATTR_ALWAYS_INLINE;
static inline tusb_error_t usbh_pipe_control_close(uint8_t dev_addr)
{
  ASSERT_STATUS( hcd_pipe_control_close(dev_addr) );

  return TUSB_ERROR_NONE;
}

pipe_status_t usbh_pipe_status_get(pipe_handle_t pipe_hdl)
{
  return PIPE_STATUS_BUSY;
}

//--------------------------------------------------------------------+
// USBH-HCD ISR/Callback API
//--------------------------------------------------------------------+
// interrupt caused by a TD (with IOC=1) in pipe of class class_code
void usbh_isr(pipe_handle_t pipe_hdl, uint8_t class_code)
{
  if (class_code == 0) // Control transfer
  {
    osal_semaphore_post( usbh_device_info_pool[ pipe_hdl.dev_addr ].sem_hdl );
  }else if (usbh_class_drivers[class_code].isr)
  {
    usbh_class_drivers[class_code].isr(pipe_hdl);
  }else
  {
    ASSERT(false, (void) 0); // something wrong, no one claims the isr's source
  }
}

void usbh_device_plugged_isr(uint8_t hostid, tusb_speed_t speed)
{
  osal_queue_send(enum_queue_hdl,
   *( (uint32_t*) ( &(usbh_enumerate_t){ .core_id = hostid, .speed = speed} ) )
   );
}

void usbh_device_unplugged_isr(uint8_t hostid)
{
  //------------- find the device address that is unplugged -------------//
  uint8_t dev_addr=1;
  while ( dev_addr <= TUSB_CFG_HOST_DEVICE_MAX && ! (usbh_device_info_pool[dev_addr].core_id == hostid &&
      usbh_device_info_pool[dev_addr].hub_addr == 0 &&
      usbh_device_info_pool[dev_addr].hub_port ==0))
  {
    dev_addr++;
  }

  ASSERT(dev_addr <= TUSB_CFG_HOST_DEVICE_MAX, (void) 0 );

  // if device unplugged is not a hub TODO handle hub unplugged
  for (uint8_t class_code = 1; class_code < TUSB_CLASS_MAX_CONSEC_NUMBER; class_code++)
  {
    if (usbh_class_drivers[class_code].close)
      usbh_class_drivers[class_code].close(dev_addr);
  }

  usbh_pipe_control_close(dev_addr);

  // set to REMOVING to prevent allocate to other new device
  // need to set to UNPLUGGED by HCD after freeing all resources
  usbh_device_info_pool[dev_addr].status = TUSB_DEVICE_STATUS_REMOVING;
}


//--------------------------------------------------------------------+
// ENUMERATION TASK
//--------------------------------------------------------------------+
//TODO reduce Cyclomatic Complexity
OSAL_TASK_DECLARE(usbh_enumeration_task)
{
  tusb_error_t error;
  usbh_enumerate_t enum_entry;

  // for OSAL_NONE local variable won't retain value after blocking service sem_wait/queue_recv
  static uint8_t new_addr;
  static uint8_t configure_selected = 1;
  static uint8_t *p_desc = NULL;

  OSAL_TASK_LOOP_BEGIN

  osal_queue_receive(enum_queue_hdl, (uint32_t*)(&enum_entry), OSAL_TIMEOUT_WAIT_FOREVER, &error);

  TASK_ASSERT( hcd_port_connect_status(enum_entry.core_id) ); // device may be unplugged
  usbh_device_info_pool[0].core_id  = enum_entry.core_id; // TODO refractor integrate to device_pool
  usbh_device_info_pool[0].hub_addr = enum_entry.hub_addr;
  usbh_device_info_pool[0].hub_port = enum_entry.hub_port;
  usbh_device_info_pool[0].speed    = enum_entry.speed;

  TASK_ASSERT_STATUS( usbh_pipe_control_open(0, 8) );

  //------------- Get first 8 bytes of device descriptor to get Control Endpoint Size -------------//
  OSAL_SUBTASK_INVOKED_AND_WAIT(
    usbh_control_xfer_subtask(
      0,
      &(tusb_std_request_t)
      {
        .bmRequestType = { .direction = TUSB_DIR_DEV_TO_HOST, .type = TUSB_REQUEST_TYPE_STANDARD, .recipient = TUSB_REQUEST_RECIPIENT_DEVICE },
        .bRequest = TUSB_REQUEST_GET_DESCRIPTOR,
        .wValue   = (TUSB_DESC_DEVICE << 8),
        .wLength  = 8
      },
      enum_data_buffer
    )
  );

  //------------- Set new address -------------//
  new_addr = get_new_address();
  TASK_ASSERT(new_addr <= TUSB_CFG_HOST_DEVICE_MAX);

  OSAL_SUBTASK_INVOKED_AND_WAIT(
    usbh_control_xfer_subtask(
      0,
      &(tusb_std_request_t)
      {
        .bmRequestType = { .direction = TUSB_DIR_HOST_TO_DEV, .type = TUSB_REQUEST_TYPE_STANDARD, .recipient = TUSB_REQUEST_RECIPIENT_DEVICE },
        .bRequest = TUSB_REQUEST_SET_ADDRESS,
        .wValue   = new_addr
      },
      NULL
    )
  );

  //------------- update port info & close control pipe of addr0 -------------//
  usbh_device_info_pool[new_addr].core_id  = usbh_device_info_pool[0].core_id;
  usbh_device_info_pool[new_addr].hub_addr = usbh_device_info_pool[0].hub_addr;
  usbh_device_info_pool[new_addr].hub_port = usbh_device_info_pool[0].hub_port;
  usbh_device_info_pool[new_addr].speed    = usbh_device_info_pool[0].speed;
  usbh_device_info_pool[new_addr].status   = TUSB_DEVICE_STATUS_ADDRESSED;
  usbh_pipe_control_close(0);

//  hcd_port_reset( usbh_device_info_pool[new_addr].core_id ); TODO verified

  // open control pipe for new address
  TASK_ASSERT_STATUS ( usbh_pipe_control_open(new_addr, ((tusb_descriptor_device_t*) enum_data_buffer)->bMaxPacketSize0 ) );

  //------------- Get full device descriptor -------------//
  OSAL_SUBTASK_INVOKED_AND_WAIT(
    usbh_control_xfer_subtask(
      new_addr,
      &(tusb_std_request_t)
      {
        .bmRequestType = { .direction = TUSB_DIR_DEV_TO_HOST, .type = TUSB_REQUEST_TYPE_STANDARD, .recipient = TUSB_REQUEST_RECIPIENT_DEVICE },
        .bRequest = TUSB_REQUEST_GET_DESCRIPTOR,
        .wValue   = (TUSB_DESC_DEVICE << 8),
        .wLength  = 18
      },
      enum_data_buffer
    )
  );

  // update device info
  usbh_device_info_pool[new_addr].vendor_id       = ((tusb_descriptor_device_t*) enum_data_buffer)->idVendor;
  usbh_device_info_pool[new_addr].product_id      = ((tusb_descriptor_device_t*) enum_data_buffer)->idProduct;
  usbh_device_info_pool[new_addr].configure_count = ((tusb_descriptor_device_t*) enum_data_buffer)->bNumConfigurations;

  configure_selected = get_configure_number_for_device((tusb_descriptor_device_t*) enum_data_buffer);
  TASK_ASSERT(configure_selected <= usbh_device_info_pool[new_addr].configure_count);

  //------------- Get 9 bytes of configuration descriptor -------------//
  OSAL_SUBTASK_INVOKED_AND_WAIT(
    usbh_control_xfer_subtask(
      new_addr,
      &(tusb_std_request_t)
      {
        .bmRequestType = { .direction = TUSB_DIR_DEV_TO_HOST, .type = TUSB_REQUEST_TYPE_STANDARD, .recipient = TUSB_REQUEST_RECIPIENT_DEVICE },
        .bRequest = TUSB_REQUEST_GET_DESCRIPTOR,
        .wValue   = (TUSB_DESC_CONFIGURATION << 8) | (configure_selected - 1),
        .wLength  = 9
      },
      enum_data_buffer
    )
  );
  TASK_ASSERT_WITH_HANDLER( TUSB_CFG_HOST_ENUM_BUFFER_SIZE > ((tusb_descriptor_configuration_t*)enum_data_buffer)->wTotalLength,
                            tusbh_device_mount_failed_cb(TUSB_ERROR_USBH_MOUNT_CONFIG_DESC_TOO_LONG, NULL) );

  //------------- Get full configuration descriptor -------------//
  OSAL_SUBTASK_INVOKED_AND_WAIT(
    usbh_control_xfer_subtask(
      new_addr,
      &(tusb_std_request_t)
      {
        .bmRequestType = { .direction = TUSB_DIR_DEV_TO_HOST, .type = TUSB_REQUEST_TYPE_STANDARD, .recipient = TUSB_REQUEST_RECIPIENT_DEVICE },
        .bRequest = TUSB_REQUEST_GET_DESCRIPTOR,
        .wValue   = (TUSB_DESC_CONFIGURATION << 8) | (configure_selected - 1),
        .wLength  = ((tusb_descriptor_configuration_t*) enum_data_buffer)->wTotalLength
      },
      enum_data_buffer
    )
  );

  // update configuration info
  usbh_device_info_pool[new_addr].interface_count = ((tusb_descriptor_configuration_t*) enum_data_buffer)->bNumInterfaces;

  //------------- parse configuration & install drivers -------------//
  p_desc = enum_data_buffer + sizeof(tusb_descriptor_configuration_t);

  // parse each interfaces
  while( p_desc < enum_data_buffer + ((tusb_descriptor_configuration_t*)enum_data_buffer)->wTotalLength )
  {
    TASK_ASSERT( TUSB_DESC_INTERFACE == ((tusb_descriptor_interface_t*) p_desc)->bDescriptorType ); // TODO should we skip this descriptor and advance

    uint8_t class_code = ((tusb_descriptor_interface_t*) p_desc)->bInterfaceClass;
    if (class_code == 0)
    {
      TASK_ASSERT( false ); // corrupted data, abort enumeration
    } else if ( class_code < TUSB_CLASS_MAX_CONSEC_NUMBER)
    {
      if ( usbh_class_drivers[class_code].open_subtask )
      {
        uint16_t length;
        OSAL_SUBTASK_INVOKED_AND_WAIT ( // parameters in task/sub_task must be static storage (static or global)
            usbh_class_drivers[ ((tusb_descriptor_interface_t*) p_desc)->bInterfaceClass ].open_subtask(new_addr, p_desc, &length) );
        p_desc += length;
      }
    } else // unsupported class (not enable or yet implemented)
    {
      do
      {
        p_desc += (*p_desc);
      } while ( (p_desc < enum_data_buffer + ((tusb_descriptor_configuration_t*)enum_data_buffer)->wTotalLength)
          && TUSB_DESC_INTERFACE != p_desc[1] );
    }
  }

  //------------- Set Configure -------------//
  OSAL_SUBTASK_INVOKED_AND_WAIT (
    usbh_control_xfer_subtask(
        new_addr,
        &(tusb_std_request_t)
        {
          .bmRequestType = { .direction = TUSB_DIR_HOST_TO_DEV, .type = TUSB_REQUEST_TYPE_STANDARD, .recipient = TUSB_REQUEST_RECIPIENT_DEVICE },
          .bRequest = TUSB_REQUEST_SET_CONFIGURATION,
          .wValue   = configure_selected
        },
        NULL
    )
  );

  usbh_device_info_pool[new_addr].status = TUSB_DEVICE_STATUS_READY;
  tusbh_device_mount_succeed_cb(new_addr);

  // TODO invoke mounted callback
  OSAL_TASK_LOOP_END
}

//--------------------------------------------------------------------+
// REPORTER TASK & ITS DATA
//--------------------------------------------------------------------+



#endif

//--------------------------------------------------------------------+
// INTERNAL HELPER
//--------------------------------------------------------------------+
static inline uint8_t get_new_address(void)
{
  uint8_t addr;
  for (addr=1; addr <= TUSB_CFG_HOST_DEVICE_MAX; addr++)
  {
    if (usbh_device_info_pool[addr].status == TUSB_DEVICE_STATUS_UNPLUG)
      break;
  }
  return addr;
}

static inline uint8_t get_configure_number_for_device(tusb_descriptor_device_t* dev_desc)
{
  uint8_t config_num = 1;

  // invoke callback to ask user which configuration to select
  if (tusbh_device_attached_cb)
  {
    config_num = min8_of(1, tusbh_device_attached_cb(dev_desc) );
  }

  return config_num;
}