#ifndef _ble_h_
#define _ble_h_

#include <stdint.h>
#include <stdbool.h>

void ble_init();

uint8_t *ble_response_prepare(size_t* max_size);
void ble_response_send(size_t size);

const uint8_t *ble_request_get(size_t* size);

// callbacks
void ble_packet();

#endif
