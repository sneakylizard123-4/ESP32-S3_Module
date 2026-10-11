#include <string.h>
#include "tusb.h"

//--------------------------------------------------------------------+
// Device descriptor: HackRF One (0x1d50 / 0x6089)
//--------------------------------------------------------------------+
static const uint8_t _descriptor_device[] = {
  0x12, 0x01,             // bLength, DEVICE
  0x00, 0x02,             // bcdUSB 2.00
  0x00, 0x00, 0x00,       // class/subclass/protocol
  0x40,                   // bMaxPacketSize0 = 64
  0x50, 0x1d,             // idVendor  = 0x1d50
  0x89, 0x60,             // idProduct = 0x6089
  0x13, 0x01,             // bcdDevice = 0x0113
  0x01, 0x02, 0x03,       // iManufacturer, iProduct, iSerialNumber
  0x01                    // bNumConfigurations
};

uint8_t const * tud_descriptor_device_cb(void) {
  return _descriptor_device;
}

//--------------------------------------------------------------------+
// Configuration: 1 vendor interface, EP 0x02 OUT + EP 0x81 IN (bulk, 64)
//--------------------------------------------------------------------+
static const uint8_t _descriptor_config[] = {
  // Configuration (total length 32)
  0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0xFA,
  // Interface 0: vendor specific (0xFF/0xFF/0xFF), 2 endpoints
  0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x04,
  // Endpoint OUT 0x02, bulk, 64
  0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
  // Endpoint IN 0x81, bulk, 64
  0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return _descriptor_config;
}

//--------------------------------------------------------------------+
// String descriptors
//--------------------------------------------------------------------+
static const char *string_desc_arr[] = {
  (const char[]) { 0x09, 0x04 },   // 0: LANGID 0x0409
  "Great Scott Gadgets",           // 1: Manufacturer
  "HackRF One",                    // 2: Product
  "0000000000000000",              // 3: Serial
  "Transceiver",                   // 4: Configuration
};

static uint16_t _desc_str[32];

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t chr_count;

  if (index == 0) {
    _desc_str[1] = 0x0409;
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
    const char *str = string_desc_arr[index];
    chr_count = (uint8_t) strlen(str);
    if (chr_count > 31) chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }
  }

  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
