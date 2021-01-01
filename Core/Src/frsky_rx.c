// FROM https://github.com/pascallanger/DIY-Multiprotocol-TX-Module
/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "iface_cc2500.h"
#include "Multiprotocol.h"
#include "eeprom.h"
#include "frsky_rx.h"

typedef uint16_t EE_ADDR;

enum {
	EE_RX_FORMAT = 0,
	EE_RX_TX_ADDR0,
	EE_RX_TX_ADDR1,
	EE_RX_FINETUNE,
	EE_FREQ_HOP,
};

uint16_t VirtAddVarTab[NumbOfVar];

void EE_Init_Vtab()
{
	for (uint8_t i = 0; i < NumbOfVar; ++i) {
		VirtAddVarTab[i] = i;
	}
}

static inline uint16_t eeprom_read_var(EE_ADDR addr) {
	uint16_t var = 0;
	EE_ReadVariable(addr, &var);
	return var;
}

static inline void eeprom_write_var(EE_ADDR addr, uint16_t var)
{
	EE_WriteVariable(addr, var);
}

#define FRSKY_RX_D16FCC_LENGTH	0x1D+1
#define FRSKY_RX_D16LBT_LENGTH	0x20+1
#define FRSKY_RX_D16v2_LENGTH	0x1D+1
#define FRSKY_RX_D8_LENGTH		0x11+1
#define FRSKY_RX_FORMATS		5

#define FRSKY_RX_EEPROM_OFFSET	178		// (1) format + (3) TX ID + (1) freq_tune + (47) channels, 52 bytes, end is 178+52=230

#define TELEMETRY_BUFFER_SIZE 32
static uint8_t packet_in[TELEMETRY_BUFFER_SIZE];//telemetry receiving packets
static uint8_t packet[50];
static uint8_t hopping_frequency[50];
static uint8_t calData[50];
static uint8_t rx_tx_addr[5];
static uint16_t rx_rc_chan[16];

static uint8_t packet_length;
static uint8_t packet_count;
static uint8_t hopping_frequency_no=0;
//static uint8_t protocol_flags=0;
static uint8_t telemetry_link=0;
static uint8_t phase;
static bool rx_data_started;
static uint8_t option;
static uint8_t prev_option;
//static uint8_t sub_protocol;
//static uint8_t protocol;
static uint16_t state;
//static uint8_t RX_num;
static uint8_t RX_RSSI;
static uint8_t RX_LQI;
static uint8_t FrSkyFormat=0;
static uint32_t pps_timer;
static uint16_t pps_counter;
//static uint8_t FrSkyX_chanskip;

static bool bind_in_progress = false;

void FrSky_Rx_Bind(void) {
	 bind_in_progress = true;
}

uint8_t Frky_Rx_Phase(void) {
	return phase;
}

uint8_t FrSky_Rx_Lqi(void) {
	return RX_LQI;
}

bool FrSky_Rx_Channels(const uint16_t** pChannels) {
	uint8_t oldTelem = telemetry_link;
	telemetry_link = 0;
	*pChannels = rx_rc_chan;
	return oldTelem;
}

static void FrSkyX2_init_hop(void);
static uint16_t FrSkyX_crc(uint8_t *data, uint8_t len, uint16_t init);

enum
{
	FRSKY_RX_D8			=0,
	FRSKY_RX_D16FCC		=1,
	FRSKY_RX_D16LBT		=2,
	FRSKY_RX_D16v2FCC	=3,
	FRSKY_RX_D16v2LBT	=4,
};

