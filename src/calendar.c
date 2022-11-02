
#include "calendar.h"

static const int SECONDS_PER_MINUTE = 60;
static const int SECONDS_PER_HOUR = 60 * SECONDS_PER_MINUTE;
static const int SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;
static const int DAYS_PER_LEAP_YEAR = 366;
static const int DAYS_PER_COMMON_YEAR = 365;
static const int DAYS_PER_COMMON_4_YEARS = 4 * DAYS_PER_COMMON_YEAR + 1;
static const int DAYS_PER_COMMON_100_YEARS = 25 * DAYS_PER_COMMON_4_YEARS - 1;
static const int DAYS_PER_400_YEARS = 4 * DAYS_PER_COMMON_100_YEARS + 1;
static const int DAYS_BEFORE_1970 = 719528;
static const int FIRST_DAY_OF_WEAK_OF_YEAR_0 = 6;


static int days_before_month(int month, bool leap_year) {
	int days = (month * 214 + 3) / 7;
	if (month >= 2) {
		days -= leap_year ? 1 : 2;
	}
	return days;
}


static int days_of_month(int month, bool leap_year) {
	month++;
	int days = 30 + ((month ^ (month >> 3)) & 1);
	if (month == 2) {
		days -= leap_year ? 1 : 2;
	}
	return days;
}


static void days_from_0_to_date(int day_from_0, DateTime* date) {
	int c400;
	int c100;
	int c4;
	int c1;
	int day_of_year;
	int year;
	int month;
	int day;
	bool leap;

	// TODO: if CALENDAR_SHORT_TIMESTAMP then only every 4 years rule applies, so other rules can be removed
	date->day_of_week = (FIRST_DAY_OF_WEAK_OF_YEAR_0 + day_from_0) % 7;
	c400 = day_from_0 / DAYS_PER_400_YEARS;
	day_of_year = day_from_0 % DAYS_PER_400_YEARS;
	year = 400 * c400;
	leap = true;
	if (day_of_year >= DAYS_PER_LEAP_YEAR) {
		day_of_year = day_of_year - 1;
		c100 = day_of_year / DAYS_PER_COMMON_100_YEARS;
		day_of_year = day_of_year % DAYS_PER_COMMON_100_YEARS;
		year += 100 * c100;
		leap = false;
		if (day_of_year >= DAYS_PER_COMMON_YEAR) {
			day_of_year = day_of_year + 1;
			c4 = day_of_year / DAYS_PER_COMMON_4_YEARS;
			day_of_year = day_of_year % DAYS_PER_COMMON_4_YEARS;
			year += 4 * c4;
			leap = true;
			if (day_of_year >= DAYS_PER_LEAP_YEAR) {
				day_of_year = day_of_year - 1;
				c1 = day_of_year / DAYS_PER_COMMON_YEAR;
				day_of_year = day_of_year % DAYS_PER_COMMON_YEAR;
				year += c1;
				leap = false;
			}
		}
	}
	if (leap) {
		month = (day_of_year * 2 + 2) / 61;
	} else {
		month = (day_of_year * 50 + 107) / 1526;
	}
	day = day_of_year - days_before_month(month, leap);
	if (day < 0) {
		month -= 1;
		day = 31 + day;
	}
	date->year = year;
	date->month = month + 1;
	date->day = day + 1;
	date->leap = leap;
}

