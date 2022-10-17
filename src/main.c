/*
 * Copyright (c) 2020 Nordic Semiconductor
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "nrf.h"

#if 1
#include "SEGGER_RTT.h"
#else
#define SEGGER_RTT_printf(...) do { } while (0)
#endif

void POWER_CLOCK_IRQHandler() {
	NRF_CLOCK->INTENCLR = 0xFFFFFFFF;
}

void TEMP_IRQHandler() {
	NRF_TEMP->INTENCLR = 0xFFFFFFFF;
}

void RTC0_IRQHandler() {
	NRF_RTC0->INTENCLR = 0xFFFFFFFF;
}

void ADC_IRQHandler() {
	NRF_ADC->INTENCLR = 0xFFFFFFFF;
}

void RADIO_IRQHandler() {
	NRF_RADIO->INTENCLR = 0xFFFFFFFF;
}


static void delay(int t) {
	NRF_RTC0->TASKS_CLEAR = 1;
	NRF_RTC0->CC[0] = t;
	NRF_RTC0->INTENSET = RTC_INTENSET_COMPARE0_Msk;
	NRF_RTC0->TASKS_START = 1;
	while (!NRF_RTC0->EVENTS_COMPARE[0]) __WFE();
	NRF_RTC0->EVENTS_COMPARE[0] = 0;
	NRF_RTC0->TASKS_STOP = 1;
}

static void delay_ms(int ms) {
	delay(ms * 1024 / 125);
}

__attribute__((aligned(4)))
static uint8_t packet[256];

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	int16_t temp;
	int16_t voltage;
} OutputPacket;

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	uint16_t _reserved;
	uint16_t flags;
} InputPacket;

static OutputPacket *const output_packet = (OutputPacket*)&packet[0];
static InputPacket *const input_packet = (InputPacket*)&packet[0];

static const int REPORT_INTERVAL_MS = 5 /* 60 */* 1000;

static const uint32_t FREQUENCY = 2400;
static const uint32_t BASE_ADDR = 0x63e0;
static const uint32_t PREFIX_BYTE_ADDR = 0x17;
static const uint32_t CRC_POLY = 0x864CFB; // CRC-24-Radix-64 (OpenPGP)
static const uint32_t RETRY_DELAY_MS = 1000;
static const uint16_t INPUT_FLAG_ACK = 0x8000;

static const int FAILED_COUNT_ACCEPTABLE = 2;
static const int FAILED_COUNT_INCREASE_POWER = 3;
static const int FAILED_COUNT_FULL_POWER = 4;
static const int FAILED_COUNT_GIVE_UP = 5;
static const int ACCEPTABLE_COUNT_TO_POWER_DECREASE = 100;


static const uint8_t power_levels[] = {
	RADIO_TXPOWER_TXPOWER_Neg30dBm,
	RADIO_TXPOWER_TXPOWER_Neg20dBm,
	RADIO_TXPOWER_TXPOWER_Neg16dBm,
	RADIO_TXPOWER_TXPOWER_Neg12dBm,
	RADIO_TXPOWER_TXPOWER_Neg8dBm,
	RADIO_TXPOWER_TXPOWER_Neg4dBm,
	RADIO_TXPOWER_TXPOWER_0dBm,
	RADIO_TXPOWER_TXPOWER_Pos4dBm,
};

static const char *const power_levels_str[] = {
	"-30 dBm",
	"-20 dBm",
	"-16 dBm",
	"-12 dBm",
	"-8 dBm",
	"-4 dBm",
	"0 dBm",
	"+4 dBm",
};

static const int POWER_LEVEL_MAX = sizeof(power_levels) / sizeof(power_levels[0]) - 1;
static int power_level = 0;
static const int RX_TIMEOUT_MAX = 10 * 1024/125;
static int rx_timeout = RX_TIMEOUT_MAX;

static void radio_start() {
	NRF_RADIO->POWER = 1;
	NRF_RADIO->PACKETPTR = (uint32_t)&packet[0];
	NRF_RADIO->FREQUENCY = FREQUENCY - 2400;
	NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_250Kbit;
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
}

