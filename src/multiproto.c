
#include <zephyr/zephyr.h>
#include <mpsl.h>
#include <mpsl_timeslot.h>
#include <mpsl/mpsl_work.h>
#include <hal/nrf_timer.h>

#include "leds.h"
#include "multiproto.h"

/* Timeslot calculation
	RX ramp up:              140
	On air output packet:    544 = 8 (preamble) + 16 (base) + 8 (prefix) + 80 (payload) + 24 (crc) = 136 bits / 250000 kbit/s
	RX disable:                4
	packet process:          100
	TX ramp up:              140
	On air input packet:     544 = 8 (preamble) + 16 (base) + 8 (prefix) + 80 (payload) + 24 (crc) = 136 bits / 250000 kbit/s
	TX disable:               15
				-----
				1487 us
	Margin:                 +200 us
*/
static const uint32_t TIME_SLOT_MARGIN_US = 200;
static const uint32_t TIME_SLOT_US = 1500 + TIME_SLOT_MARGIN_US;

// Time needed to "jump" over BLE advertizing
static const uint32_t ADV_SLOT_TIME_US = 6000;
static const uint32_t ADV_JUMP_TIME_US = ADV_SLOT_TIME_US * 4 / 3;

// MPSL timeslot request (first)
static mpsl_timeslot_request_t timeslot_request_earliest = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
	.params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
	.params.earliest.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.earliest.length_us = TIME_SLOT_US,
	.params.earliest.timeout_us = 1000000
};

// MPSL timeslot request (next)
static mpsl_timeslot_request_t timeslot_request_normal = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_NORMAL,
	.params.normal.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
	.params.normal.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.normal.length_us = TIME_SLOT_US
};

// Work for requesting a new timeslot
static struct k_work request_slot_work;

// MPSL session id
static mpsl_timeslot_session_id_t session_id = 0xFFu;

// Flag telling that the next timer compare will end this timeslot and request new after few ms
static bool is_ending_timer = false;

static void schedule_timeslot_on_next_timer() {
	uint32_t counter;
	uint32_t new_counter;
	is_ending_timer = true;
	nrf_timer_task_trigger(NRF_TIMER0, nrf_timer_capture_task_get(NRF_TIMER_CC_CHANNEL1));
	new_counter = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL1);
	do {
		counter = new_counter;
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, counter + 5);
		nrf_timer_task_trigger(NRF_TIMER0, nrf_timer_capture_task_get(NRF_TIMER_CC_CHANNEL1));
		new_counter = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL1);
	} while (new_counter > counter + 1);
	nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
}

static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(mpsl_timeslot_session_id_t session_id, uint32_t signal_type)
{
	static uint32_t end_time_us = 0;
	static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

	switch (signal_type) {
	case MPSL_TIMESLOT_SIGNAL_START:
		end_time_us = TIME_SLOT_US;
		is_ending_timer = false;
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, end_time_us - TIME_SLOT_MARGIN_US);
		nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		led_off(3);
		if (!multiproto_start_callback()) {
			schedule_timeslot_on_next_timer();
		}
		break;

	case MPSL_TIMESLOT_SIGNAL_TIMER0:
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);
		if (is_ending_timer) {
			led_on(3);
			timeslot_request_normal.params.normal.distance_us = end_time_us + ADV_JUMP_TIME_US;
			signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
			is_ending_timer = false;
		} else {
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_EXTEND;
			signal_callback_return_param.params.extend.length_us = TIME_SLOT_US;
		}
		return &signal_callback_return_param;

	case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED:
		nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		if (!multiproto_end_callback()) {
			schedule_timeslot_on_next_timer();
		}
		break;

	case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
		end_time_us += TIME_SLOT_US;
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, end_time_us - TIME_SLOT_MARGIN_US);
		break;

	case MPSL_TIMESLOT_SIGNAL_RADIO:
		if (!multiproto_radio_callback()) {
			led_on(3);
			timeslot_request_normal.params.normal.distance_us = end_time_us + ADV_JUMP_TIME_US;
			signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
			return &signal_callback_return_param;
		}
		break;

	case MPSL_TIMESLOT_SIGNAL_BLOCKED:
		mpsl_work_submit(&request_slot_work);
		break;

	case MPSL_TIMESLOT_SIGNAL_CANCELLED:
		mpsl_work_submit(&request_slot_work);
		break;

	case MPSL_TIMESLOT_SIGNAL_INVALID_RETURN:
	case MPSL_TIMESLOT_SIGNAL_OVERSTAYED:
	case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
	case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
	default:
		break;
	}

	signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
	return &signal_callback_return_param;
}


static void work_handler(struct k_work *work)
{
	static bool session_opened = false;
	int err;
	if (!session_opened) {
		err = mpsl_timeslot_session_open(mpsl_timeslot_callback, &session_id);
		if (err) {
			printk("Timeslot session open error: %d\n", err);
			k_oops();
		}
		session_opened = true;
		mpsl_work_submit(&request_slot_work);
		return;
	}
	err = mpsl_timeslot_request(session_id, &timeslot_request_earliest);
	if (err) {
		printk("Timeslot request error: %d\n", err);
		k_oops();
	}
}


void multiproto_init()
{
	led_on(3);
	k_work_init(&request_slot_work, work_handler);
	mpsl_work_submit(&request_slot_work);
}

