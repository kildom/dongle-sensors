
const SECONDS_PER_MINUTE = 60;
const SECONDS_PER_HOUR = 60 * SECONDS_PER_MINUTE;
const SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;
const DAYS_PER_LEAP_YEAR = 366;
const DAYS_PER_COMMON_YEAR = 365;
const DAYS_PER_COMMON_4_YEARS = 4 * DAYS_PER_COMMON_YEAR + 1;
const DAYS_PER_COMMON_100_YEARS = 25 * DAYS_PER_COMMON_4_YEARS - 1;
const DAYS_PER_400_YEARS = 4 * DAYS_PER_COMMON_100_YEARS + 1;
const DAYS_BEFORE_1970 = 719528;
const FIRST_DAY_OF_WEAK_OF_YEAR_0 = 6;


function days_before_month(month, leap_year) {
	days = Math.floor((month * 214 + 3) / 7);
	if (month >= 2) {
		days -= leap_year ? 1 : 2;
	}
	return days;
}

function days_of_month(month, leap_year) {
	month++;
	let days = 30 + ((month ^ (month >> 3)) & 1);
	if (month == 2) {
		days -= leap_year ? 1 : 2;
	}
	return days;
}


function date_from_days_from_0(day_from_0) {
	let day_of_week = (FIRST_DAY_OF_WEAK_OF_YEAR_0 + day_from_0) % 7;
	let c400 = Math.floor(day_from_0 / DAYS_PER_400_YEARS);
	let day_of_year = day_from_0 % DAYS_PER_400_YEARS;
	let year = 400 * c400;
	let leap = true;
	if (day_of_year >= DAYS_PER_LEAP_YEAR) {
		day_of_year = day_of_year - 1;
		let c100 = Math.floor(day_of_year / DAYS_PER_COMMON_100_YEARS);
		day_of_year = day_of_year % DAYS_PER_COMMON_100_YEARS;
		year += 100 * c100;
		leap = false;
		if (day_of_year >= DAYS_PER_COMMON_YEAR) {
			day_of_year = day_of_year + 1;
			let c4 = Math.floor(day_of_year / DAYS_PER_COMMON_4_YEARS);
			day_of_year = day_of_year % DAYS_PER_COMMON_4_YEARS;
			year += 4 * c4;
			leap = true;
			if (day_of_year >= DAYS_PER_LEAP_YEAR) {
				day_of_year = day_of_year - 1;
				c1 = Math.floor(day_of_year / DAYS_PER_COMMON_YEAR);
				day_of_year = day_of_year % DAYS_PER_COMMON_YEAR;
				year += c1;
				leap = false;
			}
		}
	}
	let month;
	let day;
	if (leap) {
		month = Math.floor((day_of_year * 2 + 2) / 61);
	} else {
		month = Math.floor((day_of_year * 50 + 107) / 1526);
	}
	day = day_of_year - days_before_month(month, leap);
	if (day < 0) {
		month -= 1;
		day = 31 + day;
	}
	month++;
	day++;
	return [year, month, day, day_of_week, leap];
}

function convert_time_utc(time) {
	let day_from_1970 = Math.floor(time / SECONDS_PER_DAY);
	let time_of_day = time - SECONDS_PER_DAY * day_from_1970;
	let day_from_0 = DAYS_BEFORE_1970 + day_from_1970;
	let [year, month, day, day_of_week, leap] = date_from_days_from_0(day_from_0);
	let hour = time_of_day;
	let sec = hour % 60;
	hour = Math.floor(hour / 60);
	let min = hour % 60;
	hour = Math.floor(hour / 60);
	return [year, month, day, hour, min, sec, day_of_week, leap];
}