static void radio_stop()
{
	NRF_RADIO->POWER = 0;
}

static bool exchange_packets(int16_t temp, int16_t voltage)
{
	// Setup output packet
	output_packet->address_low = NRF_FICR->DEVICEADDR[0];
	output_packet->address_high = NRF_FICR->DEVICEADDR[1];
	output_packet->temp = temp;
	output_packet->voltage = voltage;
	__DMB();

	SEGGER_RTT_printf(0, "Sending packet %d/100\xB0""C, %dmV, %s...\n", temp, (int)voltage * 10, power_levels_str[power_level]);

	// Setup packet transmission
	NRF_RADIO->TXPOWER = power_levels[power_level];
	NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk | RADIO_SHORTS_DISABLED_RXEN_Msk;
	NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
	NRF_RADIO->TASKS_TXEN = 1;
	while (!NRF_RADIO->EVENTS_DISABLED) __WFE();
	NRF_RADIO->EVENTS_DISABLED = 0;
	NRF_RADIO->EVENTS_END = 0;

	SEGGER_RTT_printf(0, "Packet send. Receiving with timeout...\n");

	// Setup packet receiving
	NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
	NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;

	// Set timeout
	NRF_RTC0->TASKS_CLEAR = 1;
	NRF_RTC0->CC[0] = rx_timeout;
	NRF_RTC0->INTENSET = RTC_INTENSET_COMPARE0_Msk;
	NRF_RTC0->TASKS_START = 1;

	// Wait for end of packet receive or timeout
	while (!NRF_RTC0->EVENTS_COMPARE[0] && !NRF_RADIO->EVENTS_DISABLED) __WFE();

	// Stop timer
	NRF_RTC0->TASKS_STOP = 1;
	NRF_RTC0->EVENTS_COMPARE[0] = 0;
	NRF_RTC0->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;

	// Handle timeout
	if (!NRF_RADIO->EVENTS_DISABLED) {
		SEGGER_RTT_printf(0, "Timeout occured. Disabling radio...\n");
		// Disable radio
		NRF_RADIO->TASKS_DISABLE = 1;
		while (!NRF_RADIO->EVENTS_DISABLED) __WFE();
	}
	NRF_RADIO->EVENTS_DISABLED = 0;

	if (!NRF_RADIO->EVENTS_END) {
		SEGGER_RTT_printf(0, "No packet\n");
		rx_timeout += rx_timeout / 2;
		if (rx_timeout > RX_TIMEOUT_MAX) {
			rx_timeout = RX_TIMEOUT_MAX;
		} else {
			SEGGER_RTT_printf(0, "Increasing timeout to %dus\n", rx_timeout);
		}
		return false;
	}
	NRF_RADIO->EVENTS_END = 0;

	int receive_time = NRF_RTC0->COUNTER;

	SEGGER_RTT_printf(0, "Packet received after %d (%dus)\n", receive_time, receive_time * 15625/128);

	if (input_packet->address_low != NRF_FICR->DEVICEADDR[0] ||
		input_packet->address_high != (uint16_t)NRF_FICR->DEVICEADDR[1] ||
		!(input_packet->flags & INPUT_FLAG_ACK) ||
		(NRF_RADIO->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) != RADIO_CRCSTATUS_CRCSTATUS_CRCOk ||
		(NRF_RADIO->RXMATCH & RADIO_RXMATCH_RXMATCH_Msk) != 0)
	{
		SEGGER_RTT_printf(0, "Invalid packet\n");
		return false;
	}

	int new_timeout = 1 + receive_time + (receive_time + 2) / 3;
	if (new_timeout < 2) {
		new_timeout = 2;
	}
	if (new_timeout != rx_timeout) {
		SEGGER_RTT_printf(0, "Timeout adjusted to %d (%dus)\n", new_timeout, new_timeout * 15625/128);
	}
	rx_timeout = new_timeout;

	return true;
}


