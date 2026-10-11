#ifndef _HACKRF_USB_H_
#define _HACKRF_USB_H_

#include <stdint.h>
#include <stdbool.h>

// Called from the main loop to service the synthetic I/Q stream.
void hackrf_task(void);

#endif
