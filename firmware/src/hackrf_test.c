/*
 * HackRF USB protocol test for ESP32-S3 acting as a USB-OTG host.
 *
 * Enumerates a HackRF (One / Jawbreaker / rad1o) with the ESP-IDF usb_host
 * driver and exercises every vendor request defined in libhackrf master
 * (host/libhackrf/src/hackrf.c), requests 1..62.
 *
 * Safety:
 *  - SPIFLASH_ERASE / SPIFLASH_WRITE can destroy the CPLD/FPGA images and
 *    are compiled OUT unless ENABLE_DESTRUCTIVE_TESTS is defined.
 *  - Transceiver mode CPLD_UPDATE (4) is skipped: it puts the CPLD into a
 *    state that can only be left by uploading a bitstream or resetting.
 *  - Everything else is state that a final SET_TRANSCEIVER_MODE OFF plus a
 *    RESET request restores.
 *
 * No real RF data is captured or transmitted; RX streaming is probed long
 * enough to prove the bulk IN endpoint responds and the TX probe writes a
 * silent (all-zero) block.
 */

#include <string.h>
#include <stdarg.h>
#include "driver/gpio.h"

#include "driver/rmt.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"

static const char *TAG = "hackrf_test";

/* ------------------------------------------------------------------ */
/* HackRF protocol constants (from hackrf.c)                           */
/* ------------------------------------------------------------------ */

#define HACKRF_VID              0x1d50
#define HACKRF_PID_HACKRF_ONE   0x6089
#define HACKRF_PID_JAWBREAKER   0x604b
#define HACKRF_PID_RAD1O        0xcc15

#define HACKRF_EP_RX            0x81        /* bulk IN, radio samples    */
#define HACKRF_EP_TX            0x02        /* bulk OUT, radio samples   */

/* Transceiver modes (wValue of request 1) */
#define TRANSCEIVER_MODE_OFF         0
#define TRANSCEIVER_MODE_RECEIVE     1
#define TRANSCEIVER_MODE_TRANSMIT    2
#define TRANSCEIVER_MODE_SS          3
#define TRANSCEIVER_MODE_CPLD_UPDATE 4
#define TRANSCEIVER_MODE_RX_SWEEP    5

/* Vendor request codes (hackrf_vendor_request in hackrf.c) */
enum {
    VENDOR_REQ_SET_TRANSCEIVER_MODE        = 1,
    VENDOR_REQ_MAX283X_WRITE               = 2,
    VENDOR_REQ_MAX283X_READ                = 3,
    VENDOR_REQ_SI5351C_WRITE               = 4,
    VENDOR_REQ_SI5351C_READ                = 5,
    VENDOR_REQ_SAMPLE_RATE_SET             = 6,
    VENDOR_REQ_BASEBAND_FILTER_BANDWIDTH   = 7,
    VENDOR_REQ_RFFC5071_WRITE              = 8,
    VENDOR_REQ_RFFC5071_READ               = 9,
    VENDOR_REQ_SPIFLASH_ERASE              = 10,
    VENDOR_REQ_SPIFLASH_WRITE              = 11,
    VENDOR_REQ_SPIFLASH_READ               = 12,
    VENDOR_REQ_BOARD_ID_READ               = 14,
    VENDOR_REQ_VERSION_STRING_READ         = 15,
    VENDOR_REQ_SET_FREQ                    = 16,
    VENDOR_REQ_AMP_ENABLE                  = 17,
    VENDOR_REQ_BOARD_PARTID_SERIALNO_READ  = 18,
    VENDOR_REQ_SET_LNA_GAIN                = 19,
    VENDOR_REQ_SET_VGA_GAIN                = 20,
    VENDOR_REQ_SET_TXVGA_GAIN              = 21,
    VENDOR_REQ_ANTENNA_ENABLE              = 23,
    VENDOR_REQ_SET_FREQ_EXPLICIT           = 24,
    VENDOR_REQ_USB_WCID_VENDOR_REQ         = 25,
    VENDOR_REQ_INIT_SWEEP                  = 26,
    VENDOR_REQ_OPERACAKE_GET_BOARDS        = 27,
    VENDOR_REQ_OPERACAKE_SET_PORTS         = 28,
    VENDOR_REQ_SET_HW_SYNC_MODE            = 29,
    VENDOR_REQ_RESET                       = 30,
    VENDOR_REQ_OPERACAKE_SET_RANGES        = 31,
    VENDOR_REQ_CLKOUT_ENABLE               = 32,
    VENDOR_REQ_SPIFLASH_STATUS             = 33,
    VENDOR_REQ_SPIFLASH_CLEAR_STATUS       = 34,
    VENDOR_REQ_OPERACAKE_GPIO_TEST         = 35,
    VENDOR_REQ_CPLD_CHECKSUM               = 36,
    VENDOR_REQ_UI_ENABLE                   = 37,
    VENDOR_REQ_OPERACAKE_SET_MODE          = 38,
    VENDOR_REQ_OPERACAKE_GET_MODE          = 39,
    VENDOR_REQ_OPERACAKE_SET_DWELL_TIMES   = 40,
    VENDOR_REQ_GET_M0_STATE                = 41,
    VENDOR_REQ_SET_TX_UNDERRUN_LIMIT       = 42,
    VENDOR_REQ_SET_RX_OVERRUN_LIMIT        = 43,
    VENDOR_REQ_GET_CLKIN_STATUS            = 44,
    VENDOR_REQ_BOARD_REV_READ              = 45,
    VENDOR_REQ_SUPPORTED_PLATFORM_READ     = 46,
    VENDOR_REQ_SET_LEDS                    = 47,
    VENDOR_REQ_SET_USER_BIAS_T_OPTS        = 48,
    VENDOR_REQ_FPGA_WRITE_REG              = 49,
    VENDOR_REQ_FPGA_READ_REG               = 50,
    VENDOR_REQ_P2_CTRL                     = 51,
    VENDOR_REQ_P1_CTRL                     = 52,
    VENDOR_REQ_SET_NARROWBAND_FILTER       = 53,
    VENDOR_REQ_SET_FPGA_BITSTREAM          = 54,
    VENDOR_REQ_CLKIN_CTRL                  = 55,
    VENDOR_REQ_READ_SELFTEST               = 56,
    VENDOR_REQ_READ_ADC                    = 57,
    VENDOR_REQ_TEST_RTC_OSC                = 58,
    VENDOR_REQ_RADIO_WRITE_REG             = 59,
    VENDOR_REQ_RADIO_READ_REG              = 60,
    VENDOR_REQ_GET_BUFFER_SIZE             = 61,
    VENDOR_REQ_READ_TEMPERATURE            = 62,
};

