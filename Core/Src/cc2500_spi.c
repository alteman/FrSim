
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
//-------------------------------
//-------------------------------
//CC2500 SPI routines
//-------------------------------
//-------------------------------
#include <stdint.h>
#include <string.h>
#include "iface_cc2500.h"
#include "main.h"
#include "stm32f1xx_hal.h"

//// CC2500
//	#define CC25_CSN_pin	7								//D7 = PD7
//	#define CC25_CSN_port	PORTD
//	#define CC25_CSN_ddr	DDRD
//	#define CC25_CSN_output	CC25_CSN_ddr  |=  _BV(CC25_CSN_pin)
//	#define CC25_CSN_on		CC25_CSN_port |=  _BV(CC25_CSN_pin)
//	#define CC25_CSN_off	CC25_CSN_port &= ~_BV(CC25_CSN_pin)

#define hCCSpi hspi1

// All transactions should take <1ms
#define SPI_TIMEOUT 2

static inline void cs_en()
{
	HAL_GPIO_WritePin(CC2500_CS_GPIO_Port, CC2500_CS_Pin, GPIO_PIN_RESET);
}

static inline void cs_dis()
{
	HAL_GPIO_WritePin(CC2500_CS_GPIO_Port, CC2500_CS_Pin, GPIO_PIN_SET);
}

//----------------------------
void CC2500_WriteReg(uint8_t address, uint8_t data)
{
	uint8_t buf[2] = {address, data};
	cs_en();
	HAL_SPI_Transmit(&hCCSpi, buf, sizeof(buf), SPI_TIMEOUT);
	cs_dis();
}

//----------------------
static void CC2500_ReadRegisterMulti(uint8_t address, uint8_t data[], uint8_t length)
{
#define MAX_BURST_LEN 36 //(FRSKY_RX_D16LBT_LENGTH+2) = 33 + 2 + 1
	uint8_t spibuf[MAX_BURST_LEN];
	spibuf[0] = CC2500_READ_BURST | address;
	if (length + 1 > sizeof(spibuf)) {
		// Max read length out of bounds
		for(;;);
	}
	cs_en();
	HAL_SPI_TransmitReceive(&hCCSpi, spibuf, spibuf, length + 1, SPI_TIMEOUT);
	//SPI_Write(CC2500_READ_BURST | address);
	cs_dis();
	memcpy(data, spibuf + 1, length);
}

//--------------------------------------------
uint8_t CC2500_ReadReg(uint8_t address)
{
	uint8_t spibuf[MAX_BURST_LEN] = {CC2500_READ_SINGLE | address};
	cs_en();
	HAL_SPI_TransmitReceive(&hCCSpi, spibuf, spibuf, sizeof(spibuf), SPI_TIMEOUT);
	cs_dis();
	return spibuf[1];
}

//------------------------
void CC2500_ReadData(uint8_t *dpbuffer, uint8_t len)
{
	CC2500_ReadRegisterMulti(CC2500_3F_RXFIFO, dpbuffer, len);
}

//*********************************************
void CC2500_Strobe(uint8_t state)
{
	cs_en();
	HAL_SPI_Transmit(&hCCSpi, &state, sizeof(state), SPI_TIMEOUT);
	cs_dis();
}

//static void CC2500_WriteRegisterMulti(uint8_t address, const uint8_t data[], uint8_t length)
//{
//	cs_en();
//	SPI_Write(CC2500_WRITE_BURST | address);
//	for(uint8_t i = 0; i < length; i++)
//		SPI_Write(data[i]);
//	cs_dis();
//}

//void CC2500_WriteData(uint8_t *dpbuffer, uint8_t len)
//{
//	CC2500_Strobe(CC2500_SFTX);
//	CC2500_WriteRegisterMulti(CC2500_3F_TXFIFO, dpbuffer, len);
//	CC2500_Strobe(CC2500_STX);
//}

void CC2500_SetTxRxMode(uint8_t mode)
{
	if(mode == TX_EN)
	{//from deviation firmware
		CC2500_WriteReg(CC2500_00_IOCFG2, 0x2F);
		CC2500_WriteReg(CC2500_02_IOCFG0, 0x2F | 0x40);
	}
	else
		if (mode == RX_EN)
		{
			CC2500_WriteReg(CC2500_02_IOCFG0, 0x2F);
			CC2500_WriteReg(CC2500_00_IOCFG2, 0x2F | 0x40);
		}
		else
		{
			CC2500_WriteReg(CC2500_02_IOCFG0, 0x2F);
			CC2500_WriteReg(CC2500_00_IOCFG2, 0x2F);
		}
}

//------------------------
/*static void cc2500_resetChip(void)
{
	// Toggle chip select signal
	CC25_CSN_on;
	delayMicroseconds(30);
	CC25_CSN_off;
	delayMicroseconds(30);
	CC25_CSN_on;
	delayMicroseconds(45);
	CC2500_Strobe(CC2500_SRES);
	_delay_ms(100);
}
*/
uint8_t CC2500_Reset()
{
	CC2500_Strobe(CC2500_SRES);
	HAL_Delay(1);
	CC2500_SetTxRxMode(TXRX_OFF);
	return CC2500_ReadReg(CC2500_0E_FREQ1) == 0xC4;//check if reset
}
/*
static void CC2500_SetPower_Value(uint8_t power)
{
	const unsigned char patable[8]=	{
		0xC5,  // -12dbm
		0x97, // -10dbm
		0x6E, // -8dbm
		0x7F, // -6dbm
		0xA9, // -4dbm
		0xBB, // -2dbm
		0xFE, // 0dbm
		0xFF // 1.5dbm
	};
	if (power > 7)
		power = 7;
	CC2500_WriteReg(CC2500_3E_PATABLE,  patable[power]);
}
*/
//void CC2500_SetPower()
//{
//	uint8_t power=CC2500_BIND_POWER;
//	if(IS_BIND_DONE)
//		power=CC2500_HIGH_POWER;
//	if(IS_RANGE_FLAG_on)
//		power=CC2500_RANGE_POWER;
//	if(prev_power != power)
//	{
//		CC2500_WriteReg(CC2500_3E_PATABLE, power);
//		prev_power=power;
//	}
//}
