/*
 * frsky_rx.h
 *
 *  Created on: Dec 29, 2020
 *      Author: user
 */

#ifndef INC_FRSKY_RX_H_
#define INC_FRSKY_RX_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	FRSKY_RX_TUNE_START,
	FRSKY_RX_TUNE_LOW,
	FRSKY_RX_TUNE_HIGH,
	FRSKY_RX_BIND,
	FRSKY_RX_DATA,
} frsky_phase_t;

void FrSky_init_vtab(void);
uint16_t FrSky_Rx_init(void);
uint16_t FrSky_Rx_callback(void);
void FrSky_Rx_Bind(void);
frsky_phase_t Frky_Rx_Phase(void);
uint8_t FrSky_Rx_Lqi(void);
bool FrSky_Rx_Channels(const uint16_t** pChannels);


#endif /* INC_FRSKY_RX_H_ */