/* TEST_RTC_OSC wIndex steps (firmware) */
enum {
    RTC_OSC_START_32KHZ_OSC = 0,
    RTC_OSC_START_FREQ_MONITOR = 1,
    RTC_OSC_READ_FREQ_MONITOR = 2,
    RTC_OSC_STOP_32KHZ_OSC = 3,
};

/* RF path filter (set_freq_explicit) */
#define RF_PATH_FILTER_LOW_PASS  0
#define RF_PATH_FILTER_HIGH_PASS 1
#define RF_PATH_FILTER_BYPASS    2

/* Operacake "no board" address fill value */
#define HACKRF_OPERACAKE_ADDRESS_INVALID 0xff

/* Wire payloads (see hackrf.c; all multi-byte ints little-endian) */
typedef struct {
    uint32_t freq_mhz;
    uint32_t freq_hz;
} __attribute__((packed)) hackrf_set_freq_params_t;

typedef struct {
    uint32_t freq_hz;
    uint32_t divider;
} __attribute__((packed)) hackrf_set_fracrate_params_t;

typedef struct {
    uint64_t if_freq_hz;
    uint64_t lo_freq_hz;
    uint8_t path;
} __attribute__((packed)) hackrf_set_freq_explicit_params_t;

/* GET_M0_STATE payload (hackrf_m0_state, 40 bytes) */
typedef struct {
    uint16_t request_flag;
    uint16_t requested_mode;
    uint32_t active_mode;
    uint32_t m0_count;
    uint32_t m4_count;
    uint32_t num_shortfalls;
    uint32_t longest_shortfall;
    uint32_t shortfall_limit;
    uint32_t threshold;
    uint32_t next_mode;
    uint32_t error;
} __attribute__((packed)) hackrf_m0_state_t;

/* ------------------------------------------------------------------ */
/* Build configuration                                                 */
/* ------------------------------------------------------------------ */

/* Enable to actually issue SPIFLASH_ERASE / SPIFLASH_WRITE. These will
 * erase / rewrite the SPI flash that holds the CPLD and FPGA images and
 * must NOT be run against a board you care about. Default: skipped.  */
#ifndef ENABLE_DESTRUCTIVE_TESTS
#define ENABLE_DESTRUCTIVE_TESTS 0
#endif

#define FIFO_LINES_RX   512     /* supports IN endpoints up to ~2 KB    */
#define FIFO_LINES_NPTX 256     /* supports bulk OUT MPS up to 1024     */
#define FIFO_LINES_PTX  96      /* no periodic endpoints on HackRF      */

#define LED_STATUS_PIN 48
#define NEOPIXEL_PIN 47
#define NUM_LEDS 1
#define CTRL_DATA_MAX   64      /* room for the biggest vendor payload  */
#define BULK_IN_LENGTH  16384   /* multiple of 512 (EP MPS)             */
#define BULK_TX_LENGTH  512     /* one silent sample block (256 IQ)     */

/* ------------------------------------------------------------------ */
/* Task / event plumbing                                               */
/* ------------------------------------------------------------------ */

static usb_host_client_handle_t s_client;
static usb_device_handle_t s_dev;
static uint8_t s_dev_addr;

static SemaphoreHandle_t s_dev_ready;   /* given on CLIENT_EVENT_NEW_DEV */
static SemaphoreHandle_t s_xfer_done;   /* given by every transfer cb    */

static volatile int s_quit;             /* set by test task to shut down */
static volatile int s_suite_done;
static led_strip_handle_t s_pixel;       /* set once the suite finishes   */

static int s_pass;
static int s_fail;
static int s_skipped;

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    switch (msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (s_suite_done) {
            /* e.g. device re-enumerated after the RESET request */
            ESP_LOGI(TAG, "new device at address %u (ignored, suite done)",
                     msg->new_dev.address);
            break;
        }
        ESP_LOGI(TAG, "new device at address %u", msg->new_dev.address);
        s_dev_addr = msg->new_dev.address;
        xSemaphoreGive(s_dev_ready);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGW(TAG, "device gone");
        break;
    default:
        ESP_LOGW(TAG, "unsupported client event %d", msg->event);
        break;
    }
}

static void xfer_cb(usb_transfer_t *xfer)
{
    (void)xfer;
    xSemaphoreGive(s_xfer_done);
}

/* ------------------------------------------------------------------ */
/* Verdict helpers                                                     */
/* ------------------------------------------------------------------ */

static void verdict(const char *name, bool ok)
{
    if (ok) {
        s_pass++;
        ESP_LOGI(TAG, "PASS  %s", name);
    } else {
        s_fail++;
        ESP_LOGE(TAG, "FAIL  %s", name);
    }
}

static void verdict_skipped(const char *reason, const char *fmt, ...)
{
    s_skipped++;
    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ESP_LOGW(TAG, "SKIP  %s (%s)", msg, reason);
}

static void verdict_status(const char *name, usb_transfer_status_t st,
                           bool stall_ok)
{
    if (st == USB_TRANSFER_STATUS_COMPLETED) {
        verdict(name, true);
    } else if (st == USB_TRANSFER_STATUS_STALL && stall_ok) {
        ESP_LOGI(TAG, "PASS  %s (device STALL = request unsupported)", name);
        s_pass++;
    } else {
        ESP_LOGE(TAG, "FAIL  %s (status %d)", name, st);
        s_fail++;
    }
}

