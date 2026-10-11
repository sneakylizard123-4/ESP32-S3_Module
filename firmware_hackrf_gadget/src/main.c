#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tusb.h"
#include "hackrf_usb.h"

static const char *TAG = "hackrf-gadget";

void app_main(void) {
  ESP_LOGI(TAG, "Fake HackRF USB gadget starting (VID 0x1d50 PID 0x6089)");

  tusb_init();

  while (1) {
    tud_task_ext(1, false);  // service USB, wait up to 1 ms
    hackrf_task();           // feed synthetic I/Q when streaming
  }
}