const uint8_t frsky_rx_common_reg[][2] = {
	{CC2500_02_IOCFG0, 0x01},
	{CC2500_18_MCSM0, 0x18},
	{CC2500_07_PKTCTRL1, 0x05},
	{CC2500_3E_PATABLE, 0xFF},
	{CC2500_0C_FSCTRL0, 0},
	{CC2500_0D_FREQ2, 0x5C},
	{CC2500_13_MDMCFG1, 0x23},
	{CC2500_14_MDMCFG0, 0x7A},
	{CC2500_19_FOCCFG, 0x16},
	{CC2500_1A_BSCFG, 0x6C},
	{CC2500_1B_AGCCTRL2, 0x03},
	{CC2500_1C_AGCCTRL1, 0x40},
	{CC2500_1D_AGCCTRL0, 0x91},
	{CC2500_21_FREND1, 0x56},
	{CC2500_22_FREND0, 0x10},
	{CC2500_23_FSCAL3, 0xA9},
	{CC2500_24_FSCAL2, 0x0A},
	{CC2500_25_FSCAL1, 0x00},
	{CC2500_26_FSCAL0, 0x11},
	{CC2500_29_FSTEST, 0x59},
	{CC2500_2C_TEST2, 0x88},
	{CC2500_2D_TEST1, 0x31},
	{CC2500_2E_TEST0, 0x0B},
	{CC2500_03_FIFOTHR, 0x07},
	{CC2500_09_ADDR, 0x03},
};

const uint8_t frsky_rx_d16fcc_reg[][2] = {
	{CC2500_17_MCSM1, 0x0C},
	{CC2500_0E_FREQ1, 0x76},
	{CC2500_0F_FREQ0, 0x27},
	{CC2500_06_PKTLEN, 0x1E},
	{CC2500_08_PKTCTRL0, 0x01},
	{CC2500_0B_FSCTRL1, 0x0A},
	{CC2500_10_MDMCFG4, 0x7B},
	{CC2500_11_MDMCFG3, 0x61},
	{CC2500_12_MDMCFG2, 0x13},
	{CC2500_15_DEVIATN, 0x51},
};

const uint8_t frsky_rx_d16lbt_reg[][2] = {
	{CC2500_17_MCSM1, 0x0E},
	{CC2500_0E_FREQ1, 0x80},
	{CC2500_0F_FREQ0, 0x00},
	{CC2500_06_PKTLEN, 0x23},
	{CC2500_08_PKTCTRL0, 0x01},
	{CC2500_0B_FSCTRL1, 0x08},
	{CC2500_10_MDMCFG4, 0x7B},
	{CC2500_11_MDMCFG3, 0xF8},
	{CC2500_12_MDMCFG2, 0x03},
	{CC2500_15_DEVIATN, 0x53},
};

const uint8_t frsky_rx_d8_reg[][2] = {
	{CC2500_17_MCSM1,    0x0C},
	{CC2500_0E_FREQ1,    0x76},
	{CC2500_0F_FREQ0,    0x27},
	{CC2500_06_PKTLEN,   0x19},
	{CC2500_08_PKTCTRL0, 0x05},
	{CC2500_0B_FSCTRL1,  0x08},
	{CC2500_10_MDMCFG4,  0xAA},
	{CC2500_11_MDMCFG3,  0x39},
	{CC2500_12_MDMCFG2,  0x11},
	{CC2500_15_DEVIATN,  0x42},
};

static uint8_t frsky_rx_chanskip;
static int8_t  frsky_rx_finetune;
static uint8_t frsky_rx_format = FRSKY_RX_D16v2FCC;

static void __attribute__((unused)) frsky_rx_strobe_rx()
{
	 CC2500_Strobe(CC2500_SIDLE);
	 CC2500_Strobe(CC2500_SFRX);
	 CC2500_Strobe(CC2500_SRX);
}

