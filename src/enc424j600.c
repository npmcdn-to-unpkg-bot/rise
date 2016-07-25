//Copyright 2012 Charles Lohr under the MIT/x11, newBSD, LGPL or GPL licenses.  You choose.

#include "rise.h"
#include "enc424j600.h"
#include "enc424j600_regs.h"
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

uint8_t termcallbackearly;
uint16_t sendbaseaddress;

static void standarddelay()
{
	_delay_us(15);
}

// big endian
uint16_t enc424j600_pop16()
{
	uint16_t ret;
	ret = espiR();
	return espiR() | (ret << 8);
}

void enc424j600_push16( uint16_t v)
{
	espiW( v >> 8 );
	espiW( v & 0xFF );
}

//little endian
uint16_t enc424j600_pop16LE()
{
	uint16_t ret;
	ret = espiR();
	return (espiR() << 8) | ret;
}

void enc424j600_push16LE (uint16_t v)
{
	espiW(v & 0xFF);
	espiW((v >> 8) & 0xFF);
}


void enc424j600_popblob( uint8_t * data, uint8_t len )
{
	while (len--) {
		*(data++) = enc424j600_pop8();
	}
}


void enc424j600_pushstr( const char * msg )
{
	for( ; *msg; msg++ )
		enc424j600_push8( *msg );
}

void enc424j600_pushblob( const uint8_t * data, uint8_t len )
{
	while( len-- )
	{
		enc424j600_push8( *(data++) );
	}
}

void enc424j600_dumpbytes( uint8_t len )
{
	while( len-- )
	{
		enc424j600_pop8();
	}
}


static void cs_low(void)
{
	ETCSPORT &= ~ETCS;
}

static void cs_high(void)
{
	ETCSPORT |= ETCS;
}

static uint8_t do_spi = 0;
static uint8_t spi_write = 0;

static void spi_thread (void)
{
	if (do_spi != 0) {
		espiW(spi_write);
		do_spi = 0;
	}
}

static void spi_queue_write (uint8_t value) {
	do_spi = 1;
	spi_write = value;
}


void enc424j600_write_ctrl_reg16( uint8_t addy, uint16_t value )
{
	static uint8_t state = 0;

	while (1) {
		spi_thread();

		switch (state++) {
			case 0: cs_low(); break;
			case 1: spi_queue_write(EWCRU); break;
			case 2: spi_queue_write(addy); break;
			case 3: enc424j600_push16LE( value ); break;
			case 4: cs_high(); state = 0; return;
		}
	}
}

//This requires the proper bank to be selected.
static uint8_t enc_read_ctrl_reg8_common( uint8_t addy )
{
	uint8_t ret;
	cs_low();
	espiW( ERCR | addy );
	ret = espiR();
	cs_high();
	return ret;
}

uint16_t enc424j600_read_ctrl_reg16( uint8_t addy )
{
	uint16_t ret;
	cs_low();
	espiW( ERCRU );
	espiW( addy );
	ret = enc424j600_pop16LE();
	cs_high();
	return ret;
}

static void enc_oneshot( uint8_t cmd )
{
	cs_low();
	espiW( cmd );
	cs_high();
}

uint16_t NextPacketPointer = RX_BUFFER_START;

static void setup_spi_ports(void)
{
	ETPORT &= ~ETSCK;
	ETDDR &= ~( ETSO | ETINT );
	ETDDR |= ( ETSCK | ETSI	);
	ETCSDDR |= ETCS;
	cs_high();
}

int8_t enc424j600_init( const unsigned char * macaddy )
{
	unsigned char i = 0;
	setup_spi_ports();

	standarddelay();
	enc424j600_write_ctrl_reg16( EEUDASTL, 0x1234 );
	if (enc424j600_read_ctrl_reg16( EEUDASTL ) != 0x1234) {
		return -1;
	}

	//Instead of ECONing, we do it this way.
	enc_oneshot( ESSETETHRST );

	standarddelay();
	standarddelay();

	//the datasheet says to do this, but if I do, sometimes from a cold boot, it doesn't work.
//	if( enc424j600_read_ctrl_reg16( EEUDASTL ) )
//	{
//		return -2;
//	}

	enc424j600_write_ctrl_reg16( EECON2L, 0xCB00 );
	enc424j600_write_ctrl_reg16( EERXSTL, RX_BUFFER_START );
	enc424j600_write_ctrl_reg16( EMAMXFLL, MAX_FRAMELEN );
	//Must have RX tail here otherwise we will have difficulty moving our buffer along.
	enc424j600_write_ctrl_reg16( EERXTAILL, 0x5FFE );

	*((uint16_t*)(&MyMAC[0])) = enc424j600_read_ctrl_reg16( EMAADR1L );
	*((uint16_t*)(&MyMAC[2])) = enc424j600_read_ctrl_reg16( EMAADR2L );
	*((uint16_t*)(&MyMAC[4])) = enc424j600_read_ctrl_reg16( EMAADR3L );

	//Enable RX
	enc_oneshot( ESENABLERX );

	return 0;
}

void enc424j600_startsend (uint16_t baseaddress)
{
	//Start at beginning of scratch.
	sendbaseaddress = baseaddress;
	enc424j600_write_ctrl_reg16( EEGPWRPTL, baseaddress );
	cs_low();
	espiW( EWGPDATA );
	//Send away!
}

