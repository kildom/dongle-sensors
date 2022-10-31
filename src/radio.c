
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <mpsl/mpsl_work.h>
#include <mpsl_timeslot.h>
#include <mpsl.h>
#include <hal/nrf_timer.h>

#include "multiproto.h"
#include "leds.h"
#include "radio.h"

#define LOG_LEVEL CONFIG_DEFAULT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_radio);


#pragma GCC diagnostic ignored "-Wdangling-pointer"

static const MultiProtoEvent RECV_EV_RESERVED = (MultiProtoEvent)(MULTIPROTO_COUNT_EV + 1);

#define _AWAIT2(l, c) \
	_resume_point = &&_resume_label_##l##_##c; \
	ev = RECV_EV_RESERVED; \
	_resume_label_##l##_##c: \
	do { } while (0)
#define _AWAIT1(l, c) _AWAIT2(l, c)
#define AWAIT _AWAIT1(__LINE__, __COUNTER__)

#define ASYNC_BEGIN \
	static void* _resume_point = &&_resume_label_start; \
	goto *_resume_point; \
	_resume_label_start: \
	do { } while (0)


__attribute__((aligned(4)))
static uint8_t packet[sizeof(InputPacket)];
static OutputPacket *const output_packet = (OutputPacket*)&packet[0];
static InputPacket *const input_packet = (InputPacket*)&packet[0];

static const uint32_t FREQUENCY = 2400;
static const uint32_t BASE_ADDR = 0x63e0;
static const uint32_t PREFIX_BYTE_ADDR = 0x17;
static const uint32_t CRC_POLY = 0x864CFB; // CRC-24-Radix-64 (OpenPGP)
static const uint16_t INPUT_FLAG_ACK = 0x8000;