static void __attribute__((unused)) frsky_rx_initialise_cc2500() {
	const uint8_t frsky_rx_length[] = { FRSKY_RX_D8_LENGTH, FRSKY_RX_D16FCC_LENGTH, FRSKY_RX_D16LBT_LENGTH, FRSKY_RX_D16v2_LENGTH, FRSKY_RX_D16v2_LENGTH };
	packet_length = frsky_rx_length[frsky_rx_format];
	CC2500_Reset();
	CC2500_Strobe(CC2500_SIDLE);
	for (uint8_t i = 0; i < sizeof(frsky_rx_common_reg) / 2; i++)
		CC2500_WriteReg(frsky_rx_common_reg[i][0], frsky_rx_common_reg[i][1]);
	if (CC2500_ReadReg(CC2500_09_ADDR) != 0x3) {
		for(;;);
	}

	switch (frsky_rx_format)
	{
		case FRSKY_RX_D16v2FCC:
		case FRSKY_RX_D16FCC:
			for (uint8_t i = 0; i < sizeof(frsky_rx_d16fcc_reg) / 2; i++)
				CC2500_WriteReg(frsky_rx_d16fcc_reg[i][0], frsky_rx_d16fcc_reg[i][1]);
			if(frsky_rx_format==FRSKY_RX_D16v2FCC)
			{
				CC2500_WriteReg(CC2500_08_PKTCTRL0, 0x05);	// Enable CRC
				CC2500_WriteReg(CC2500_17_MCSM1, 0x0E);		// Go/Stay in RX mode
				CC2500_WriteReg(CC2500_11_MDMCFG3, 0x84);	// bitrate 70K->77K
			}
			break;
		case FRSKY_RX_D16v2LBT:
		case FRSKY_RX_D16LBT:
			for (uint8_t i = 0; i < sizeof(frsky_rx_d16lbt_reg) / 2; i++)
				CC2500_WriteReg(frsky_rx_d16lbt_reg[i][0], frsky_rx_d16lbt_reg[i][1]);
			if(frsky_rx_format==FRSKY_RX_D16v2LBT)
				CC2500_WriteReg(CC2500_08_PKTCTRL0, 0x05);	// Enable CRC
			break;
		case FRSKY_RX_D8:
			for (uint8_t i = 0; i < sizeof(frsky_rx_d8_reg) / 2; i++)
				CC2500_WriteReg(frsky_rx_d8_reg[i][0], frsky_rx_d8_reg[i][1]);
			CC2500_WriteReg(CC2500_23_FSCAL3, 0x89);
			break;
	}
	CC2500_WriteReg(CC2500_0A_CHANNR, 0);  // bind channel
	CC2500_SetTxRxMode(RX_EN); // lna disable / enable
	frsky_rx_strobe_rx();
	HAL_Delay(1); // wait for RX to activate
}

static void __attribute__((unused)) frsky_rx_set_channel(uint8_t channel)
{
	CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency[channel]);
	if(frsky_rx_format == FRSKY_RX_D8)
		CC2500_WriteReg(CC2500_23_FSCAL3, 0x89);
	CC2500_WriteReg(CC2500_25_FSCAL1, calData[channel]);
	frsky_rx_strobe_rx();
}

static void __attribute__((unused)) frsky_rx_calibrate()
{
	frsky_rx_strobe_rx();
	for (unsigned c = 0; c < 47; c++)
	{
		CC2500_Strobe(CC2500_SIDLE);
		CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency[c]);
		CC2500_Strobe(CC2500_SCAL);
		HAL_Delay(1);//(900us);
		calData[c] = CC2500_ReadReg(CC2500_25_FSCAL1);
	}
}

