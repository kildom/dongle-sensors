#ifndef _radio_h_
#define _radio_h_

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	int16_t temp;
	int16_t voltage;
}__attribute__((aligned(4)))  OutputPacket;

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	uint16_t _reserved;
	uint16_t flags;
}__attribute__((aligned(4))) InputPacket;

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	// bit -----XXX - retry counter
	// bit ---XX--- - message type: 0 - measurement, 1 - time
	uint8_t flags;
	uint8_t reserved;
	union
	{
		struct
		{
			int16_t temperature;
			int16_t voltage;
		};
		uint32_t time;
	};
}__attribute__((aligned(4)))  OutputPacket2;

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	// bit -----XXX - delay time 2343 * 2^X [ms] - sensor will change delay until next update or after 5 cycles (to the max)
	// bit ---XX--- - message type: 1 - time
	uint8_t flags;
	uint8_t reserved;
	uint32_t time;
}__attribute__((aligned(4))) InputPacket2;


uint32_t get_time();
void packet_received(OutputPacket* packet);


#endif
