
#include <zephyr/kernel.h>
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

static uint32_t end_time_us = 0;

static mpsl_timeslot_signal_return_param_t signal_callback_return_param;


static void safe_set_cc(nrf_timer_cc_channel_t channel, int time)
{
	uint32_t counter;
	uint32_t new_counter;
	if (time < 5) {
		time = 5;
	}
	nrf_timer_task_trigger(NRF_TIMER0, nrf_timer_capture_task_get(NRF_TIMER_CC_CHANNEL3));
	new_counter = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL3);
	do {
		counter = new_counter;
		nrf_timer_cc_set(NRF_TIMER0, channel, counter + time);
		nrf_timer_task_trigger(NRF_TIMER0, nrf_timer_capture_task_get(NRF_TIMER_CC_CHANNEL3));
		new_counter = nrf_timer_cc_get(NRF_TIMER0, NRF_TIMER_CC_CHANNEL3);
	} while (new_counter > counter + 1);
}


static void handle_callback(MultiProtoEvent event, bool allow_fast_end)
{
	int req = multiproto_callback(event);

	if (req == MULTIPROTO_REQ_CONTINUE) {
		// nothing to do
	} else if (req == MULTIPROTO_REQ_END) {
		if (allow_fast_end) {
			nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK | NRF_TIMER_INT_COMPARE1_MASK | NRF_TIMER_INT_COMPARE2_MASK);
			led_on(3);
			timeslot_request_normal.params.normal.distance_us = end_time_us + ADV_JUMP_TIME_US;
			signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		} else {
			safe_set_cc(NRF_TIMER_CC_CHANNEL1, 0);
		}
	} else {
		safe_set_cc(NRF_TIMER_CC_CHANNEL2, req);
	}
}


static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(mpsl_timeslot_session_id_t session_id, uint32_t signal_type)
{
	signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;

	switch (signal_type) {

	case MPSL_TIMESLOT_SIGNAL_START:
		end_time_us = TIME_SLOT_US;
		nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK | NRF_TIMER_INT_COMPARE1_MASK | NRF_TIMER_INT_COMPARE2_MASK | NRF_TIMER_INT_COMPARE2_MASK);
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, end_time_us - TIME_SLOT_MARGIN_US);
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL1, 0xFFFFFFFF);
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1);
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL2, 0xFFFFFFFF);
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE2);
		nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK | NRF_TIMER_INT_COMPARE1_MASK | NRF_TIMER_INT_COMPARE2_MASK);
		led_off(3);
		handle_callback(MULTIPROTO_EV_START, false);
		break;

	case MPSL_TIMESLOT_SIGNAL_TIMER0:
		if (nrf_timer_event_check(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0)) {
			nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_EXTEND;
			signal_callback_return_param.params.extend.length_us = TIME_SLOT_US;
		}

		if (nrf_timer_event_check(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE1)) {
			nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK | NRF_TIMER_INT_COMPARE1_MASK | NRF_TIMER_INT_COMPARE2_MASK);
			led_on(3);
			timeslot_request_normal.params.normal.distance_us = end_time_us + ADV_JUMP_TIME_US;
			signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
			signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		} else if (nrf_timer_event_check(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE2)) {
			nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE2);
			handle_callback(MULTIPROTO_EV_TIMER, true);
		}
		break;

	case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED:
		handle_callback(MULTIPROTO_EV_END, false);
		break;

	case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
		end_time_us += TIME_SLOT_US;
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, end_time_us - TIME_SLOT_MARGIN_US);
		break;

	case MPSL_TIMESLOT_SIGNAL_RADIO:
		handle_callback(MULTIPROTO_EV_RADIO, true);
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