static uint8_t __attribute__((unused)) frskyx_rx_check_crc_id(bool bind,bool init)
{
	/*debugln("RX");
	for(uint8_t i=0; i<packet_length;i++)
		debug(" %02X",packet[i]);
	debugln("");*/

	if(bind && packet[0]!=packet_length-1 && packet[1] !=0x03 && packet[2] != 0x01)
		return false;
	uint8_t offset=bind?3:1;

	// Check D8 checksum
	if (frsky_rx_format == FRSKY_RX_D8)
	{
		if((packet[packet_length+1] & 0x80) != 0x80)	// Check CRC_OK flag in status byte 2
			return false; 								// Bad CRC
		if(init)
		{//Save TXID
			rx_tx_addr[3] = packet[3];
			rx_tx_addr[2] = packet[4];
			rx_tx_addr[1] = packet[17];
		}
		else
			if(rx_tx_addr[3] != packet[offset] || rx_tx_addr[2] != packet[offset+1] || rx_tx_addr[1] != packet[bind?17:5])
				return false;							// Bad address
		return true;									// Full match
	}

	// Check D16v2 checksum
	if (frsky_rx_format == FRSKY_RX_D16v2LBT || frsky_rx_format == FRSKY_RX_D16v2FCC)
		if((packet[packet_length+1] & 0x80) != 0x80)	// Check CRC_OK flag in status byte 2
			return false;
	//debugln("HW Checksum ok");

	// Check D16 checksum
	uint16_t lcrc = FrSkyX_crc(&packet[3], packet_length - 5, 0);		// Compute crc
	uint16_t rcrc = (packet[packet_length-2] << 8) | (packet[packet_length-1] & 0xff);	// Received crc
	if(lcrc != rcrc)
		return false; 									// Bad CRC
	//debugln("Checksum ok");

	if (bind && (frsky_rx_format == FRSKY_RX_D16v2LBT || frsky_rx_format == FRSKY_RX_D16v2FCC))
		for(uint8_t i=3; i<packet_length-2; i++)		//unXOR bind packet
			packet[i] ^= 0xA7;

	uint8_t offset2=0;
	if (bind && (frsky_rx_format == FRSKY_RX_D16LBT || frsky_rx_format == FRSKY_RX_D16FCC))
		offset2=6;
	if(init)
	{//Save TXID
		rx_tx_addr[3] = packet[3];
		rx_tx_addr[2] = packet[4];
		rx_tx_addr[1] = packet[5+offset2];
		rx_tx_addr[0] = packet[6+offset2];				// RXnum
	}
	else
		if(rx_tx_addr[3] != packet[offset] || rx_tx_addr[2] != packet[offset+1] || rx_tx_addr[1] != packet[offset+2+offset2])
			return false;								// Bad address
	//debugln("Address ok");

	if(!bind && rx_tx_addr[0] != packet[6])
		return false;									// Bad RX num

	//debugln("Match");
	return true;										// Full match
}

