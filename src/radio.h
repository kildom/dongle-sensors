#ifndef _radio_h_
#define _radio_h_

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	// TODO: uint8_t flags - retry_counter for connection quality statistics and to verify if last response may be not received
	//                     - message_type: measurement, time
	// TODO: uint8_t _reserved
	int16_t temp;
	int16_t voltage;
	// TODO: union: uint8_t time_hi; uint32_t time_lo; // current real time (unix time stamp) if known, zero if unknown
}__attribute__((aligned(4)))  OutputPacket;

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	// TODO: uint8_t flags - ack
	//                     - request: none, fast update, set time, later: change next/all delays, sw update
	// TODO: uint8_t _reserved
	uint16_t _reserved; // TODO: delete
	uint16_t flags; // TODO: delete
	// TODO: union: uint8_t time_hi; uint32_t time_lo; // current real time (unix time stamp) if known, zero if unknown
}__attribute__((aligned(4))) InputPacket;


#endif