static void communicate(int16_t temp, int16_t voltage) {
	static int acceptable_count = 0;
	int failed_count = 0;
	static int rand_delay_index = 0;
	radio_start();
	while (true) {
		
		if (exchange_packets(temp, voltage)) {
			if (failed_count <= FAILED_COUNT_ACCEPTABLE && power_level > 0) {
				acceptable_count++;
				if (acceptable_count >= ACCEPTABLE_COUNT_TO_POWER_DECREASE) {
					SEGGER_RTT_printf(0, "Decreasing power level.\n");
					power_level--;
					acceptable_count = 0;
				} else {
					SEGGER_RTT_printf(0, "Consequtive acceptable transactions %d of %d.\n",
						acceptable_count, ACCEPTABLE_COUNT_TO_POWER_DECREASE);
				}
			}
			break;
		}
		failed_count++;

		if (failed_count > FAILED_COUNT_ACCEPTABLE) {
			acceptable_count = 0;
		}
		
		if (failed_count == FAILED_COUNT_INCREASE_POWER && power_level < POWER_LEVEL_MAX) {
			SEGGER_RTT_printf(0, "Increasing power level.\n");
			power_level++;
		} else if (failed_count == FAILED_COUNT_FULL_POWER && power_level < POWER_LEVEL_MAX) {
			SEGGER_RTT_printf(0, "Setting maximum power level.\n");
			power_level = POWER_LEVEL_MAX;
		} else if (failed_count >= FAILED_COUNT_GIVE_UP) {
			SEGGER_RTT_printf(0, "Communication failed.\n");
			break;
		}
		int delay_time = RETRY_DELAY_MS;
		delay_time += 100 * ((packet[rand_delay_index >> 3] >> (rand_delay_index & 7)) & 3);
		delay_time += 200 * (failed_count - 1);
		rand_delay_index += 2;
		if (rand_delay_index >= 48) {
			rand_delay_index = 0;
		}
		SEGGER_RTT_printf(0, "Packet exchange failed. Retry after %dms\n", delay_time * 100);
		delay_ms(delay_time);
	}
	radio_stop();
}


static void recv() {
	radio_start();

	NRF_RADIO->TXPOWER = power_levels[POWER_LEVEL_MAX];

	while (1) {
		SEGGER_RTT_printf(0, "Enable RX\n");
		NRF_RADIO->EVENTS_END = 0;
		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
		NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
		NRF_RADIO->TASKS_RXEN = 1;
		while (1) {
			while (!NRF_RADIO->EVENTS_END) __WFE();
			NRF_RADIO->EVENTS_END = 0;

			if ((NRF_RADIO->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) != RADIO_CRCSTATUS_CRCSTATUS_CRCOk ||
				(NRF_RADIO->RXMATCH & RADIO_RXMATCH_RXMATCH_Msk) != 0)
			{
				NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
				NRF_RADIO->TASKS_START = 1;
				SEGGER_RTT_printf(0, "Invalid packet received\n");
				continue;
			} else {
				break;
			}
		}
		SEGGER_RTT_printf(0, "Received\n");

		SEGGER_RTT_printf(0, "Packet from %04X%08X\n", output_packet->address_high, output_packet->address_low);
		int t = output_packet->temp;
		SEGGER_RTT_printf(0, "Temperature: %d.%d%d\xB0""C\n", t / 100, (t / 10) % 10, t % 10);
		int v = output_packet->voltage;
		SEGGER_RTT_printf(0, "Voltage: %d.%d%dV\n", v / 100, (v / 10) % 10, v % 10);

		SEGGER_RTT_printf(0, "Disable RX\n");
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->TASKS_DISABLE = 1;
		while (!NRF_RADIO->EVENTS_DISABLED) __WFE();
		NRF_RADIO->EVENTS_DISABLED = 0;

		input_packet->_reserved = 0;
		input_packet->flags = INPUT_FLAG_ACK;
		__DMB();

		SEGGER_RTT_printf(0, "Wait for remote switch\n");
		delay(1);

		SEGGER_RTT_printf(0, "Sending response\n");
		NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->TASKS_TXEN = 1;
		while (!NRF_RADIO->EVENTS_DISABLED) __WFE();
	}

}