static void __attribute__((unused)) frsky_rx_build_telemetry_packet()
{
	uint16_t raw_channel[8];
	uint32_t bits = 0;
	uint8_t bitsavailable = 0;
	uint8_t idx = 0;
	uint8_t i;

	if (frsky_rx_format == FRSKY_RX_D8)
	{// decode D8 channels
		raw_channel[0] = ((packet[10] & 0x0F) << 8 | packet[6]);
		raw_channel[1] = ((packet[10] & 0xF0) << 4 | packet[7]);
		raw_channel[2] = ((packet[11] & 0x0F) << 8 | packet[8]);
		raw_channel[3] = ((packet[11] & 0xF0) << 4 | packet[9]);
		raw_channel[4] = ((packet[16] & 0x0F) << 8 | packet[12]);
		raw_channel[5] = ((packet[16] & 0xF0) << 4 | packet[13]);
		raw_channel[6] = ((packet[17] & 0x0F) << 8 | packet[14]);
		raw_channel[7] = ((packet[17] & 0xF0) << 4 | packet[15]);
		for (i = 0; i < 8; i++) {
			if (raw_channel[i] < 1290)
				raw_channel[i] = 1290;
			uint16_t tmp = ((raw_channel[i] - 1290) << 4) / 15;
			if (tmp > 2047) {
				tmp = 2047;
			}
			rx_rc_chan[i] = tmp;
		}
	}
	else
	{// decode D16 channels
		raw_channel[0] = ((packet[10] << 8) & 0xF00) | packet[9];
		raw_channel[1] = ((packet[11] << 4) & 0xFF0) | (packet[10] >> 4);
		raw_channel[2] = ((packet[13] << 8) & 0xF00) | packet[12];
		raw_channel[3] = ((packet[14] << 4) & 0xFF0) | (packet[13] >> 4);
		raw_channel[4] = ((packet[16] << 8) & 0xF00) | packet[15];
		raw_channel[5] = ((packet[17] << 4) & 0xFF0) | (packet[16] >> 4);
		raw_channel[6] = ((packet[19] << 8) & 0xF00) | packet[18];
		raw_channel[7] = ((packet[20] << 4) & 0xFF0) | (packet[19] >> 4);
		for (i = 0; i < 8; i++) {
			// ignore failsafe channels
			if(packet[7] != 0x10+(i<<1)) {
				uint8_t shifted = (raw_channel[i] & 0x800)>0;
				uint16_t channel_value = raw_channel[i] & 0x7FF;
				if (channel_value < 64)
					rx_rc_chan[shifted ? i + 8 : i] = 0;
				else {
					uint16_t tmp = ((channel_value - 64) << 4) / 15;
					if (tmp > 2047) {
						tmp = 2047;
					}
					rx_rc_chan[shifted ? i + 8 : i] = tmp;
				}
			}
		}
	}

	// buid telemetry packet
	packet_in[idx++] = RX_LQI;
	packet_in[idx++] = RX_RSSI;
	packet_in[idx++] = 0;  // start channel
	packet_in[idx++] = frsky_rx_format == FRSKY_RX_D8 ? 8 : 16; // number of channels in packet

	// pack channels
	for (i = 0; i < packet_in[3]; i++) {
		bits |= ((uint32_t)rx_rc_chan[i]) << bitsavailable;
		bitsavailable += 11;
		while (bitsavailable >= 8) {
			packet_in[idx++] = bits & 0xff;
			bits >>= 8;
			bitsavailable -= 8;
		}
	}
}

static void __attribute__((unused)) frsky_rx_data()
{
	frsky_rx_format = eeprom_read_var(EE_RX_FORMAT) % FRSKY_RX_FORMATS;
	((uint16_t*)&rx_tx_addr)[0] = eeprom_read_var(EE_RX_TX_ADDR0);
	((uint16_t*)&rx_tx_addr)[1] = eeprom_read_var(EE_RX_TX_ADDR1);
	frsky_rx_finetune = eeprom_read_var(EE_RX_FINETUNE);
	debug("format=%d, ", frsky_rx_format);
	debug("addr[3]=%02X, ", rx_tx_addr[3]);
	debug("addr[2]=%02X, ", rx_tx_addr[2]);
	debug("addr[1]=%02X, ", rx_tx_addr[1]);
	debug("rx_num=%02X, ",  rx_tx_addr[0]);
	debugln("tune=%d", (int8_t)frsky_rx_finetune);
	if(frsky_rx_format != FRSKY_RX_D16v2LBT && frsky_rx_format != FRSKY_RX_D16v2FCC)
	{//D8 & D16v1
		for (uint8_t ch = 0; ch < 47; ch += 2) {
			((uint16_t*)hopping_frequency)[ch/2] = eeprom_read_var(EE_FREQ_HOP + ch / 2);
		}
	}
	else
	{
		FrSkyFormat=frsky_rx_format == FRSKY_RX_D16v2FCC?0:2;
		FrSkyX2_init_hop();
	}
	debug("ch:");
	for (uint8_t ch = 0; ch < 47; ch++)
		debug(" %02X", hopping_frequency[ch]);
	debugln("");

	frsky_rx_initialise_cc2500();
	frsky_rx_calibrate();
	CC2500_WriteReg(CC2500_18_MCSM0, 0x08); // FS_AUTOCAL = manual
	CC2500_WriteReg(CC2500_09_ADDR, rx_tx_addr[3]); // set address
	CC2500_WriteReg(CC2500_07_PKTCTRL1, 0x05); // check address
	if (option == 0)
		CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
	else
		CC2500_WriteReg(CC2500_0C_FSCTRL0, option);
	frsky_rx_set_channel(hopping_frequency_no);
	phase = FRSKY_RX_DATA;
}

