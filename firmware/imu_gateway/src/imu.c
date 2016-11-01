/**
 * @file imu.c
 * @brief TODO.
 *
 */




#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <math.h>

#include "board.h"
#include "xbusmessage.h"
#include "xbusparser.h"
#include "uart_lib.h"
#include "uart_drv.h"
#include "hobd.h"
#include "ring_buffer.h"
#include "time.h"
#include "canbus.h"
#include "diagnostics.h"
#include "imu.h"




// *****************************************************
// static global types/macros
// *****************************************************

//
#define XBUS_BUFFER_SIZE (512)

// IMU is on UART0
#define UART_RX_INTERRUPT USART0_RX_vect
#define UART_UCSRA UCSR0A
#define UART_UCSRB UCSR0B
#define UART_UCSRC UCSR0C
#define UART_DATA UDR0


//
#define imu_uart_enable() (UART_UCSRB |= (_BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0)))


//
#define imu_uart_disable() (UART_UCSRB &= ~(_BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0)))




// *****************************************************
// static global data
// *****************************************************

// UART rx ring buffer
static volatile ring_buffer_s rx_buffer;


// Xbus parser state
static struct XbusParser xbus_parser;


// Xbux static buffer
static uint8_t xbus_buffer[ XBUS_BUFFER_SIZE ];


// IMU message/data state
static imu_data_s imu_data;




// *****************************************************
// static declarations
// *****************************************************

//
static void hw_init( void );




// *****************************************************
// static definitions
// *****************************************************

//
ISR( UART_RX_INTERRUPT )
{
    // read UART status register and UART data register
    const uint8_t status  = UART_UCSRA;
    const uint8_t data = UART_DATA;

    // read error status
    rx_buffer.error = (status & (_BV(FE0) | _BV(DOR0)) );

    // push data into the rx buffer, error is updated with return status
    (void) ring_buffer_putc(
            data,
            &rx_buffer );
}


//
static void hw_init( void )
{
    Uart_select( IMU_UART );

    Uart_clear();

    Uart_set_ubrr( IMU_BAUDRATE );

    Uart_hw_init( CONF_8BIT_NOPAR_1STOP );

    imu_uart_enable();
}


//
static uint8_t process_buffer( void )
{
    uint8_t ret = 0;

    const uint16_t data = ring_buffer_getc( &rx_buffer );

    if( data != RING_BUFFER_NO_DATA )
    {
        XbusParser_parseByte( &xbus_parser, (uint8_t) (data & 0xFF) );
    }

    return ret;
}


//
static void *xbus_alloc_cb( size_t size )
{
    if( size > sizeof(xbus_buffer) )
    {
        diagnostics_set_error( HOBD_HEARTBEAT_ERROR_IMU_RX_OVERFLOW );
    }

    return (void*) &xbus_buffer[0];
}


//
static void xbus_free_cb( void const * buffer )
{
    // do nothing to our static buffer
}


//
static uint8_t publish_group_d( void )
{
    uint8_t ret = 0;

    ret |= canbus_send(
            HOBD_CAN_ID_IMU_ORIENT_QUAT1,
            (uint8_t) sizeof(imu_data.group_d.orient_quat1),
            (const uint8_t *) &imu_data.group_d.orient_quat1 );

    ret |= canbus_send(
            HOBD_CAN_ID_IMU_ORIENT_QUAT2,
            (uint8_t) sizeof(imu_data.group_d.orient_quat2),
            (const uint8_t *) &imu_data.group_d.orient_quat2 );

    return ret;
}


//
static uint8_t publish_group_e( void )
{
    uint8_t ret = 0;

    ret |= canbus_send(
            HOBD_CAN_ID_IMU_RATE_OF_TURN1,
            (uint8_t) sizeof(imu_data.group_e.rate_of_turn1),
            (const uint8_t *) &imu_data.group_e.rate_of_turn1 );

    ret |= canbus_send(
            HOBD_CAN_ID_IMU_RATE_OF_TURN2,
            (uint8_t) sizeof(imu_data.group_e.rate_of_turn2),
            (const uint8_t *) &imu_data.group_e.rate_of_turn2 );

    return ret;
}