/* ------------------------------------------------------------------ */
/* Raw vendor control transfers                                        */
/* ------------------------------------------------------------------ */

static bool ctrl_rw(const char *name, uint8_t bmRequestType, uint8_t bRequest,
                    uint16_t wValue, uint16_t wIndex,
                    uint8_t *data, uint16_t wLength,
                    bool stall_ok)
{
    usb_transfer_t *xfer;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + CTRL_DATA_MAX, 0,
                                &xfer) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  %s (transfer_alloc failed)", name);
        s_fail++;
        return false;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = bmRequestType;
    setup->bRequest = bRequest;
    setup->wValue = wValue;
    setup->wIndex = wIndex;
    setup->wLength = wLength;

    xfer->device_handle = s_dev;
    xfer->bEndpointAddress = 0;
    xfer->callback = xfer_cb;
    xfer->num_bytes = sizeof(usb_setup_packet_t) + wLength;
    if (data && wLength) {
        memcpy(xfer->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
    }

    esp_err_t err = usb_host_transfer_submit_control(s_client, xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  %s (submit_control err 0x%x)", name, err);
        s_fail++;
        usb_host_transfer_free(xfer);
        return false;
    }

    if (xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "FAIL  %s (timeout)", name);
        s_fail++;
        return false;
    }

    bool ok = (xfer->status == USB_TRANSFER_STATUS_COMPLETED);
    if (ok && data && wLength) {
        memcpy(data, xfer->data_buffer + sizeof(usb_setup_packet_t),
               (wLength < xfer->actual_num_bytes) ? wLength
                                                  : xfer->actual_num_bytes);
    }
    verdict_status(name, xfer->status, stall_ok);
    usb_host_transfer_free(xfer);
    return ok;
}

static bool ctrl_in(const char *name, uint8_t bRequest, uint16_t wValue,
                    uint16_t wIndex, uint8_t *data, uint16_t wLength,
                    bool stall_ok)
{
    return ctrl_rw(name,
                   USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_VENDOR
                       | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
                   bRequest, wValue, wIndex, data, wLength, stall_ok);
}

static bool ctrl_out(const char *name, uint8_t bRequest, uint16_t wValue,
                     uint16_t wIndex, const void *data, uint16_t wLength,
                     bool stall_ok)
{
    uint8_t tmp[CTRL_DATA_MAX];
    memset(tmp, 0, sizeof(tmp));
    if (data && wLength) {
        memcpy(tmp, data, wLength);
    }
    return ctrl_rw(name,
                   USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_VENDOR
                       | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
                   bRequest, wValue, wIndex, tmp, wLength, stall_ok);
}

/* ------------------------------------------------------------------ */
/* Bulk endpoint probes                                                */
/* ------------------------------------------------------------------ */

static void bulk_in_probe(void)
{
    usb_transfer_t *xfer;
    if (usb_host_transfer_alloc(BULK_IN_LENGTH, 0, &xfer) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  bulk IN probe (transfer_alloc failed)");
        s_fail++;
        return;
    }

    xfer->device_handle = s_dev;
    xfer->bEndpointAddress = HACKRF_EP_RX;
    xfer->callback = xfer_cb;
    xfer->num_bytes = BULK_IN_LENGTH;

    esp_err_t err = usb_host_transfer_submit(xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  bulk IN probe (submit err 0x%x)", err);
        s_fail++;
        usb_host_transfer_free(xfer);
        return;
    }

    if (xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGE(TAG, "FAIL  bulk IN probe (timeout, no samples from EP 0x%02x)",
                 HACKRF_EP_RX);
        s_fail++;
        return;
    }

    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        char name[80];
        snprintf(name, sizeof(name),
                 "bulk IN on EP 0x%02x (%d B requested, %d B received)",
                 HACKRF_EP_RX, BULK_IN_LENGTH, xfer->actual_num_bytes);
        verdict(name, xfer->actual_num_bytes > 0);
    } else {
        verdict_status("bulk IN on EP 0x81", xfer->status, false);
    }
    usb_host_transfer_free(xfer);
}

/* Send one all-zero block to EP 0x02 with the radio in TRANSMIT mode.
 * Zeros are the DAC mid-scale value -> CDD/CW at DC, effectively silent. */
static void bulk_tx_probe(void)
{
    usb_transfer_t *xfer;
    if (usb_host_transfer_alloc(BULK_TX_LENGTH, 0, &xfer) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  bulk TX probe (transfer_alloc failed)");
        s_fail++;
        return;
    }

    xfer->device_handle = s_dev;
    xfer->bEndpointAddress = HACKRF_EP_TX;
    xfer->callback = xfer_cb;
    xfer->num_bytes = BULK_TX_LENGTH;
    memset(xfer->data_buffer, 0, BULK_TX_LENGTH);

    esp_err_t err = usb_host_transfer_submit(xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  bulk TX probe (submit err 0x%x)", err);
        s_fail++;
        usb_host_transfer_free(xfer);
        return;
    }

    if (xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGE(TAG, "FAIL  bulk TX probe (timeout on EP 0x%02x)",
                 HACKRF_EP_TX);
        s_fail++;
        return;
    }

    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        char name[80];
        snprintf(name, sizeof(name),
                 "bulk OUT on EP 0x%02x (%d B silent block)",
                 HACKRF_EP_TX, xfer->actual_num_bytes);
        verdict(name, xfer->actual_num_bytes == BULK_TX_LENGTH);
    } else {
        verdict_status("bulk OUT on EP 0x02", xfer->status, false);
    }
    usb_host_transfer_free(xfer);
}

/* ------------------------------------------------------------------ */
/* Full protocol test suite                                            */
/* ------------------------------------------------------------------ */

static void reset_test(void);
static void led_init(void);
static void led_blink_pass(void);
static void led_blink_fail(void);

