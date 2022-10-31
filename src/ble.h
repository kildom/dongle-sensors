#ifndef _ble_h_
#define _ble_h_

#include <stdint.h>
#include <stdbool.h>

#define REQUEST_SIZE 512
#define RESPONSE_SIZE 512

extern uint8_t ble_request[REQUEST_SIZE];
extern int ble_request_size;
extern uint8_t ble_response[RESPONSE_SIZE];
extern int ble_response_size;

void ble_init();

// callbacks
void ble_packet();

#endif