//
static void parse_sample_time_fine(
        const struct XbusMessage * const message,
        const uint32_t * const rx_timestamp )
{
    uint32_t sample_time;

    const uint8_t status = XbusMessage_getDataItem(
            &sample_time,
            XDI_SampleTimeFine,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_sample_time_fine\n" );

        imu_data.group_a.sample_time.rx_time = *rx_timestamp;
        imu_data.group_a.sample_time.sample_time = sample_time;

        imu_set_group_ready( IMU_GROUP_A_READY );
    }
}


//
static void parse_utc_time(
        const struct XbusMessage * const message,
        const uint32_t * const rx_timestamp )
{
    XsUtcTime utc_time;

    const uint8_t status = XbusMessage_getDataItem(
            &utc_time,
            XDI_UtcTime,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_utc_time\n" );

        imu_data.group_c.utc_time1.rx_time = *rx_timestamp;
        imu_data.group_c.utc_time1.flags = utc_time.flags;
        imu_data.group_c.utc_time1.year = utc_time.year;
        imu_data.group_c.utc_time1.month = utc_time.month;
        imu_data.group_c.utc_time2.day = utc_time.day;
        imu_data.group_c.utc_time2.hour = utc_time.hour;
        imu_data.group_c.utc_time2.min = utc_time.min;
        imu_data.group_c.utc_time2.sec = utc_time.sec;
        imu_data.group_c.utc_time2.nanosec = utc_time.nanosec;

        imu_set_group_ready( IMU_GROUP_C_READY );
    }
}


//
static void parse_orient_quat(
        const struct XbusMessage * const message )
{
    float quat[4];

    const uint8_t status = XbusMessage_getDataItem(
            quat,
            XDI_Quaternion,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_orient_quat\n" );

        imu_data.group_d.orient_quat1.q1 = quat[ 0 ];
        imu_data.group_d.orient_quat1.q2 = quat[ 1 ];
        imu_data.group_d.orient_quat2.q3 = quat[ 2 ];
        imu_data.group_d.orient_quat2.q4 = quat[ 3 ];

        imu_set_group_ready( IMU_GROUP_D_READY );
    }
}


//
static void parse_rate_of_turn(
        const struct XbusMessage * const message )
{
    float gryo[3];

    const uint8_t status = XbusMessage_getDataItem(
            gryo,
            XDI_RateOfTurn,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_rate_of_turn\n" );

        imu_data.group_e.rate_of_turn1.x = gryo[ 0 ];
        imu_data.group_e.rate_of_turn1.y = gryo[ 1 ];
        imu_data.group_e.rate_of_turn2.z = gryo[ 2 ];

        imu_set_group_ready( IMU_GROUP_E_READY );
    }
}


//
static void parse_accel(
        const struct XbusMessage * const message )
{
#warning "USING FreeAcceleration instead of Acceleration"
    float accel[3];

    const uint8_t status = XbusMessage_getDataItem(
            accel,
            XDI_FreeAcceleration,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_accel\n" );

        imu_data.group_f.accel1.x = accel[ 0 ];
        imu_data.group_f.accel1.y = accel[ 1 ];
        imu_data.group_f.accel2.z = accel[ 2 ];

        imu_set_group_ready( IMU_GROUP_F_READY );
    }
}


//
static void parse_magf(
        const struct XbusMessage * const message )
{
    float magf[3];

    const uint8_t status = XbusMessage_getDataItem(
            magf,
            XDI_MagneticField,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_magf\n" );

        imu_data.group_g.magf1.x = magf[ 0 ];
        imu_data.group_g.magf1.y = magf[ 1 ];
        imu_data.group_g.magf2.z = magf[ 2 ];

        imu_set_group_ready( IMU_GROUP_G_READY );
    }
}


//
static void parse_pos_ll(
        const struct XbusMessage * const message )
{
    float lat_lon[2];

    const uint8_t status = XbusMessage_getDataItem(
            lat_lon,
            XDI_LatLon,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_pos_ll\n" );

        imu_data.group_h.pos_llh1.latitude = lat_lon[ 0 ];
        imu_data.group_h.pos_llh1.longitude = lat_lon[ 1 ];

        imu_set_group_ready( IMU_GROUP_H_READY );
    }
}


