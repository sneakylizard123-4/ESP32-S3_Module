#include <string.h>
#include <math.h>
#include "tusb.h"
#include "class/vendor/vendor_device.h"
#include "hackrf_usb.h"

//--------------------------------------------------------------------+
// HackRF vendor request codes
//--------------------------------------------------------------------+
enum {
  HACKRF_SET_TRANSCEIVER_MODE      = 1,
  HACKRF_MAX283X_WRITE             = 2,
  HACKRF_MAX283X_READ              = 3,
  HACKRF_SI5351C_WRITE             = 4,
  HACKRF_SI5351C_READ              = 5,
  HACKRF_SAMPLE_RATE_SET           = 6,
  HACKRF_BASEBAND_FILTER_BW_SET    = 7,
  HACKRF_RFFC5071_WRITE            = 8,
  HACKRF_RFFC5071_READ             = 9,
  HACKRF_SPIFLASH_ERASE            = 10,
  HACKRF_SPIFLASH_WRITE            = 11,
  HACKRF_SPIFLASH_READ             = 12,
  HACKRF_BOARD_ID_READ             = 14,
  HACKRF_VERSION_STRING_READ       = 15,
  HACKRF_SET_FREQ                  = 16,
  HACKRF_AMP_ENABLE                = 17,
  HACKRF_BOARD_PARTID_SERIALNO_READ= 18,
  HACKRF_SET_LNA_GAIN              = 19,
  HACKRF_SET_VGA_GAIN              = 20,
  HACKRF_SET_TXVGA_GAIN            = 21,
  HACKRF_ANTENNA_ENABLE            = 23,
  HACKRF_SET_FREQ_EXPLICIT         = 24,
  HACKRF_RESET                    = 30,
  HACKRF_CPLD_CHECKSUM_READ        = 36,
  HACKRF_UI_ENABLE                 = 37,
  HACKRF_BOARD_REV_READ            = 45,
  HACKRF_SUPPORTED_PLATFORM_READ   = 46,
  HACKRF_SET_LEDS                  = 47,
  HACKRF_GET_BUFFER_SIZE           = 61,
  HACKRF_READ_TEMPERATURE          = 62,
};

#define BOARD_ID_HACKRF1_OG   2
#define USB_API_VERSION       0x0113

static uint8_t  g_mode = 0;
static uint32_t g_sample_rate = 20000000;
static uint32_t g_freq_mhz = 100;
static uint32_t g_freq_hz  = 0;

static uint8_t  g_ctrl_buf[64];

//--------------------------------------------------------------------+
// Control transfer handling
//--------------------------------------------------------------------+
static bool ctrl_setup(uint8_t rhport, tusb_control_request_t const *req);
static bool ctrl_data (uint8_t rhport, tusb_control_request_t const *req);

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
  if (stage == CONTROL_STAGE_SETUP) return ctrl_setup(rhport, req);
  if (stage == CONTROL_STAGE_DATA)  return ctrl_data(rhport, req);
  return true; // ACK
}