uint16_t FrSky_Rx_init(void)
{
	frsky_rx_chanskip = 1;
	hopping_frequency_no = 0;
	rx_data_started = false;
	frsky_rx_finetune = 0;
	telemetry_link = 0;
	packet_count = 0;
	if (bind_in_progress)
	{
		frsky_rx_format = FRSKY_RX_D8;
		frsky_rx_initialise_cc2500();
		phase = FRSKY_RX_TUNE_START;
		debugln("FRSKY_RX_TUNE_START");
	}
	else
		frsky_rx_data();
	return 1000;
}

uint16_t FrSky_Rx_callback(void)
{
	static int8_t read_retry = 0;
	static int8_t tune_low, tune_high;
	uint8_t len, ch;

	if (!bind_in_progress && phase != FRSKY_RX_DATA) {
		return FrSky_Rx_init();	// Abort bind
	}

	if ((prev_option != option) && (phase >= FRSKY_RX_DATA))
	{
		if (option == 0)
			CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
		else
			CC2500_WriteReg(CC2500_0C_FSCTRL0, option);
		prev_option = option;
	}

#if 0
	if (rx_disable_lna != IS_POWER_FLAG_on)
	{
		rx_disable_lna = IS_POWER_FLAG_on;
		CC2500_SetTxRxMode(rx_disable_lna ? TXRX_OFF : RX_EN);
	}
#endif

	len = CC2500_ReadReg(CC2500_3B_RXBYTES | CC2500_READ_BURST) & 0x7F;
	switch(phase)
	{
		case FRSKY_RX_TUNE_START:
			if (len == packet_length + 2) //+2=RSSI+LQI+CRC
			{
				CC2500_ReadData(packet, len);
				if(frskyx_rx_check_crc_id(true,true))
				{
					frsky_rx_finetune = -127;
					CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
					phase = FRSKY_RX_TUNE_LOW;
					debugln("FRSKY_RX_TUNE_LOW");
					frsky_rx_strobe_rx();
					state = 0;
					return 1000;
				}
			}
			frsky_rx_format = (frsky_rx_format + 1) % FRSKY_RX_FORMATS; // switch to next format (D8, D16FCC, D16LBT, D16v2FCC, D16v2LBT)
			frsky_rx_initialise_cc2500();
			frsky_rx_finetune += 10;
			CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
			frsky_rx_strobe_rx();
			return 18000;

		case FRSKY_RX_TUNE_LOW:
			if (len == packet_length + 2) //+2=RSSI+LQI+CRC
			{
				CC2500_ReadData(packet, len);
				if(frskyx_rx_check_crc_id(true,false)) {
					tune_low = frsky_rx_finetune;
					frsky_rx_finetune = 127;
					CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
					phase = FRSKY_RX_TUNE_HIGH;
					debugln("FRSKY_RX_TUNE_HIGH");
					frsky_rx_strobe_rx();
					return 1000;
				}
			}
			frsky_rx_finetune += 1;
			CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
			frsky_rx_strobe_rx();
			return 18000;

		case FRSKY_RX_TUNE_HIGH:
			if (len == packet_length + 2) //+2=RSSI+LQI+CRC
			{
				CC2500_ReadData(packet, len);
				if(frskyx_rx_check_crc_id(true,false)) {
					tune_high = frsky_rx_finetune;
					frsky_rx_finetune = (tune_low + tune_high) / 2;
					CC2500_WriteReg(CC2500_0C_FSCTRL0, (int8_t)frsky_rx_finetune);
					if(tune_low < tune_high)
					{
						phase = FRSKY_RX_BIND;
						debugln("FRSKY_RX_TUNE_HIGH");
					}
					else
					{
						phase = FRSKY_RX_TUNE_START;
						debugln("FRSKY_RX_TUNE_START");
					}
					frsky_rx_strobe_rx();
					return 1000;
				}
			}
			frsky_rx_finetune -= 1;
			CC2500_WriteReg(CC2500_0C_FSCTRL0, frsky_rx_finetune);
			frsky_rx_strobe_rx();
			return 18000;

		case FRSKY_RX_BIND:
			if (len == packet_length + 2) //+2=RSSI+LQI+CRC
			{
				CC2500_ReadData(packet, len);
				if (frskyx_rx_check_crc_id(true,false)) {
					if (frsky_rx_format != FRSKY_RX_D16v2LBT && frsky_rx_format != FRSKY_RX_D16v2FCC)
					{// D8 & D16v1
						if (packet[5] <= 0x2D)
						{
							for (ch = 0; ch < 5; ch++)
								hopping_frequency[packet[5]+ch] = packet[6+ch];
							state |= 1 << (packet[5] / 5);
						}
					}
					else
						state = 0x3FF; //No hop table for D16v2
					if (state == 0x3FF)
					{
						debugln("Bind complete");
						bind_in_progress = false;
						// store format, finetune setting, txid, channel list
						eeprom_write_var(EE_RX_FORMAT, frsky_rx_format);
						eeprom_write_var(EE_RX_TX_ADDR0, ((uint16_t*)rx_tx_addr)[0]);
						eeprom_write_var(EE_RX_TX_ADDR1, ((uint16_t*)rx_tx_addr)[1]);
						eeprom_write_var(EE_RX_FINETUNE, frsky_rx_finetune);
						if(frsky_rx_format != FRSKY_RX_D16v2FCC && frsky_rx_format != FRSKY_RX_D16v2LBT) {
							for (ch = 0; ch < 47; ch += 2) {
								eeprom_write_var(EE_FREQ_HOP + ch / 2, ((uint16_t*)hopping_frequency)[ch / 2]);
							}
						}
						frsky_rx_data();
						debugln("FRSKY_RX_DATA");
					}
				}
				frsky_rx_strobe_rx();
			}
			return 1000;

		case FRSKY_RX_DATA:
			if (len == packet_length + 2) //+2=RSSI+LQI+CRC
			{
				CC2500_ReadData(packet, len);
				if (frskyx_rx_check_crc_id(false,false))
				{
					RX_RSSI = packet[len-2];
					if(RX_RSSI >= 128)
						RX_RSSI -= 128;
					else
						RX_RSSI += 128;
					bool chanskip_valid=true;
					// hop to next channel
					if (frsky_rx_format != FRSKY_RX_D8)
					{//D16v1 & D16v2
						if (rx_data_started)
						{
							if (frsky_rx_chanskip != (((packet[4] & 0xC0) >> 6) | ((packet[5] & 0x3F) << 2)))
							{
								chanskip_valid=false;	// chanskip value has changed which surely indicates a bad frame
								packet_count++;
								if(packet_count>5)		// the TX must have changed chanskip...
									frsky_rx_chanskip = ((packet[4] & 0xC0) >> 6) | ((packet[5] & 0x3F) << 2);	// chanskip init
							}
							else
								packet_count=0;
						}
						else
							frsky_rx_chanskip = ((packet[4] & 0xC0) >> 6) | ((packet[5] & 0x3F) << 2);	// chanskip init
					}
					hopping_frequency_no = (hopping_frequency_no + frsky_rx_chanskip) % 47;
					frsky_rx_set_channel(hopping_frequency_no);
					if (chanskip_valid)
					{
						if (telemetry_link == 0)
						{ // send channels to TX
							frsky_rx_build_telemetry_packet();
							telemetry_link = 1;
						}
						pps_counter++;
					}
					rx_data_started = true;
					read_retry = 0;
				}
			}

			// packets per second
			if (HAL_GetTick() - pps_timer >= 1000) {
				pps_timer = HAL_GetTick();
				debugln("%d pps", pps_counter);
				RX_LQI = pps_counter;
				if(pps_counter==0)	// no packets for 1 sec or more...
				{// restart the search
					rx_data_started=false;
					packet_count=0;
				}
				pps_counter = 0;
			}

			// skip channel if no packet received in time
			if (read_retry++ >= 9) {
				hopping_frequency_no = (hopping_frequency_no + frsky_rx_chanskip) % 47;
				frsky_rx_set_channel(hopping_frequency_no);
				if(rx_data_started)
					read_retry = 0;
				else
					read_retry = -50; // retry longer until first packet is catched
			}
			break;
	}
	return 1000;
}