static int normalize_date(const DateTime* date, int* time_of_day_ptr)
{
	// Adjust and convert date
	bool leap;
	int leap_years;
	int days_from_0;
	int time_of_day;
	int year = date->year;
	int month = date->month - 1;
	int day = date->day - 1;
	int hour = date->hour;
	int min = date->min;
	int sec = date->sec;
	int months_from_0 = year * 12 + month;

	year = months_from_0 / 12;
	month = months_from_0 % 12;
	leap_years = (year + 3) / 4;
	leap_years -= (year + 99) / 100;
	leap_years += (year + 399) / 400;
	days_from_0 = year * DAYS_PER_COMMON_YEAR + leap_years;
	leap = (!(year % 4) && (year % 100)) || !(year % 400);
	days_from_0 += days_before_month(month, leap);
	days_from_0 += day;

	// Adjust time, add it to days if needed, and convert it to seconds from midnight
	time_of_day = sec + 60 * (min + 60 * hour);
	if (time_of_day < 0) {
		int sub_days = (-time_of_day + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY;
		days_from_0 -= sub_days;
		time_of_day += SECONDS_PER_DAY * sub_days;
	} else {
		days_from_0 += time_of_day / SECONDS_PER_DAY;
		time_of_day %= SECONDS_PER_DAY;
	}

	*time_of_day_ptr = time_of_day;
	return days_from_0;
}

static int calc_transition_day(const DaylightTransition* tr, int day, int day_of_week, bool leap) {
	if (tr->day < 0) {
		return -tr->day;
	}
	int tr_month = tr->month - 1;
	int tr_day = tr->day;
	int tr_week = tr->week;
	day--;
	int days_per_month = days_of_month(tr_month, leap);
	if (tr_week >= 0) {
		int first_day_of_month = (day_of_week - day + 35) % 7;
		int first_tr_day_of_month = tr_day - first_day_of_month;
		if (first_tr_day_of_month < 0) {
			first_tr_day_of_month += 7;
		}
		int tr_day_of_month = first_tr_day_of_month + 7 * tr_week;
		while (tr_day_of_month >= days_per_month) {
			tr_day_of_month -= 7;
		}
		return tr_day_of_month + 1;
	} else {
		int last_day_of_month = (day_of_week - day + days_per_month + 6) % 7;
		int last_tr_day_of_month = days_per_month - 1 + tr_day - last_day_of_month;
		if (last_tr_day_of_month < days_per_month) {
			last_tr_day_of_month += 7;
		}
		int tr_day_of_month = last_tr_day_of_month + 7 * tr_week;
		while (tr_day_of_month < 0) {
			tr_day_of_month += 7;
		}
		return tr_day_of_month + 1;
	}
}

static bool is_daylight_enabled(const DateTime* date, int time_of_day, const TimeZone* tz, int delta)
{
	int day = date->day;
	if (tz->daylight_delta == 0 || date->month < tz->daylight_start.month || date->month > tz->daylight_end.month) {
		return false;
	} else if (date->month > tz->daylight_start.month && date->month < tz->daylight_end.month) {
		return true;
	} else if (date->month == tz->daylight_start.month) {
		int tr_day = calc_transition_day(&tz->daylight_start, day, date->day_of_week, date->leap);
		if (day < tr_day) {
			return false;
		} else if (day > tr_day) {
			return true;
		} else if (time_of_day < (int)tz->daylight_start.time * SECONDS_PER_MINUTE + delta) {
			return false;
		} else {
			return true;
		}
	} else {
		int tr_day = calc_transition_day(&tz->daylight_end, day, date->day_of_week, date->leap);
		if (day < tr_day) {
			return true;
		} else if (day > tr_day) {
			return false;
		} else if (time_of_day < (int)tz->daylight_end.time * SECONDS_PER_MINUTE + delta) {
			return true;
		} else {
			return false;
		}
	}
}

void convert_time_utc(TimeStamp time, DateTime* date) {
	int day_from_1970 = time / (TimeStamp)SECONDS_PER_DAY;
	int time_of_day = time - (TimeStamp)SECONDS_PER_DAY * (TimeStamp)day_from_1970;
	int day_from_0 = DAYS_BEFORE_1970 + day_from_1970;
	days_from_0_to_date(day_from_0, date);
	date->sec = time_of_day % 60;
	time_of_day /= 60;
	date->min = time_of_day % 60;
	date->hour = time_of_day / 60;
}


TimeStamp make_time_utc(const DateTime* date)
{
	int time_of_day;
	int days_from_0 = normalize_date(date, &time_of_day);
	return (TimeStamp)(days_from_0 - DAYS_BEFORE_1970) * (TimeStamp)SECONDS_PER_DAY + (TimeStamp)time_of_day;
}

void convert_time_tz(TimeStamp time, DateTime* date, const TimeZone* tz) {
	TimeStamp tz_time = time + (TimeStamp)tz->utc_offset * SECONDS_PER_MINUTE;
	convert_time_utc(tz_time, date);
	int time_of_day = (int)date->sec + 60 * ((int)date->min + 60 * (int)date->hour);
	if (is_daylight_enabled(date, time_of_day, tz, 0)) {
		convert_time_utc(tz_time + (TimeStamp)tz->daylight_delta * SECONDS_PER_MINUTE, date);
	}
}

TimeStamp make_time_tz(const DateTime* date, const TimeZone* tz)
{
	DateTime temp_date;
	int time_of_day;
	int days_from_0 = normalize_date(date, &time_of_day);
	TimeStamp result = (TimeStamp)(days_from_0 - DAYS_BEFORE_1970) * (TimeStamp)SECONDS_PER_DAY + (TimeStamp)time_of_day - (TimeStamp)tz->utc_offset * (TimeStamp)SECONDS_PER_MINUTE;
	days_from_0_to_date(days_from_0, &temp_date);
	if (is_daylight_enabled(&temp_date, time_of_day, tz, (TimeStamp)tz->daylight_delta * SECONDS_PER_MINUTE)) {
		result -= (TimeStamp)tz->daylight_delta * SECONDS_PER_MINUTE;
	}
	return result;
}