int multiproto_callback(MultiProtoEvent ev) {

	LOG_WRN("receive_async_func %d %d", ev, NRF_RADIO->STATE);

	if (ev == MULTIPROTO_EV_START) {
		goto start_from_beginning;
	}
	ASYNC_BEGIN;
	start_from_beginning:
	
	// Init minimal
	NRF_RADIO->POWER = 1;
	NRF_RADIO->INTENCLR = 0xFFFFFFFF;
	NVIC_EnableIRQ(RADIO_IRQn);
	NVIC_SetPriority(RADIO_IRQn, 0);

	// If other protocol left radio enabled, disable it
	if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->TASKS_DISABLE = 1;
		if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
			AWAIT;
			if (ev == MULTIPROTO_EV_END) goto end_during_disabling;
			if (!NRF_RADIO->EVENTS_DISABLED) return MULTIPROTO_REQ_CONTINUE;
			NRF_RADIO->EVENTS_DISABLED = 0;
		}
	}

	// Continue radio initialization
	NRF_RADIO->PACKETPTR = (uint32_t)&packet[0];
	NRF_RADIO->FREQUENCY = FREQUENCY - 2400;
	NRF_RADIO->MODE = 2;
	NRF_RADIO->PCNF0 =
		(0 << RADIO_PCNF0_LFLEN_Pos) |
		(0 << RADIO_PCNF0_S0LEN_Pos) |
		(0 << RADIO_PCNF0_S1LEN_Pos);
	NRF_RADIO->PCNF1 =
		(10 << RADIO_PCNF1_MAXLEN_Pos) |
		(10 << RADIO_PCNF1_STATLEN_Pos) |
		(2 << RADIO_PCNF1_BALEN_Pos) |
		(RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos);
	NRF_RADIO->BASE0 = BASE_ADDR;
	NRF_RADIO->PREFIX0 = PREFIX_BYTE_ADDR << RADIO_PREFIX0_AP0_Pos;
	NRF_RADIO->TXADDRESS = 0;
	NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Msk;
	NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos;
	NRF_RADIO->CRCPOLY = CRC_POLY;
	NRF_RADIO->CRCINIT = 0;
	NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos4dBm;

	// Loop over single cycle: RX and TX pair
	while (1) {
		// Enable RX and (with short) start receiving packets
		NRF_RADIO->EVENTS_END = 0;
		NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
		NRF_RADIO->INTENCLR = 0xFFFFFFFF;
		NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
		NRF_RADIO->TASKS_RXEN = 1;
		// Loop over packets receiving until we get valid packet
		while (1) {
			// Wait for RX packet end (or timeslot "END" event)
			AWAIT;
			LOG_WRN("resumed %d %d", ev, NRF_RADIO->STATE);
			if (ev == MULTIPROTO_EV_END) goto end_at_rx;
			if (!NRF_RADIO->EVENTS_END) return MULTIPROTO_REQ_CONTINUE;
			NRF_RADIO->EVENTS_END = 0;
			led_toggle(0);
			// Check if packet is valid
			if ((NRF_RADIO->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) != (RADIO_CRCSTATUS_CRCSTATUS_CRCOk << RADIO_CRCSTATUS_CRCSTATUS_Pos) ||
				(NRF_RADIO->RXMATCH & RADIO_RXMATCH_RXMATCH_Msk) != 0)
			{
				// Resume receiving if invalid
				NRF_RADIO->TASKS_START = 1;
				continue;
			} else {
				// Break this loop to process valid packet further
				break;
			}
		}
		led_toggle(1);
		// Disable RX
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->INTENCLR = RADIO_INTENSET_END_Msk;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->TASKS_DISABLE = 1;
		packet_received(output_packet);
		// Wait until RX disabled (or timeslot "END" event)
		AWAIT;
		if (ev == MULTIPROTO_EV_END) goto end_during_disabling;
		if (!NRF_RADIO->EVENTS_DISABLED) return MULTIPROTO_REQ_CONTINUE;
		NRF_RADIO->EVENTS_DISABLED = 0;
		AWAIT;
		if (ev == MULTIPROTO_EV_END) goto power_off;
		if (ev != MULTIPROTO_EV_TIMER) return MULTIPROTO_REQ_TIMER(50);
		// TODO: new packet format
		input_packet->_reserved = 0;
		input_packet->flags = INPUT_FLAG_ACK;
		__DMB();
		// Enable TX and (with short) send packet and (with short) disable TX
		NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->TASKS_TXEN = 1;
		// Wait until TX disabled (or timeslot "END" event)
		AWAIT;
		if (ev == MULTIPROTO_EV_END) goto end_at_tx;
		if (!NRF_RADIO->EVENTS_DISABLED) return MULTIPROTO_REQ_CONTINUE;
		NRF_RADIO->EVENTS_DISABLED = 0;
	}

end_at_tx:
	if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->TASKS_DISABLE = 1;
		if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
			goto end_during_disabling;
		}
	}
	goto power_off;
end_at_rx:
	LOG_WRN("end at rx %d %d %d", ev, NRF_RADIO->STATE, NRF_RADIO->EVENTS_DISABLED);
	NRF_RADIO->EVENTS_DISABLED = 0;
	NRF_RADIO->SHORTS = 0;
	NRF_RADIO->INTENCLR = RADIO_INTENSET_END_Msk;
	NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
	NRF_RADIO->TASKS_DISABLE = 1;
end_during_disabling:
	LOG_WRN("wait for disabled %d %d", NRF_RADIO->STATE, NRF_RADIO->EVENTS_DISABLED);
	_resume_point = &&_resume_label_aaaa;
	return MULTIPROTO_REQ_CONTINUE;
	_resume_label_aaaa:
	LOG_WRN("resume state %d %d", NRF_RADIO->STATE, NRF_RADIO->EVENTS_DISABLED);
	if (!NRF_RADIO->EVENTS_DISABLED) return MULTIPROTO_REQ_CONTINUE;
	NRF_RADIO->EVENTS_DISABLED = 0;
power_off:
	NRF_RADIO->POWER = 0;
	LOG_WRN("powered off, ret false");
	return MULTIPROTO_REQ_END;
}