//
static void parse_pos_h(
        const struct XbusMessage * const message )
{
    float height;

    const uint8_t status = XbusMessage_getDataItem(
            &height,
            XDI_AltitudeEllipsoid,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_pos_h\n" );

        imu_data.group_i.pos_llh2.height = height;

        imu_set_group_ready( IMU_GROUP_I_READY );
    }
}


//
static void parse_vel_ned(
        const struct XbusMessage * const message )
{
    float vel[3];

    const uint8_t status = XbusMessage_getDataItem(
            vel,
            XDI_VelocityXYZ,
            message );

    if( status != 0 )
    {
        DEBUG_PUTS( "imu_vel_ned\n" );

        imu_data.group_j.vel_ned1.north = vel[ 0 ];
        imu_data.group_j.vel_ned1.east = vel[ 1 ];
        imu_data.group_j.vel_ned2.down = vel[ 2 ];

        imu_set_group_ready( IMU_GROUP_J_READY );
    }
}


//
static void handle_message_cb( struct XbusMessage const * message )
{
    const uint32_t rx_timestamp = time_get_ms();

    if( message->length > (uint16_t) sizeof(xbus_buffer) )
    {
        diagnostics_set_error( HOBD_HEARTBEAT_ERROR_IMU_RX_OVERFLOW );
    }
    else if( message->data != NULL )
    {
        // TODO
        #warning "TODO - message handler - group B time data"

        parse_sample_time_fine(
                (const struct XbusMessage *) message,
                &rx_timestamp );

        parse_orient_quat( (const struct XbusMessage *) message );

        parse_rate_of_turn( (const struct XbusMessage *) message );

        parse_accel( (const struct XbusMessage *) message );

        parse_magf( (const struct XbusMessage *) message );

        parse_pos_ll( (const struct XbusMessage *) message );

        parse_pos_h( (const struct XbusMessage *) message );

        parse_vel_ned( (const struct XbusMessage *) message );
    }
}




// *****************************************************
// public definitions
// *****************************************************

//
uint8_t imu_init( void )
{
    uint8_t ret = 0;

    ring_buffer_init( &rx_buffer );

    const struct XbusParserCallback xbus_callbacks =
    {
        .allocateBuffer = &xbus_alloc_cb,
        .deallocateBuffer = &xbus_free_cb,
        .handleMessage = &handle_message_cb
    };

    (void) XbusParser_init(
            (void*) &xbus_parser,
            &xbus_callbacks );

    hw_init();

    // clear all ready groups
    imu_clear_all_group_ready();

    // flush rx buffer
    ring_buffer_flush( &rx_buffer );

    return ret;
}


//
void imu_disable( void )
{
    // disable UART
    imu_uart_disable();

    // flush rx buffer
    ring_buffer_flush( &rx_buffer );
}


//
void imu_enable( void )
{
    // flush rx buffer
    ring_buffer_flush( &rx_buffer );

    // enable UART
    imu_uart_enable();
}


//
void imu_set_group_ready(
        const uint16_t group )
{
    imu_data.ready_groups |= group;
}


//
void imu_clear_group_ready(
        const uint16_t group )
{
    imu_data.ready_groups &= ~group;
}


//
void imu_clear_all_group_ready( void )
{
    imu_data.ready_groups = IMU_GROUP_NONE_READY;
}


//
uint8_t imu_is_group_ready(
        const uint16_t group )
{
    return ((imu_data.ready_groups & group) == 0) ? 0 : 1;
}


//
uint8_t imu_update( void )
{
    uint8_t ret = 0;

    // process any available data in the rx buffer, callbacks are called from
    // this context
    ret = process_buffer();

    // check for any ready groups
    if( imu_data.ready_groups != IMU_GROUP_NONE_READY )
    {
#warning "TODO - handle groups"
        // handle groups in order/priority
//        if( imu_is_group_ready( IMU_GROUP_A_READY ) != 0 )
//        {
//            ret |= publish_group_a();
//            imu_clear_group_ready( IMU_GROUP_A_READY );
//        }

        if( imu_is_group_ready( IMU_GROUP_D_READY ) != 0 )
        {
            ret |= publish_group_d();
            imu_clear_group_ready( IMU_GROUP_D_READY );
        }

        if( imu_is_group_ready( IMU_GROUP_E_READY ) != 0 )
        {
            ret |= publish_group_e();
            imu_clear_group_ready( IMU_GROUP_E_READY );
        }
    }

    return ret;
}
