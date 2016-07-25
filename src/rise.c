#include "rise.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include "ip.h"
#include "enc424j600.h"
#include <avr/pgmspace.h>
#include <string.h>

#define NOOP asm volatile("nop" ::)

unsigned char MyIP[4] = { 169, 254, 0, 144 };
unsigned char MyMask[4] = { 255, 255, 255, 0 };
unsigned char MyMAC[6];

int8_t pingslot;
uint8_t iptoping[4] = { 169, 254, 0, 143 };

void SetupPing()
{
	pingslot = GetPingslot( iptoping );
}

void DoPingCode()
{
	int8_t r;

	// sendchr( 0 );
	// sendhex4( ClientPingEntries[pingslot].last_recv_seqnum );
	// sendchr( '/' );
	// sendhex4( ClientPingEntries[pingslot].last_send_seqnum );
	// sendchr( '\n' );

	r = RequestARP( iptoping );

//	if( r < 0 )
//		return;

	DoPing( pingslot );

}

char* packet = "[MOT6]\n";

void SendUDP (void)
{
	const char * sending;

	enc424j600_stopop();
	enc424j600_startsend( 0 );
	send_etherlink_header( 0x0800 );
//	send_ip_header( 0, "\xE0\x00\x02\x3c", 17 ); //UDP Packet to 224.0.2.60
	send_ip_header( 0, iptoping, 17 ); //UDP Packet to 255.255.255.255

	enc424j600_push16( 1222 );
	enc424j600_push16( 3333 );
	enc424j600_push16( 0 ); //length for later
	enc424j600_push16( 0 ); //csum for later

	enc424j600_pushstr( packet );

	util_finish_udp_packet();
}

void HandleUDP (uint16_t len)
{
	enc424j600_pop16(); //Checksum
	len -= 8; //remove header.

	//You could pop things, or check ports, etc. here.
	iptoping[0] = ipsource[0];
	iptoping[1] = ipsource[1];
	iptoping[2] = ipsource[2];
	iptoping[3] = ipsource[3];

	if (len > 0) {
		packet[0] = enc424j600_pop8();
	}
	if (len > 1) {
		packet[1] = enc424j600_pop8();
	}

	return;
}

static void setup_clock (void)
{
	/*Examine Page 33*/
	CLKPR = 0x80;	/*Setup CLKPCE to be receptive*/
	CLKPR = 0x00;	/*No scalar*/
//	OSCCAL=0xff;
}

static void timer_config (void)
{
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
}

static int timer_tick(void)
{
	static uint8_t tick = 0;

	if( TIFR2 & _BV(TOV2) )
	{
		TIFR2 |= _BV(TOV2);
		tick++;

		if( tick == 100 ) {
			tick = 0;
			return 1;
		}
	}
	return 0;
}

int main (void)
{
	cli();
	// setup_spi();
	setup_clock();
	timer_config();
	SetupPing();
	sei();

	if (enc424j600_init(MyMAC)) {
		// Failure
		return -1;
	}

	while(1) {
		unsigned short r;

		while (enc424j600_recvpack()) {
			continue; //may be more
		}

		if (timer_tick()) {
			// DoPingCode();
			SendUDP();
		}
	}

	return 0;
}