void enc424j600_endsend ()
{
	uint16_t i;
	uint16_t es;
	cs_high();
	es = enc424j600_read_ctrl_reg16(EEGPWRPTL) - sendbaseaddress;

	if (enc424j600_xmitpacket(sendbaseaddress, es)) {
		// ERROR: Send failure.
	}
}

uint16_t enc424j600_get_write_length ()
{
	return enc424j600_read_ctrl_reg16(EEGPWRPTL) + 6 - sendbaseaddress;
}

void enc424j600_wait_for_dma ()
{
	uint8_t i = 0;
	//wait for previous DMA operation to complete
	while ((enc_read_ctrl_reg8_common(EECON1L) & _BV(5)) && (i++ < 250)) {
		standarddelay(); //This can wait up to 3.75mS
	}
}

int8_t enc424j600_xmitpacket (uint16_t start, uint16_t len)
{
	uint8_t retries = 0;
	uint8_t i = 0;

	enc424j600_wait_for_dma();

	//Wait for previous packet to complete.
	//ECON1 is in the common banks, so we can get info on it more quickly.
	while (enc_read_ctrl_reg8_common( EECON1L ) & _BV(1)) {
		i++;
		if (i > 250) {
			// Error: XMIT Could not send.
			//Consider clearing the packet in transmission, here.
			//Done by clearing TXRTS
			return -1;
		}
		//_delay_us(1);
		standarddelay();
	}

	enc424j600_write_ctrl_reg16( EETXSTL, start );
	enc424j600_write_ctrl_reg16( EETXLENL, len );

	//Don't worry, by default this part inserts the padding and crc.
	enc_oneshot( ESSETTXRTS );

	enc424j600_wait_for_dma();


	return 0;
}

void enc424j600_finish_callback_now()
{
	unsigned short nextpos;
	cs_high();
	//XXX BUG: NextPacketPointer-2 may be less than in the RX area
	if( NextPacketPointer == RX_BUFFER_START )
		nextpos = 0x5FFE;  //XXX MAGIC NUMBER
	else
		nextpos = NextPacketPointer - 2;

	enc424j600_write_ctrl_reg16( EERXTAILL, nextpos );
	enc_oneshot( ESSETPKTDEC );

	termcallbackearly = 1;
}

void enc424j600_stopop()
{
	cs_high();
}

unsigned short enc424j600_recvpack()
{
	uint16_t receivedbytecount;
	enc_oneshot( ESENABLERX );
	termcallbackearly = 0;

	//if ERXTAIL == ERXHEAD this is BAD! (it thinks it's full)
	//ERXTAIL should be 2 less than ERXHEAD
	//Note: you don't read the two bytes immediately following ERXTAIL, they are dummy.
//	if( enc424j600_read_ctrl_reg16( EERXHEADL ) == enc424j600_read_ctrl_reg16( EERXTAILL ) )
//	{
//		//I Tried this, the code in here never got called.  I thought this might be the cause of the missing packets.
//	}


	//NextPacketPointer contains the start address of ERXST initially
	uint8_t estat = enc_read_ctrl_reg8_common( EESTATL );
	if( !estat )
	{
		return 0;
	}

	//Configure ERXDATA for reading.
	enc424j600_write_ctrl_reg16( EERXRDPTL, NextPacketPointer );

	//Start reading!!!
	cs_low();
	espiW( ERRXDATA );

	NextPacketPointer = enc424j600_pop16LE();

	//Read the status vector
	receivedbytecount = enc424j600_pop16LE();

	if (enc424j600_pop16LE() & _BV( 7 )) {
		enc424j600_pop16LE();
		//Good packet.

		//Dest mac.
		enc424j600_receivecallback( receivedbytecount );
	} else {
		// ERROR: Bad packet
		//I have never observed tis code getting called, even when I saw dropped packets.
	}

	if( !termcallbackearly )
	{
		enc424j600_finish_callback_now();
	}

	return 0;
}


//Start a checksum
void enc424j600_start_checksum( uint16_t start, uint16_t len )
{
	enc424j600_wait_for_dma();
	enc424j600_write_ctrl_reg16(EEDMASTL, start + sendbaseaddress);
	enc424j600_write_ctrl_reg16(EEDMALENL, len);
	enc_oneshot(ESDMACKSUM);
}

//Get the results of a checksum
uint16_t enc424j600_get_checksum()
{
	uint16_t ret;
	enc424j600_wait_for_dma();
	ret = enc424j600_read_ctrl_reg16(EEDMACSL);
	return (ret >> 8) | ((ret & 0xff) << 8);
}

//Modify a word of memory (based off of offset from start sending)
void enc424j600_alter_word (uint16_t address, uint16_t val)
{
	enc424j600_write_ctrl_reg16( EEUDAWRPTL, address + sendbaseaddress );
	cs_low();
	espiW(EWUDADATA);
	espiW((val >> 8) & 0xFF);
	espiW(val & 0xFF);
	cs_high();
}


void enc424j600_copy_memory( uint16_t to, uint16_t from, uint16_t length, uint16_t range_start, uint16_t range_end )
{
	enc424j600_wait_for_dma();
	enc424j600_write_ctrl_reg16(EEUDASTL, range_start);
	enc424j600_write_ctrl_reg16(EEUDANDL, range_end);

	enc424j600_write_ctrl_reg16(EEDMASTL, from);
	enc424j600_write_ctrl_reg16(EEDMADSTL, to);
	enc424j600_write_ctrl_reg16(EEDMALENL, length);
	enc_oneshot(ESDMACOPY); //XXX TODO: Should we be purposefully /not/ calculating checksum?  Does it even matter?
}