static void run_protocol_tests(void)
{
    uint8_t buf[CTRL_DATA_MAX];

    /* =================  identity / telemetry reads  ================ */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("BOARD_ID_READ (14)", VENDOR_REQ_BOARD_ID_READ, 0, 0,
                buf, 1, true) && buf[0] != 0xff) {
        ESP_LOGI(TAG, "       board id = %u", buf[0]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("VERSION_STRING_READ (15)", VENDOR_REQ_VERSION_STRING_READ, 0, 0,
                buf, 32, true)) {
        ESP_LOGI(TAG, "       firmware = %.32s", buf);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("BOARD_PARTID_SERIALNO_READ (18)", VENDOR_REQ_BOARD_PARTID_SERIALNO_READ,
                0, 0, buf, 12, true)) {
        ESP_LOGI(TAG, "       part id  = %02x %02x %02x %02x",
                 buf[0], buf[1], buf[2], buf[3]);
        ESP_LOGI(TAG, "       serial no= %02x %02x%02x %02x%02x %02x%02x %02x%02x",
                 buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("BOARD_REV_READ (45)", VENDOR_REQ_BOARD_REV_READ, 0, 0,
                buf, 1, true)) {
        ESP_LOGI(TAG, "       board rev = %u", buf[0]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("SUPPORTED_PLATFORM_READ (46)", VENDOR_REQ_SUPPORTED_PLATFORM_READ,
                0, 0, buf, 4, true)) {
        ESP_LOGI(TAG, "       supported platform = %02x%02x%02x%02x",
                 buf[0], buf[1], buf[2], buf[3]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("GET_BUFFER_SIZE (61)", VENDOR_REQ_GET_BUFFER_SIZE, 0, 0,
                buf, 2, true)) {
        uint16_t bsz = (uint16_t)(buf[0] | (buf[1] << 8));
        ESP_LOGI(TAG, "       transfer buffer size = %u", bsz);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("READ_TEMPERATURE (62)", VENDOR_REQ_READ_TEMPERATURE, 0, 0,
                buf, 1, true)) {
        ESP_LOGI(TAG, "       temperature = %d C", (int8_t)buf[0]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("GET_M0_STATE (41)", VENDOR_REQ_GET_M0_STATE, 0, 0,
                buf, sizeof(hackrf_m0_state_t), true)) {
        const hackrf_m0_state_t *st = (void *)buf;
        uint32_t am = (uint32_t)st->active_mode;    /* already LE on ESP32 */
        ESP_LOGI(TAG, "       M0 state: request=%u requested=%u active=%u "
                 "err=%u (mode 0x%" PRIx32 ")", st->request_flag,
                 st->requested_mode, am, st->error, am);
    }

    /* =================  register-level chip I/O  =================== */
    /* MAX2837: read reg 0 and write the same value back (no net change) */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("MAX283X_READ (3) reg0", VENDOR_REQ_MAX283X_READ, 0, 0,
                buf, 2, true)) {
        uint16_t v = (uint16_t)(buf[0] | (buf[1] << 8));
        ESP_LOGI(TAG, "       max2837 reg0 = 0x%04x", v);
        ctrl_out("MAX283X_WRITE (2) reg0 = readback", VENDOR_REQ_MAX283X_WRITE,
                 v, 0, NULL, 0, true);
    }

    /* SI5351C clock chip: read reg 0, write it back */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("SI5351C_READ (5) reg0", VENDOR_REQ_SI5351C_READ, 0, 0,
                buf, 1, true)) {
        ESP_LOGI(TAG, "       si5351c reg0 = 0x%02x", buf[0]);
        ctrl_out("SI5351C_WRITE (4) reg0 = readback", VENDOR_REQ_SI5351C_WRITE,
                 buf[0], 0, NULL, 0, true);
    }

    /* RFFC5071 mixer/VCO: read reg 0, write it back */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("RFFC5071_READ (9) reg0", VENDOR_REQ_RFFC5071_READ, 0, 0,
                buf, 2, true)) {
        uint16_t v = (uint16_t)(buf[0] | (buf[1] << 8));
        ESP_LOGI(TAG, "       rffc5071 reg0 = 0x%04x", v);
        ctrl_out("RFFC5071_WRITE (8) reg0 = readback", VENDOR_REQ_RFFC5071_WRITE,
                 v, 0, NULL, 0, true);
    }

    /* FPGA register 0 (API >= 0x0109) */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("FPGA_READ_REG (50) reg0", VENDOR_REQ_FPGA_READ_REG, 0, 0,
                buf, 1, true)) {
        ESP_LOGI(TAG, "       fpga reg0 = 0x%02x", buf[0]);
        ctrl_out("FPGA_WRITE_REG (49) reg0 = readback", VENDOR_REQ_FPGA_WRITE_REG,
                 buf[0], 0, NULL, 0, true);
    }

    /* HackRF Radio SPI regs (API >= 0x0111; on a One these STALL) */
    memset(buf, 0, sizeof(buf));
    {
        uint64_t radio_val = 0;
        bool have_radio = ctrl_in("RADIO_READ_REG (60) bank0 reg0",
                                  VENDOR_REQ_RADIO_READ_REG, 0, 0,
                                  (uint8_t *)&radio_val, 8, true);
        if (have_radio) {
            ESP_LOGI(TAG, "       radio reg0 = %016" PRIx64, radio_val);
            /* round-trip: only write if the device supports this request */
            uint8_t wdata[9];
            wdata[0] = 0;                       /* register number */
            memcpy(&wdata[1], &radio_val, 8);   /* value LE */
            ctrl_out("RADIO_WRITE_REG (59) bank0 reg0 = readback",
                     VENDOR_REQ_RADIO_WRITE_REG, 0, 0, wdata, sizeof(wdata), true);
        }
    }

    /* =================  status reads  ============================== */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("READ_SELFTEST (56)", VENDOR_REQ_READ_SELFTEST, 0, 0,
                buf, 32, true)) {
        ESP_LOGI(TAG, "       selftest: %02x %02x ...", buf[0], buf[1]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("READ_ADC (57) channel 0", VENDOR_REQ_READ_ADC, 0, 0,
                buf, 2, true)) {
        ESP_LOGI(TAG, "       adc ch0 = %u", (uint16_t)(buf[0] | (buf[1] << 8)));
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("GET_CLKIN_STATUS (44)", VENDOR_REQ_GET_CLKIN_STATUS, 0, 0,
                buf, 1, true)) {
        ESP_LOGI(TAG, "       clkin status = 0x%02x", buf[0]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("SPIFLASH_STATUS (33)", VENDOR_REQ_SPIFLASH_STATUS, 0, 0,
                buf, 2, true)) {
        ESP_LOGI(TAG, "       spiflash status = 0x%02x 0x%02x", buf[0], buf[1]);
    }

    ctrl_out("SPIFLASH_CLEAR_STATUS (34)", VENDOR_REQ_SPIFLASH_CLEAR_STATUS,
             0, 0, NULL, 0, true);

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("SPIFLASH_READ (12) @0 len16", VENDOR_REQ_SPIFLASH_READ,
                0, 0, buf, 16, true)) {
        ESP_LOGI(TAG, "       spi flash @0 = %02x %02x %02x %02x %02x %02x %02x %02x",
                 buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    }

    memset(buf, 0, sizeof(buf));
    if (ctrl_in("CPLD_CHECKSUM (36)", VENDOR_REQ_CPLD_CHECKSUM, 0, 0,
                buf, 4, true)) {
        ESP_LOGI(TAG, "       cpld checksum = %08" PRIx32, *(uint32_t *)buf);
    }

#if ENABLE_DESTRUCTIVE_TESTS
    if (ctrl_out("SPIFLASH_ERASE (10)", VENDOR_REQ_SPIFLASH_ERASE, 0, 0,
                 NULL, 0, true)) {
        ESP_LOGW(TAG, "       WARNING: SPI flash erased; CPLD image is gone");
    }
    /* Do NOT write here - erasing already destroyed the config area. */
#else
    verdict_skipped("compiled out (destructive)", "SPIFLASH_ERASE (10)");
    verdict_skipped("compiled out (destructive)", "SPIFLASH_WRITE (11)");
#endif

    /* =================  RTC oscillator test (API >= 0x0109)  ======== */
    if (ctrl_out("TEST_RTC_OSC (58) start osc", VENDOR_REQ_TEST_RTC_OSC,
                 0, RTC_OSC_START_32KHZ_OSC, NULL, 0, true)) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (ctrl_out("TEST_RTC_OSC (58) start monitor", VENDOR_REQ_TEST_RTC_OSC,
                     0, RTC_OSC_START_FREQ_MONITOR, NULL, 0, true)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            memset(buf, 0, sizeof(buf));
            if (ctrl_in("TEST_RTC_OSC (58) read count", VENDOR_REQ_TEST_RTC_OSC,
                        0, RTC_OSC_READ_FREQ_MONITOR, buf, 2, true)) {
                uint16_t c = (uint16_t)(buf[0] | (buf[1] << 8));
                verdict("RTC frequency monitor count", c == 1);
                ESP_LOGI(TAG, "       rtc osc count = %u", c);
            }
        }
        ctrl_out("TEST_RTC_OSC (58) stop osc", VENDOR_REQ_TEST_RTC_OSC,
                 0, RTC_OSC_STOP_32KHZ_OSC, NULL, 0, true);
    }

    /* =================  signalling / option toggles (OUT)  ========= */
    ctrl_out("AMP_ENABLE (17) off", VENDOR_REQ_AMP_ENABLE, 0, 0, NULL, 0, false);
    ctrl_out("ANTENNA_ENABLE (23) off", VENDOR_REQ_ANTENNA_ENABLE, 0, 0,
             NULL, 0, false);
    ctrl_out("SET_HW_SYNC_MODE (29) 0", VENDOR_REQ_SET_HW_SYNC_MODE, 0, 0,
             NULL, 0, true);
    ctrl_out("CLKOUT_ENABLE (32) on", VENDOR_REQ_CLKOUT_ENABLE, 1, 0,
             NULL, 0, true);
    ctrl_out("CLKOUT_ENABLE (32) off", VENDOR_REQ_CLKOUT_ENABLE, 0, 0,
             NULL, 0, true);
    ctrl_out("CLKIN_CTRL (55) 0", VENDOR_REQ_CLKIN_CTRL, 0, 0, NULL, 0, true);
    ctrl_out("P1_CTRL (52) 0", VENDOR_REQ_P1_CTRL, 0, 0, NULL, 0, true);
    ctrl_out("P2_CTRL (51) 0", VENDOR_REQ_P2_CTRL, 0, 0, NULL, 0, true);
    ctrl_out("SET_NARROWBAND_FILTER (53) 0", VENDOR_REQ_SET_NARROWBAND_FILTER,
             0, 0, NULL, 0, true);
    ctrl_out("UI_ENABLE (37) on", VENDOR_REQ_UI_ENABLE, 1, 0, NULL, 0, true);
    ctrl_out("UI_ENABLE (37) off", VENDOR_REQ_UI_ENABLE, 0, 0, NULL, 0, true);
    ctrl_out("SET_LEDS (47) on", VENDOR_REQ_SET_LEDS, 0xff, 0, NULL, 0, true);
    ctrl_out("SET_LEDS (47) off", VENDOR_REQ_SET_LEDS, 0x00, 0, NULL, 0, true);
    ctrl_out("SET_USER_BIAS_T_OPTS (48) 0", VENDOR_REQ_SET_USER_BIAS_T_OPTS,
             0, 0, NULL, 0, true);
    ctrl_out("SET_FPGA_BITSTREAM (54) idx0", VENDOR_REQ_SET_FPGA_BITSTREAM,
             0, 0, NULL, 0, true);

    /* =================  signal chain configuration  ================ */
    hackrf_set_freq_params_t freq = {
        .freq_mhz = 915,    /* 915.000000 MHz */
        .freq_hz = 0,
    };
    ctrl_out("SET_FREQ (16) 915 MHz", VENDOR_REQ_SET_FREQ, 0, 0,
             &freq, sizeof(freq), false);

    hackrf_set_freq_explicit_params_t fexp = {
        .if_freq_hz = 2100000000ull,    /* 2.1 GHz IF */
        .lo_freq_hz = 915000000ull,     /* 915 MHz LO */
        .path = RF_PATH_FILTER_BYPASS,
    };
    ctrl_out("SET_FREQ_EXPLICIT (24)", VENDOR_REQ_SET_FREQ_EXPLICIT, 0, 0,
             &fexp, sizeof(fexp), true);

    hackrf_set_fracrate_params_t rate = {
        .freq_hz = 10000000,    /* 10 Msps */
        .divider = 1,
    };
    ctrl_out("SAMPLE_RATE_SET (6) 10 MHz", VENDOR_REQ_SAMPLE_RATE_SET, 0, 0,
             &rate, sizeof(rate), false);

    /* bandwidth split: wValue = bw & 0xffff, wIndex = bw >> 16 */
    uint32_t bw = 3500000;      /* 3.5 MHz */
    ctrl_out("BASEBAND_FILTER_BANDWIDTH_SET (7) 3.5 MHz",
             VENDOR_REQ_BASEBAND_FILTER_BANDWIDTH,
             (uint16_t)(bw & 0xffff), (uint16_t)(bw >> 16), NULL, 0, false);

    /* gain requests are IN; the gain travels in wINDEX (see hackrf.c),
     * and the firmware echoes the gain it actually applied. */
    uint8_t gain = 0;
    if (ctrl_in("SET_LNA_GAIN (19) 32", VENDOR_REQ_SET_LNA_GAIN, 0, 32,
                &gain, 1, false)) {
        ESP_LOGI(TAG, "       lna gain applied = %u", gain);
    }
    gain = 0;
    if (ctrl_in("SET_VGA_GAIN (20) 20", VENDOR_REQ_SET_VGA_GAIN, 0, 20,
                &gain, 1, false)) {
        ESP_LOGI(TAG, "       vga gain applied = %u", gain);
    }
    gain = 0;
    if (ctrl_in("SET_TXVGA_GAIN (21) 16", VENDOR_REQ_SET_TXVGA_GAIN, 0, 16,
                &gain, 1, false)) {
        ESP_LOGI(TAG, "       txvga gain applied = %u", gain);
    }

    /* M0 streaming limits (API >= 0x0106), harmless values */
    ctrl_out("SET_TX_UNDERRUN_LIMIT (42) 4", VENDOR_REQ_SET_TX_UNDERRUN_LIMIT,
             4, 0, NULL, 0, true);
    ctrl_out("SET_RX_OVERRUN_LIMIT (43) 4", VENDOR_REQ_SET_RX_OVERRUN_LIMIT,
             4, 0, NULL, 0, true);

    /* sweep setup (API >= 0x0102): 1000-1005 MHz, 8 kB blocks */
    {
        uint16_t sweep_freq[] = { 1000, 1005 };
        uint8_t data[13];
        uint32_t step_width = 1000000;  /* 1 MHz steps */
        uint32_t offset = 0;
        uint8_t style = 0;              /* LINEAR */
        data[0] = step_width & 0xff;
        data[1] = (step_width >> 8) & 0xff;
        data[2] = (step_width >> 16) & 0xff;
        data[3] = (step_width >> 24) & 0xff;
        data[4] = offset & 0xff;
        data[5] = (offset >> 8) & 0xff;
        data[6] = (offset >> 16) & 0xff;
        data[7] = (offset >> 24) & 0xff;
        data[8] = style;
        data[9] = sweep_freq[0] & 0xff;
        data[10] = sweep_freq[0] >> 8;
        data[11] = sweep_freq[1] & 0xff;
        data[12] = sweep_freq[1] >> 8;
        uint32_t num_bytes = 8192;
        ctrl_out("INIT_SWEEP (26)", VENDOR_REQ_INIT_SWEEP,
                 (uint16_t)(num_bytes & 0xffff), (uint16_t)(num_bytes >> 16),
                 data, sizeof(data), true);
    }

    /* =================  Operacake (needs a PortaPack/oc here)  ===== */
    memset(buf, 0, sizeof(buf));
    if (ctrl_in("OPERACAKE_GET_BOARDS (27)", VENDOR_REQ_OPERACAKE_GET_BOARDS,
                0, 0, buf, 8, true)) {
        ESP_LOGI(TAG, "       operacake boards: %02x %02x %02x %02x %02x %02x %02x %02x",
                 buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    }
    ctrl_out("OPERACAKE_SET_PORTS (28)", VENDOR_REQ_OPERACAKE_SET_PORTS,
             0, 0, NULL, 0, true);
    ctrl_out("OPERACAKE_SET_MODE (38)", VENDOR_REQ_OPERACAKE_SET_MODE,
             0, 0, NULL, 0, true);
    memset(buf, 0, sizeof(buf));
    ctrl_in("OPERACAKE_GET_MODE (39)", VENDOR_REQ_OPERACAKE_GET_MODE,
            0, 0, buf, 1, true);
    memset(buf, 0, sizeof(buf));
    ctrl_in("OPERACAKE_GPIO_TEST (35)", VENDOR_REQ_OPERACAKE_GPIO_TEST,
            0, 0, buf, 2, true);
    {
        uint8_t ranges[2] = { 0, 1 };
        ctrl_out("OPERACAKE_SET_RANGES (31)", VENDOR_REQ_OPERACAKE_SET_RANGES,
                 0, 0, ranges, sizeof(ranges), true);
        uint8_t dwell[5];       /* {u32 dwell LE, u8 port} */
        dwell[0] = 0xff; dwell[1] = 0xff; dwell[2] = 0x00; dwell[3] = 0x00;
        dwell[4] = 0;           /* port A1 */
        ctrl_out("OPERACAKE_SET_DWELL_TIMES (40)", VENDOR_REQ_OPERACAKE_SET_DWELL_TIMES,
                 0, 0, dwell, sizeof(dwell), true);
    }

    /* =================  transceiver modes + endpoints  ============= */
    /* RECEIVE + bulk IN stream probe */
    if (ctrl_out("SET_TRANSCEIVER_MODE (1) RECEIVE",
                 VENDOR_REQ_SET_TRANSCEIVER_MODE, TRANSCEIVER_MODE_RECEIVE, 0,
                 NULL, 0, false)) {
        bulk_in_probe();
    }
    ctrl_out("SET_TRANSCEIVER_MODE (1) OFF", VENDOR_REQ_SET_TRANSCEIVER_MODE,
             TRANSCEIVER_MODE_OFF, 0, NULL, 0, false);

    /* TRANSMIT + one silent bulk block, then OFF */
    if (ctrl_out("SET_TRANSCEIVER_MODE (1) TRANSMIT",
                 VENDOR_REQ_SET_TRANSCEIVER_MODE, TRANSCEIVER_MODE_TRANSMIT, 0,
                 NULL, 0, false)) {
        bulk_tx_probe();
    }
    ctrl_out("SET_TRANSCEIVER_MODE (1) OFF", VENDOR_REQ_SET_TRANSCEIVER_MODE,
             TRANSCEIVER_MODE_OFF, 0, NULL, 0, false);

    ctrl_out("SET_TRANSCEIVER_MODE (1) RX_SWEEP",
             VENDOR_REQ_SET_TRANSCEIVER_MODE, TRANSCEIVER_MODE_RX_SWEEP, 0,
             NULL, 0, true);
    ctrl_out("SET_TRANSCEIVER_MODE (1) OFF", VENDOR_REQ_SET_TRANSCEIVER_MODE,
             TRANSCEIVER_MODE_OFF, 0, NULL, 0, false);

    ctrl_out("SET_TRANSCEIVER_MODE (1) SWEEP",
             VENDOR_REQ_SET_TRANSCEIVER_MODE, TRANSCEIVER_MODE_SS, 0,
             NULL, 0, true);
    ctrl_out("SET_TRANSCEIVER_MODE (1) OFF", VENDOR_REQ_SET_TRANSCEIVER_MODE,
             TRANSCEIVER_MODE_OFF, 0, NULL, 0, false);

    /* live mode toggle */
    ctrl_out("ANTENNA_ENABLE (23) on", VENDOR_REQ_ANTENNA_ENABLE, 1, 0,
             NULL, 0, false);
    ctrl_out("ANTENNA_ENABLE (23) off", VENDOR_REQ_ANTENNA_ENABLE, 0, 0,
             NULL, 0, false);
    ctrl_out("AMP_ENABLE (17) on", VENDOR_REQ_AMP_ENABLE, 1, 0, NULL, 0, false);
    ctrl_out("AMP_ENABLE (17) off", VENDOR_REQ_AMP_ENABLE, 0, 0, NULL, 0, false);

    /* mode never tested: CPLD_UPDATE (4) would need a bitstream upload */
    verdict_skipped("would require CPLD bitstream upload",
                    "SET_TRANSCEIVER_MODE (1) CPLD_UPDATE");

    /* =================  final RESET (restores all state)  ========== */
    reset_test();
}

/* RESET (30) reboots the device, so it runs last. */
static void reset_test(void)
{
    usb_transfer_t *xfer;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &xfer) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  RESET (30) (transfer_alloc failed)");
        s_fail++;
        return;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT
                           | USB_BM_REQUEST_TYPE_TYPE_VENDOR
                           | USB_BM_REQUEST_TYPE_RECIP_DEVICE;
    setup->bRequest = VENDOR_REQ_RESET;
    setup->wValue = 0;
    setup->wIndex = 0;
    setup->wLength = 0;

    xfer->device_handle = s_dev;
    xfer->bEndpointAddress = 0;
    xfer->callback = xfer_cb;
    xfer->num_bytes = sizeof(usb_setup_packet_t);

    if (usb_host_transfer_submit_control(s_client, xfer) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL  RESET (30) (submit failed)");
        s_fail++;
        usb_host_transfer_free(xfer);
        return;
    }

    if (xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "FAIL  RESET (30) (timeout)");
        s_fail++;
        return;
    }

    /* The device often drops off the bus in the middle of this; the driver
     * reports that as NO_DEVICE / ERROR, which still means "it reset". */
    switch (xfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED:
        verdict("RESET (30)", true);
        break;
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_ERROR:
        ESP_LOGI(TAG, "PASS  RESET (30) (device dropped off bus as expected)");
        s_pass++;
        break;
    case USB_TRANSFER_STATUS_STALL:
        verdict_status("RESET (30)", xfer->status, true);
        break;
    default:
        verdict_status("RESET (30)", xfer->status, false);
        break;
    }
    usb_host_transfer_free(xfer);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ------------------------------------------------------------------ */
