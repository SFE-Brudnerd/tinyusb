/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2018, hathach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#ifndef _TUSB_H_
#define _TUSB_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------+
// INCLUDE
//--------------------------------------------------------------------+
#include "common/tusb_common.h"
#include "tusb_hal.h"
#include "osal/osal.h"
#include "common/tusb_fifo.h"

//------------- HOST -------------//
#if TUSB_OPT_HOST_ENABLED
  #include "host/usbh.h"

  #if HOST_CLASS_HID
    #include "class/hid/hid_host.h"
  #endif

  #if CFG_TUH_MSC
    #include "class/msc/msc_host.h"
  #endif

  #if CFG_TUH_CDC
    #include "class/cdc/cdc_host.h"
  #endif

  #if CFG_TUSB_HOST_CUSTOM_CLASS
    #include "class/custom_host.h"
  #endif

#endif

//------------- DEVICE -------------//
#if TUSB_OPT_DEVICE_ENABLED
  #include "device/usbd.h"

  #if CFG_TUD_HID
    #include "class/hid/hid_device.h"
  #endif

  #if CFG_TUD_CDC
    #include "class/cdc/cdc_device.h"
  #endif

  #if CFG_TUD_MSC
    #include "class/msc/msc_device.h"
  #endif

  #if CFG_TUD_MIDI
    #include "class/midi/midi_device.h"
  #endif

  #if CFG_TUD_CUSTOM_CLASS
    #include "class/custom/custom_device.h"
  #endif
#endif


//--------------------------------------------------------------------+
// APPLICATION API
//--------------------------------------------------------------------+
/** \ingroup group_application_api
 *  @{ */

// Initialize device/host stack
bool tusb_init(void);

// Check if stack is initialized
bool tusb_inited(void);

// TODO
// bool tusb_teardown(void);


// backward compatible only. TODO remove later
ATTR_DEPRECATED("Please use either tud_task() or tuh_task()")
static inline void tusb_task(void)
{
  #if TUSB_OPT_HOST_ENABLED
  tuh_task();
  #endif

  #if TUSB_OPT_DEVICE_ENABLED
  tud_task();
  #endif
}

/** @} */

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_H_ */
