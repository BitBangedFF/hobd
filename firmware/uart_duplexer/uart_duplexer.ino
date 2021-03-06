/**
 * @file uart_duplexer.ino
 * @brief UART Duplexer.
 *
 * Arduino version: 1.6.12
 *
 * Libraries:
 * \li SoftwareSerialWithHalfDuplex
 *
 */




#include <SoftwareSerialWithHalfDuplex.h>




// *****************************************************
// static global types/macros
// *****************************************************

// ms
#define STARTUP_DELAY (2000UL)


//
#define UART_BAUDRATE (115200UL)
#define OBD_BAUDRATE (10400UL)


//
#define PIN_TX (0)
#define PIN_RX (1)


//
#define PIN_KLINE (12)

#define PIN_LED (13)



static bool led_state = false;


// *****************************************************
// static global data
// *****************************************************

//
static SoftwareSerialWithHalfDuplex obd_serial(
        PIN_KLINE,
        PIN_KLINE,
        false,
        false);




// *****************************************************
// static declarations
// *****************************************************




// *****************************************************
// static definitions
// *****************************************************




// *****************************************************
// public definitions
// *****************************************************

//
void setup( void )
{
    pinMode( PIN_RX, INPUT );
    pinMode( PIN_TX, INPUT );
    pinMode( PIN_LED, OUTPUT );
    digitalWrite( PIN_LED, led_state );

    delay( STARTUP_DELAY );

    Serial.begin( UART_BAUDRATE );

    obd_serial.begin( OBD_BAUDRATE );
}


//
void loop( void )
{
    if( obd_serial.available() != 0 )
    {
        const byte rx_byte = (byte) obd_serial.read();

        Serial.write( rx_byte );

        led_state = !led_state;
        digitalWrite( PIN_LED, led_state );
    }
}