/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */

/******************************/
/**  FrSky D and X routines  **/
/******************************/

//**CRC**
static const uint16_t FrSkyX_CRC_Short[]={
	0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
	0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7 };
static uint16_t __attribute__((unused)) FrSkyX_CRCTable(uint8_t val)
{
	uint16_t word = FrSkyX_CRC_Short[val & 0x0F];
	val /= 16 ;
	return word ^ (0x1081 * val) ;
}
static uint16_t FrSkyX_crc(uint8_t *data, uint8_t len, uint16_t init)
{
	uint16_t crc = init;
	for(uint8_t i=0; i < len; i++)
		crc = (crc<<8) ^ FrSkyX_CRCTable((uint8_t)(crc>>8) ^ *data++);
	return crc;
}

enum {
	FRSKY_BIND		= 0,
	FRSKY_BIND_DONE	= 1000,
	FRSKY_DATA1,
	FRSKY_DATA2,
	FRSKY_DATA3,
	FRSKY_DATA4,
	FRSKY_DATA5,
};

void Frsky_init_hop(void)
{
	uint8_t val;
	uint8_t channel = rx_tx_addr[0]&0x07;
	uint8_t channel_spacing = rx_tx_addr[1];
	//Filter bad tables
	if(channel_spacing<0x02) channel_spacing+=0x02;
	if(channel_spacing>0xE9) channel_spacing-=0xE7;
	if(channel_spacing%0x2F==0) channel_spacing++;

	hopping_frequency[0]=channel;
	for(uint8_t i=1;i<50;i++)
	{
		channel=(channel+channel_spacing) % 0xEB;
		val=channel;
		if((val==0x00) || (val==0x5A) || (val==0xDC))
			val++;
		hopping_frequency[i]=i>46?0:val;
	}
}

