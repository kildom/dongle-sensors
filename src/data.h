#ifndef _data_h_
#define _data_h_

#include <stdint.h>
#include <stdbool.h>

#include "hash.h"

typedef struct {
	int16_t time; // relative to base utc_offset
	int8_t month;
	int8_t day; // negative for fixed, positive or zero for floating
	int8_t week; // negative counts backwards from end of month
} DaylightTransition;

typedef struct {
	int16_t utc_offset;
	int16_t daylight_delta; // Daylight disabled if zero
	DaylightTransition daylight_start;
	DaylightTransition daylight_end;
} TimeZone;

typedef enum {
	FUNC_MIN,
	FUNC_MAX,
	FUNC_AVG,
} ChannelFunction;

typedef struct {
	uint32_t addr_low;
	uint16_t addr_high;
	uint8_t channel;
	char name[48];
} ConfigNode;

typedef struct {
	uint8_t func;
	char name[48];
} ConfigChannel;

typedef struct {
	uint8_t config_version;
	uint8_t node_count;
	uint8_t channel_count;
	uint8_t _reserved_1;
	TimeZone time_zone;
	ConfigNode nodes[32];
	ConfigChannel channels[8];
} Config;

typedef struct {
	uint32_t last_update;
	uint16_t temperature;
	uint16_t voltage;
} StateNode;

typedef struct {
	uint16_t temperature;
} StateChannel;

typedef struct {
	volatile uint32_t time_shift;
	StateNode nodes[32];
	StateChannel channels[8];
} State;

extern Config data_config;
extern State data_state;

void data_config_keep();
void data_init();

#endif // _data_h_
