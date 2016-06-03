#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "avr_print.h"
#include <stdio.h>
#include "iparpetc.h"
#include "enc424j600.h"
#include <basicfat.h>
#include <util10.h>
#include <avr/pgmspace.h>
#include <string.h>

#define NOOP asm volatile("nop" ::)


static void setup_clock( void )
{
	/*Examine Page 33*/

	CLKPR = 0x80;	/*Setup CLKPCE to be receptive*/
	CLKPR = 0x00;	/*No scalar*/

//	OSCCAL=0xff;
}

unsigned short frameno;
unsigned char cc;

#define POP enc424j600_pop8()
#define POP16 enc424j600_pop16()
#define PUSH(x) enc424j600_push8(x)
#define PUSH16(x) enc424j600_push16(x)

unsigned char MyIP[4] = { 192, 168, 0, 142 };
unsigned char MyMask[4] = { 255, 255, 255, 0 };
unsigned char MyMAC[6];

int8_t pingslot;
uint8_t iptoping[4] = { 169, 254, 44, 216 };

void SetupPing()
{
	pingslot = GetPingslot( iptoping );
}

void DoPingCode()
{
	int8_t r;

	sendchr( 0 );
	sendhex4( ClientPingEntries[pingslot].last_recv_seqnum );
	sendchr( '/' );
	sendhex4( ClientPingEntries[pingslot].last_send_seqnum );
	sendchr( '\n' );

	r = RequestARP( iptoping );

//	if( r < 0 )
//		return;

	DoPing( pingslot );

}

void SendAnnounce( )
{
	const char * sending;

	enc424j600_stopop();
	enc424j600_startsend( 0 );
	send_etherlink_header( 0x0800 );
//	send_ip_header( 0, "\xE0\x00\x02\x3c", 17 ); //UDP Packet to 224.0.2.60
	send_ip_header( 0, "\xa9\xfe\x2c\xd8", 17 ); //UDP Packet to 255.255.255.255

	enc424j600_push16( 1222 );
	enc424j600_push16( 3333 );
	enc424j600_push16( 0 ); //length for later
	enc424j600_push16( 0 ); //csum for later

	enc424j600_pushpgmstr( PSTR( "[MOTD]\n" ) );

	util_finish_udp_packet();
}

void HandleUDP( uint16_t len )
{
	POP16; //Checksum
	len -= 8; //remove header.

	//You could pop things, or check ports, etc. here.

	return;
}

int main( void )
{
	uint8_t tick;

	//Input the interrupt.
	// DDRD &= ~_BV(2);
	cli();
	setup_spi();
	sendstr( "HELLO\n" );
	setup_clock();

	//Configure T2 to "overflow" at 100 Hz, this lets us run the TCP clock
	TCCR2A = _BV(WGM21) | _BV(WGM20);
	TCCR2B = _BV(WGM22) | _BV(CS22) | _BV(CS21) | _BV(CS20);
	//T2 operates on clkIO, fast PWM.  Fast PWM's TOP is OCR2A
	#define T2CNT  ((F_CPU/1024)/100)
	#if( T2CNT > 254 )
	#undef T2CNT
	#define T2CNT 254
	#endif
	OCR2A = T2CNT;

	SetupPing();

	sei();

//	InitTCP();
	// DDRD &= 0xff;
	if( enc424j600_init( MyMAC ) )
	{
		sendstr( "Failure.\n" );
		return -1;
	}
	sendstr( "OK.\n" );

	while(1)
	{
		unsigned short r;

		r = enc424j600_recvpack( );
		if( r ) continue; //may be more

		if( TIFR2 & _BV(TOV2) )
		{
			TIFR2 |= _BV(TOV2);
			tick++;

			if( tick == 255 )
			{
				// DoPingCode();
				SendAnnounce();
				tick = 0;
			}
		}
	}

	return 0;
} 




