function normalize_date(year, month, day, hour, min, sec)
{
	// Adjust and convert date
	month -= 1;
	day -= 1;
	let months_from_0 = year * 12 + month;
	year = Math.floor(months_from_0 / 12);
	month = months_from_0 % 12;
	let leap_years = Math.floor((year + 3) / 4);
	leap_years -= Math.floor((year + 99) / 100);
	leap_years += Math.floor((year + 399) / 400);
	let days_from_0 = year * DAYS_PER_COMMON_YEAR + leap_years;
	let leap = (!(year % 4) && (year % 100)) || !(year % 400);
	days_from_0 += days_before_month(month, leap);
	days_from_0 += day;

	// Adjust time, add it to days if needed, and convert it to seconds from midnight
	let time_of_day = sec + 60 * (min + 60 * hour);
	if (time_of_day < 0) {
		let sub_days = Math.floor((-time_of_day + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY);
		days_from_0 -= sub_days;
		time_of_day += SECONDS_PER_DAY * sub_days;
	} else {
		days_from_0 += Math.floor(time_of_day / SECONDS_PER_DAY);
		time_of_day %= SECONDS_PER_DAY;
	}

	return [days_from_0, time_of_day];
}

function make_time_utc(year, month, day, hour, min, sec)
{
	let [days_from_0, time_of_day] = normalize_date(year, month, day, hour, min, sec);
	return (days_from_0 - DAYS_BEFORE_1970) * SECONDS_PER_DAY + time_of_day;
}

time_zone = {
	utc_offset: 1 * 60 * 60,
	daylight: {
		delta: 1 * 60 * 60,
		start_month: 3,
		start_day: 0, // negative for fixed, positive or zero for floating
		start_week: -1, // negative counts backwards from end of month
		start_time: 2 * 60 * 60, // relative to base utc_offset
		// TODO: pack start_* and end_* into structures
		end_month: 10,
		end_day: 0,
		end_week: -1,
		end_time: 2 * 60 * 60, // relative to base utc_offset
	}
};

function calc_transition_day(tr_month, tr_day, tr_week, day, day_of_week, leap_year) {
	if (tr_day < 0) {
		return -tr_day;
	}
	tr_month--;
	day--;
	let days_per_month = days_of_month(tr_month, leap_year);
	if (tr_week >= 0) {
		let first_day_of_month = (day_of_week - day + 35) % 7;
		let first_tr_day_of_month = tr_day - first_day_of_month;
		if (first_tr_day_of_month < 0) {
			first_tr_day_of_month += 7;
		}
		let tr_day_of_month = first_tr_day_of_month + 7 * tr_week;
		while (tr_day_of_month >= days_per_month) {
			tr_day_of_month -= 7;
		}
		return tr_day_of_month + 1;
	} else {
		let last_day_of_month = (day_of_week - day + days_per_month + 6) % 7;
		let last_tr_day_of_month = days_per_month - 1 + tr_day - last_day_of_month;
		if (last_tr_day_of_month < days_per_month) {
			last_tr_day_of_month += 7;
		}
		let tr_day_of_month = last_tr_day_of_month + 7 * tr_week;
		while (tr_day_of_month < 0) {
			tr_day_of_month += 7;
		}
		return tr_day_of_month + 1;
	}
}


function is_daylight_enabled(month, day, time_of_day, day_of_week, leap_year, tz, delta)
{
	if (tz.daylight.delta == 0 || month < tz.daylight.start_month || month > tz.daylight.end_month) {
		return false;
	} else if (month > tz.daylight.start_month && month < tz.daylight.end_month) {
		return true;
	} else if (month == tz.daylight.start_month) {
		let tr_day = calc_transition_day(tz.daylight.start_month, tz.daylight.start_day, tz.daylight.start_week, day, day_of_week, leap_year);
		if (day < tr_day) {
			return false;
		} else if (day > tr_day) {
			return true;
		} else if (time_of_day < tz.daylight.start_time + delta) {
			return false;
		} else {
			return true;
		}
	} else {
		let tr_day = calc_transition_day(tz.daylight.end_month, tz.daylight.end_day, tz.daylight.end_week, day, day_of_week, leap_year);
		if (day < tr_day) {
			return true;
		} else if (day > tr_day) {
			return false;
		} else if (time_of_day < tz.daylight.end_time + delta) {
			return true;
		} else {
			return false;
		}
	}
}

function convert_time_tz(time, tz) {
	let tz_time = time + tz.utc_offset;
	let [year, month, day, hour, min, sec, day_of_week, leap_year] = convert_time_utc(tz_time);
	let time_of_day = sec + 60 * (min + 60 * hour);
	if (is_daylight_enabled(month, day, time_of_day, day_of_week, leap_year, tz, 0)) {
		return convert_time_utc(tz_time + tz.daylight.delta);
	} else {
		return [year, month, day, hour, min, sec, day_of_week, leap_year];
	}
}

function make_time_tz(year, month, day, hour, min, sec, tz)
{
	let [days_from_0, time_of_day] = normalize_date(year, month, day, hour, min, sec);
	[year, month, day, day_of_week, leap_year] = date_from_days_from_0(days_from_0);
	let result = (days_from_0 - DAYS_BEFORE_1970) * SECONDS_PER_DAY + time_of_day - tz.utc_offset;
	if (is_daylight_enabled(month, day, time_of_day, day_of_week, leap_year, tz, tz.daylight.delta)) {
		result -= tz.daylight.delta;
	}
	return result;
}


const event = new Date();
let time = Math.floor(event.getTime() / 1000);
console.log(convert_time_tz(time, time_zone));
//process.exit();

for (let y = 1000; y < 4000; y++) {
	console.log(y);
	for (let m = 0; m < 12; m++) {
		for (let d = 1; d <= 31; d++) {
			let hour = Math.floor(Math.random() * 24);
			let min = Math.floor(Math.random() * 60);
			let sec = Math.floor(Math.random() * 60);
			let date = new Date(0);
			date.setUTCFullYear(y, m, d);
			date.setUTCHours(hour, min, sec);
			let time = Math.floor(date.getTime() / 1000);
			let x = convert_time_utc(time);
			if (x[0] != date.getUTCFullYear()) throw `Different Year ${x[0]} != ${date.getUTCFullYear()}   --   ${date.toISOString()}`;
			if (x[1] != date.getUTCMonth() + 1) throw `Different Month ${x[1]} != ${date.getUTCMonth() + 1}   --   ${date.toISOString()}`;
			if (x[2] != date.getUTCDate()) throw `Different Date ${x[2]} != ${date.getUTCDate()}   --   ${date.toISOString()}`;
			if (x[3] != date.getUTCHours()) throw `Different Hours ${x[3]} != ${date.getUTCHours()}   --   ${date.toISOString()}`;
			if (x[4] != date.getUTCMinutes()) throw `Different Minutes ${x[4]} != ${date.getUTCMinutes()}   --   ${date.toISOString()}`;
			if (x[5] != date.getUTCSeconds()) throw `Different Seconds ${x[5]} != ${date.getUTCSeconds()}   --   ${date.toISOString()}`;
			if (x[6] != date.getUTCDay()) throw `Different Day Of Week ${x[6]} != ${date.getUTCDay()}   --   ${date.toISOString()}`;
			let t = make_time_utc(y, m + 1, d, hour, min, sec);
			if (t != time) throw `Different time output ${t} != ${time}   --   ${date.toISOString()}`;

			hour = Math.floor(Math.random() * 24000);
			min = Math.floor(Math.random() * 60000);
			sec = Math.floor(Math.random() * 60000);
			let yy = y + Math.floor(Math.random() * 20 - 10);
			let mm = m + Math.floor(Math.random() * 2000 - 1000);
			let dd = d + Math.floor(Math.random() * 2000 - 1000);
			date.setUTCFullYear(yy, mm, dd);
			date.setUTCHours(hour, min, sec);
			time = Math.floor(date.getTime() / 1000);
			t = make_time_utc(yy, mm + 1, dd, hour, min, sec);
			if (t != time) throw `Different time output ${t} != ${time}   --   ${date.toISOString()}`;
		}
	}
}

for (let y = 1996; y < 4000; y++) {
	console.log(y);
	for (let m = 0; m < 12; m++) {
		for (let d = 1; d <= 31; d++) {
			let hour = Math.floor(Math.random() * 4);
			let min = Math.floor(Math.random() * 600);
			let sec = Math.floor(Math.random() * 600);
			let date = new Date(0);
			date.setFullYear(y, m, d);
			date.setHours(hour, min, sec);
			let time = Math.floor(date.getTime() / 1000);
			let x = convert_time_tz(time, time_zone);
			if (x[0] != date.getFullYear()) throw `Different Year ${x[0]} != ${date.getFullYear()}   --   ${date.toISOString()}`;
			if (x[1] != date.getMonth() + 1) throw `Different Month ${x[1]} != ${date.getMonth() + 1}   --   ${date.toISOString()}`;
			if (x[2] != date.getDate()) throw `Different Date ${x[2]} != ${date.getDate()}   --   ${date.toISOString()}`;
			if (x[3] != date.getHours()) throw `Different Hours ${x[3]} != ${date.getHours()}   --   ${date.toISOString()}`;
			if (x[4] != date.getMinutes()) throw `Different Minutes ${x[4]} != ${date.getMinutes()}   --   ${date.toISOString()}`;
			if (x[5] != date.getSeconds()) throw `Different Seconds ${x[5]} != ${date.getSeconds()}   --   ${date.toISOString()}`;
			if (x[6] != date.getDay()) throw `Different Day Of Week ${x[6]} != ${date.getDay()}   --   ${date.toISOString()}`;
			let t = make_time_tz(y, m + 1, d, hour, min, sec, time_zone);
			if (t != time) throw `Different time output ${t} != ${time}   --   ${date.toISOString()}`;
		}
	}
}
