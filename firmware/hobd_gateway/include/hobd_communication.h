/**
 * @file hobd_communication.h
 * @brief TODO.
 *
 */




#ifndef HOBD_COMMUNICATION_H
#define	HOBD_COMMUNICATION_H




#include <stdint.h>
#include "hobd.h"




//
uint8_t hobdcomm_init( void );


//
uint8_t hobdcomm_send( void );


//
uint8_t hobdcomm_recv( void );


// TODO - checksum function




#endif	/* HOBD_COMMUNICATION_H */