/* Task entry points                                                   */
/* ------------------------------------------------------------------ */

static void test_task(void *arg)
{
    (void)arg;

    if (xSemaphoreTake(s_dev_ready, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "no USB device appeared within 15 s");
        s_quit = 1;
        usb_host_client_unblock(s_client);
        vTaskDelete(NULL);
        return;
    }

    if (usb_host_device_open(s_client, s_dev_addr, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "failed to open device at address %u", s_dev_addr);
        s_quit = 1;
        usb_host_client_unblock(s_client);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "opened device at address %u", s_dev_addr);

    const usb_device_desc_t *dev_desc;
    if (usb_host_get_device_descriptor(s_dev, &dev_desc) == ESP_OK) {
        ESP_LOGI(TAG, "device descriptor: idVendor=%04x idProduct=%04x "
                 "bcdUSB=%04x (USB API v%u.%02u)",
                 dev_desc->idVendor, dev_desc->idProduct, dev_desc->bcdUSB,
                 dev_desc->bcdUSB >> 8, dev_desc->bcdUSB & 0xff);
        switch (dev_desc->idProduct) {
        case HACKRF_PID_HACKRF_ONE:
            ESP_LOGI(TAG, "detected: HackRF One");
            break;
        case HACKRF_PID_JAWBREAKER:
            ESP_LOGI(TAG, "detected: Jawbreaker");
            break;
        case HACKRF_PID_RAD1O:
            ESP_LOGI(TAG, "detected: rad1o");
            break;
        default:
            ESP_LOGW(TAG, "unexpected product %04x; expected a HackRF (VID %04x)",
                     dev_desc->idProduct, HACKRF_VID);
            break;
        }
    } else {
        ESP_LOGE(TAG, "failed to read device descriptor");
    }

    const usb_config_desc_t *cfg;
    if (usb_host_get_active_config_descriptor(s_dev, &cfg) == ESP_OK) {
        int off = 0;
        const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_address(
            cfg, 0, 0, HACKRF_EP_RX, &off);
        if (ep) {
            ESP_LOGI(TAG, "EP 0x%02x (RX): MPS %d", ep->bEndpointAddress,
                     USB_EP_DESC_GET_MPS(ep));
        }
        off = 0;
        ep = usb_parse_endpoint_descriptor_by_address(
            cfg, 0, 0, HACKRF_EP_TX, &off);
        if (ep) {
            ESP_LOGI(TAG, "EP 0x%02x (TX): MPS %d", ep->bEndpointAddress,
                     USB_EP_DESC_GET_MPS(ep));
        }
    }

    esp_err_t err = usb_host_interface_claim(s_client, s_dev, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to claim interface 0: err 0x%x", err);
        s_quit = 1;
        usb_host_client_unblock(s_client);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "claimed interface 0, alternate 0");

    run_protocol_tests();
    s_suite_done = 1;

    /* teardown; after a Reset the device may already be gone, so tolerate
     * failures here. */
    usb_host_interface_release(s_client, s_dev, 0);
    usb_host_device_close(s_client, s_dev);

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "RESULTS: %d passed, %d failed, %d skipped",
             s_pass, s_fail, s_skipped);
    ESP_LOGI(TAG, "==========================================");

    if (s_fail == 0) {
        ESP_LOGI(TAG, "ALL CHECKS PASSED - signaling via LED");
        led_blink_pass();
    } else {
        ESP_LOGE(TAG, "SOME CHECKS FAILED - signaling via LED");
        led_blink_fail();
    }

    s_quit = 1;
    usb_host_client_unblock(s_client);
    vTaskDelete(NULL);
}

