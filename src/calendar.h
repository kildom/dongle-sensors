#ifndef _calendar_h_
#define _calendar_h_

#include <stdint.h>
#include <stdbool.h>

#ifdef CALENDAR_SHORT_TIMESTAMP
typedef int32_t TimeStamp;
#else
typedef int64_t TimeStamp;
#endif

typedef struct {
	int16_t year;
	int16_t month;
	int16_t day;
	int16_t hour;
	int16_t min;
	int16_t sec;
	bool leap;
	uint8_t day_of_week;
} DateTime;

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


void convert_time_utc(TimeStamp time, DateTime* date);
TimeStamp make_time_utc(const DateTime* date);
void convert_time_tz(TimeStamp time, DateTime* date, const TimeZone* tz);
TimeStamp make_time_tz(const DateTime* date, const TimeZone* tz);

#endif