static void FrSkyX2_init_hop(void)
{
	uint16_t id=(rx_tx_addr[2]<<8) + rx_tx_addr[3];
	//Increment
	uint8_t inc = (id % 46) + 1;
	if( inc == 12 || inc ==35 ) inc++;							//Exception list from dumps
	//Start offset
	uint8_t offset = id % 5;

	debug("hop: ");
	uint8_t channel;
	for(uint8_t i=0; i<47; i++)
	{
		channel = 5 * ((uint16_t)(inc * i) % 47) + offset;
		//Exception list from dumps
		if(FrSkyFormat & 2 )// LBT or FCC
		{//LBT
			if( channel <=1 || channel == 43 || channel == 44 || channel == 87 || channel == 88 || channel == 129 || channel == 130 || channel == 173 || channel == 174)
				channel += 2;
			else if( channel == 216 || channel == 217 || channel == 218)
				channel += 3;
		}
		else //FCC
			if ( channel == 3 || channel == 4 || channel == 46 || channel == 47 || channel == 90 || channel == 91  || channel == 133 || channel == 134 || channel == 176 || channel == 177 || channel == 220 || channel == 221 )
				channel += 2;
		//Store
		hopping_frequency[i] = channel;
		debug(" %02X",channel);
	}
	debugln("");
	hopping_frequency[47] = 0;									//Bind freq
}

