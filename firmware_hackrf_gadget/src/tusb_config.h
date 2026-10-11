#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// MCU / port
//--------------------------------------------------------------------+
#define CFG_TUSB_MCU             OPT_MCU_ESP32S3
#define CFG_TUSB_OS              OPT_OS_FREERTOS

// Native USB peripheral: device-only on port 0
#define CFG_TUSB_RHPORT0_MODE    (OPT_MODE_DEVICE)

#define CFG_TUD_ENDPOINT0_SIZE   64

//--------------------------------------------------------------------+
// Device classes
//--------------------------------------------------------------------+
#define CFG_TUD_CDC              0
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           1

#define CFG_TUD_VENDOR_EPSIZE        64
#define CFG_TUD_VENDOR_RX_BUFSIZE    1024
#define CFG_TUD_VENDOR_TX_BUFSIZE    8192

//--------------------------------------------------------------------+
// Debug
//--------------------------------------------------------------------+
#define CFG_TUSB_DEBUG           0

#ifdef __cplusplus
}
#endif

#endif