int main()
{
	NRF_POWER->RAMON = POWER_RAMON_ONRAM0_RAM0On;
	NRF_POWER->RAMONB = 0;
	NVIC_EnableIRQ(POWER_CLOCK_IRQn);
	NVIC_SetPriority(POWER_CLOCK_IRQn, 0);
//#	if defined(BUILD_MODE_DONGLE)
		NVIC_EnableIRQ(TEMP_IRQn);
		NVIC_SetPriority(TEMP_IRQn, 0);
		NVIC_EnableIRQ(RTC0_IRQn);
		NVIC_SetPriority(RTC0_IRQn, 0);
		NVIC_EnableIRQ(ADC_IRQn);
		NVIC_SetPriority(ADC_IRQn, 0);
		NRF_RTC0->PRESCALER = 3;
		NRF_RTC0->EVTENSET = RTC_EVTENSET_COMPARE0_Msk;
		NRF_ADC->CONFIG = 
			(ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
			(ADC_CONFIG_INPSEL_SupplyOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
			(ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos);
//#	endif
	NVIC_EnableIRQ(RADIO_IRQn);
	NVIC_SetPriority(RADIO_IRQn, 0);

	__enable_irq();

	SEGGER_RTT_printf(0, "Setting up the clock\n");

	NRF_CLOCK->INTENSET = CLOCK_INTENSET_HFCLKSTARTED_Msk;
	NRF_CLOCK->TASKS_HFCLKSTART = 1;
	while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) __WFE();
	NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;

	NRF_CLOCK->INTENSET = CLOCK_INTENSET_LFCLKSTARTED_Msk;
	NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_Xtal;
	NRF_CLOCK->TASKS_LFCLKSTART = 1;
	while (!NRF_CLOCK->EVENTS_LFCLKSTARTED) __WFE();
	NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;

	SEGGER_RTT_printf(0, "DONE\n");

#	if defined(BUILD_MODE_DONGLE)

	while(1)
	{
		SEGGER_RTT_printf(0, "Measuring temp\n");
		NRF_TEMP->INTENSET = TEMP_INTENSET_DATARDY_Msk;
		NRF_TEMP->TASKS_START = 1;
		while (!NRF_TEMP->EVENTS_DATARDY) __WFE();
		NRF_TEMP->EVENTS_DATARDY = 0;
		int t = NRF_TEMP->TEMP * 25;
		SEGGER_RTT_printf(0, "Temperature: %d.%d%d\xB0""C\n", t / 100, (t / 10) % 10, t % 10);

		SEGGER_RTT_printf(0, "Measuring battery\n");
		NRF_ADC->ENABLE = 1;
		NRF_ADC->INTENSET = ADC_INTENSET_END_Msk;
		NRF_ADC->TASKS_START = 1;
		while (!NRF_ADC->EVENTS_END) __WFE();
		NRF_ADC->EVENTS_END = 0;
		NRF_ADC->ENABLE = 0;
		int v = NRF_ADC->RESULT * 45 / 128;
		SEGGER_RTT_printf(0, "Voltage: %d.%d%dV\n", v / 100, (v / 10) % 10, v % 10);

		communicate(t, v);

		SEGGER_RTT_printf(0, "Delay %dms\n", REPORT_INTERVAL_MS);

		while (!(NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_STATE_Msk)) {
			delay(2);
		}
		NRF_CLOCK->TASKS_HFCLKSTOP = 1;

		delay_ms(REPORT_INTERVAL_MS);

		NRF_CLOCK->INTENSET = CLOCK_INTENSET_HFCLKSTARTED_Msk;
		NRF_CLOCK->TASKS_HFCLKSTART = 1;
		while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) __WFE();
		NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
	}
#	else

	while (1) {
		recv();
	}

#	endif
}