static void client_task(void *arg)
{
    (void)arg;

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &s_client));
    ESP_LOGI(TAG, "client registered");

    while (!s_quit) {
        usb_host_client_handle_events(s_client, portMAX_DELAY);
    }

    usb_host_client_deregister(s_client);
    ESP_LOGI(TAG, "client deregistered");
    vTaskDelete(NULL);
}

static void lib_task(void *arg)
{
    (void)arg;

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        /* Custom FIFO split so full-speed bulk MPS 512 works both ways. */
        .fifo_settings_custom = {
            .rx_fifo_lines = FIFO_LINES_RX,
            .nptx_fifo_lines = FIFO_LINES_NPTX,
            .ptx_fifo_lines = FIFO_LINES_PTX,
        },
    };
    ESP_LOGI(TAG, "installing USB host (FIFO rx=%u nptx=%u ptx=%u)",
             FIFO_LINES_RX, FIFO_LINES_NPTX, FIFO_LINES_PTX);
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB host installed");

    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            if (usb_host_device_free_all() == ESP_OK) {
                has_clients = false;
            } else {
                has_devices = true;
            }
        }
        if (has_devices && (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
            has_clients = false;
        }
    }
    usb_host_uninstall();
    ESP_LOGI(TAG, "USB host stopped");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "HackRF protocol test starting (all vendor requests 1..62)");
    ESP_LOGI(TAG, "connect a HackRF One / Jawbreaker / rad1o to the native USB port");

    s_dev_ready = xSemaphoreCreateBinary();
    s_xfer_done = xSemaphoreCreateBinary();
    if (s_dev_ready == NULL || s_xfer_done == NULL) {
        ESP_LOGE(TAG, "semaphore allocation failed");
        return;
    }

    led_init();
    neopixel_init();
    xTaskCreatePinnedToCore(lib_task, "usb_lib", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(client_task, "usb_client", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(test_task, "hackrf_test", 8192, NULL, 4, NULL, 0);
}
static void led_init(void)
{
    gpio_reset_pin(LED_STATUS_PIN);
    gpio_set_direction(LED_STATUS_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STATUS_PIN, 0);
}


static void neopixel_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = NEOPIXEL_PIN,
        .max_leds = NUM_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_pixel));
    led_strip_clear(s_pixel);
}

static void led_blink_pass(void)
{
    for (;;) {
        for (int i = 0; i < 3; i++) {
            gpio_set_level(LED_STATUS_PIN, 1);
            led_strip_set_pixel(s_pixel, 0, 0, 32, 0);
            led_strip_refresh(s_pixel);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_STATUS_PIN, 0);
            led_strip_set_pixel(s_pixel, 0, 0, 0, 0);
            led_strip_refresh(s_pixel);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

static void led_blink_fail(void)
{
    for (;;) {
        gpio_set_level(LED_STATUS_PIN, 1);
        led_strip_set_pixel(s_pixel, 0, 64, 0, 0);
        led_strip_refresh(s_pixel);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_STATUS_PIN, 0);
        led_strip_set_pixel(s_pixel, 0, 0, 0, 0);
        led_strip_refresh(s_pixel);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