static bool ctrl_setup(uint8_t rhport, tusb_control_request_t const *req) {
  switch (req->bRequest) {
    case HACKRF_BOARD_ID_READ: {
      static uint8_t id = BOARD_ID_HACKRF1_OG;
      return tud_control_xfer(rhport, req, &id, 1);
    }
    case HACKRF_VERSION_STRING_READ: {
      static const char ver[] = "2024.02.1";
      uint16_t n = (uint16_t) strlen(ver);
      if (req->wLength < n) n = req->wLength;
      return tud_control_xfer(rhport, req, (void *) ver, n);
    }
    case HACKRF_BOARD_PARTID_SERIALNO_READ: {
      // read_partid_serialno_t: u32 part_id[2]; u32 serial_no[4];
      static uint32_t sn[6] = { 0x00000000, 0x00000000,
                                0x12345678, 0x9abcdef0, 0x0f1e2d3c, 0x4b5a6978 };
      uint16_t n = sizeof(sn);
      if (req->wLength < n) n = req->wLength;
      return tud_control_xfer(rhport, req, sn, n);
    }
    case HACKRF_SET_LNA_GAIN:
    case HACKRF_SET_VGA_GAIN:
    case HACKRF_SET_TXVGA_GAIN: {
      static uint8_t rv;
      rv = (uint8_t) req->wIndex;
      if (rv == 0) rv = 1;   // host requires a non-zero response
      return tud_control_xfer(rhport, req, &rv, 1);
    }
    case HACKRF_MAX283X_READ: {
      static uint16_t v = 0;
      return tud_control_xfer(rhport, req, &v, 2);
    }
    case HACKRF_SI5351C_READ: {
      static uint8_t v = 0;
      return tud_control_xfer(rhport, req, &v, 1);
    }
    case HACKRF_RFFC5071_READ: {
      static uint16_t v = 0;
      return tud_control_xfer(rhport, req, &v, 2);
    }
    case HACKRF_CPLD_CHECKSUM_READ: {
      static uint32_t v = 0;
      return tud_control_xfer(rhport, req, &v, 4);
    }
    case HACKRF_BOARD_REV_READ: {
      static uint8_t v = 1;
      return tud_control_xfer(rhport, req, &v, 1);
    }
    case HACKRF_SUPPORTED_PLATFORM_READ: {
      static uint8_t v = 0;
      return tud_control_xfer(rhport, req, &v, 1);
    }
    case HACKRF_READ_TEMPERATURE: {
      static uint8_t v = 40;   // ~40 deg C
      return tud_control_xfer(rhport, req, &v, 1);
    }
    case HACKRF_GET_BUFFER_SIZE: {
      static uint32_t v = 32768;
      return tud_control_xfer(rhport, req, &v, 4);
    }

    // --- OUT requests carrying data (captured in the DATA stage) ---------
    case HACKRF_SAMPLE_RATE_SET:
    case HACKRF_SET_FREQ:
    case HACKRF_SET_FREQ_EXPLICIT:
      return tud_control_xfer(rhport, req, g_ctrl_buf, req->wLength);

    //------------------------------------------------------------------
    // No-data OUT requests
    //--------------------------------------------------------------------
    case HACKRF_SET_TRANSCEIVER_MODE:
      g_mode = (uint8_t) req->wValue;
      return tud_control_status(rhport, req);
    case HACKRF_AMP_ENABLE:
    case HACKRF_ANTENNA_ENABLE:
    case HACKRF_SET_LEDS:
    case HACKRF_UI_ENABLE:
    case HACKRF_MAX283X_WRITE:
    case HACKRF_SI5351C_WRITE:
    case HACKRF_RFFC5071_WRITE:
    case HACKRF_BASEBAND_FILTER_BW_SET:
    case HACKRF_SPIFLASH_ERASE:
    case HACKRF_SPIFLASH_WRITE:
    case HACKRF_RESET:
      return tud_control_status(rhport, req);

    default:
      return false; // stall unknown
  }
}

static bool ctrl_data(uint8_t rhport, tusb_control_request_t const *req) {
  (void) rhport;
  if (req->bmRequestType_bit.direction != TUSB_DIR_OUT) return true;

  switch (req->bRequest) {
    case HACKRF_SAMPLE_RATE_SET: {
      uint32_t freq, div;
      memcpy(&freq, g_ctrl_buf, 4);
      memcpy(&div, g_ctrl_buf + 4, 4);
      if (div) g_sample_rate = freq / div;
      break;
    }
    case HACKRF_SET_FREQ: {
      memcpy(&g_freq_mhz, g_ctrl_buf, 4);
      memcpy(&g_freq_hz,  g_ctrl_buf + 4, 4);
      break;
    }
    case HACKRF_SET_FREQ_EXPLICIT:
      break;
    default:
      break;
  }
  return true;
}

//--------------------------------------------------------------------+
// Synthetic I/Q generation
//--------------------------------------------------------------------+
#define IQ_CHUNK 512
static uint8_t iq_buf[IQ_CHUNK];
static float   phase = 0.0f;

static void fill_iq(uint32_t n) {
  uint32_t sr = g_sample_rate ? g_sample_rate : 20000000;
  float step = 2.0f * (float) M_PI * 100000.0f / (float) sr; // 100 kHz tone
  for (uint32_t i = 0; i + 1 < n; i += 2) {
    iq_buf[i]     = (uint8_t) (int8_t) (100.0f * cosf(phase));
    iq_buf[i + 1] = (uint8_t) (int8_t) (100.0f * sinf(phase));
    phase += step;
    if (phase > 2.0f * (float) M_PI) phase -= 2.0f * (float) M_PI;
  }
}

void hackrf_task(void) {
  if (!tud_vendor_mounted()) return;

  if (g_mode == 1) { // RX: stream synthetic samples to host on EP 0x81
    uint32_t avail = tud_vendor_write_available();
    if (avail >= 128) {
      uint32_t n = avail > IQ_CHUNK ? IQ_CHUNK : avail;
      n &= ~3u; // whole I/Q pairs
      if (n) {
        fill_iq(n);
        tud_vendor_write(iq_buf, n);
        tud_vendor_write_flush();
      }
    }
  } else if (g_mode == 2) { // TX: drain host samples on EP 0x02
    static uint8_t tmp[256];
    while (tud_vendor_available() >= 64) {
      tud_vendor_read(tmp, sizeof(tmp));
    }
  }
}
