//Copyright 2012 Charles Lohr under the MIT/x11, newBSD, LGPL or GPL licenses.  You choose.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "avr_print.h"
#include <stdio.h>
#include "iparpetc.h"
#include "enc424j600.h"
#include <alloca.h>
#include <string.h>
#include <avr/pgmspace.h>

unsigned long icmp_in = 0;
unsigned long icmp_out = 0;

unsigned char macfrom[6];
unsigned char ipsource[4];
unsigned short remoteport;
unsigned short localport;
static unsigned short iptotallen;

#define SIZEOFICMP 28


uint16_t NetGetScratch()
{
	static uint8_t scratch = 0;
	scratch++;
	if( scratch == TX_SCRATCHES ) scratch = 0;
	return scratch * MAX_FRAMELEN;
}


void send_etherlink_header( unsigned short type )
{
	enc424j600_pushblob( macfrom, 6 );

// The mac does this for us.
	enc424j600_pushblob( MyMAC, 6 );

	enc424j600_push16( type );
}

void send_ip_header( unsigned short totallen, const unsigned char * to, unsigned char proto )
{
/*
	//This uses about 50 bytes less of flash, but 12 bytes more of ram.  You can choose that tradeoff.
	static unsigned char data[] = { 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 64, 0x00, 0x00, 0x00 };
	data[2] = totallen >> 8;
	data[3] = totallen & 0xFF;
	data[9] = proto;
	enc424j600_pushblob( data, 12 );
*/

	enc424j600_push16( 0x4500 );
	enc424j600_push16( totallen );

	enc424j600_push16( 0x0000 ); //ID
	enc424j600_push16( 0x4000 ); //Don't frgment, no fragment offset

	enc424j600_push8( 64 ); // TTL
	enc424j600_push8( proto ); // protocol

	enc424j600_push16( 0 ); //Checksum

	enc424j600_pushblob( MyIP, 4 );
	enc424j600_pushblob( to, 4 );
}


static void HandleICMP()
{
	unsigned short id;
	unsigned short seqnum;
	unsigned char type;
	unsigned short payloadsize = iptotallen - SIZEOFICMP;

	unsigned short payload_from_start, payload_dest_start;

	icmp_in++;

	type = enc424j600_pop8();
	enc424j600_pop8(); //code
	enc424j600_pop16(); //Checksum

	switch( type )
	{
		// Ping Response
		case 0: {
			uint16_t id;

			id = enc424j600_pop16();
			if( id < PING_RESPONSES_SIZE )
			{
				ClientPingEntries[id].last_recv_seqnum = enc424j600_pop16(); //Seqnum
			}
			enc424j600_stopop();

			enc424j600_finish_callback_now();
			break;
		}

		// Ping Request
		case 8: {
			//Tricky: We would ordinarily POPB to read out the payload, but we're using
			//the DMA engine to copy that data.
			//	POPB( payload, payloadsize );
			//Suspend reading for now (but don't allow over-writing of the data)
			enc424j600_stopop();
			payload_from_start = enc424j600_read_ctrl_reg16( EERXRDPTL );

			enc424j600_startsend( NetGetScratch() );
			send_etherlink_header( 0x0800 );
			send_ip_header( iptotallen, ipsource, 0x01 );

			enc424j600_push16( 0 ); //ping reply + code
			enc424j600_push16( 0 ); //Checksum
		//	enc424j600_push16( id );
		//	enc424j600_push16( seqnum );

			//Packet confiugred.  Need to copy payload.
			//Ordinarily, we'd enc424j600_pushblob for the payload, but we're currently using the DMA engine for our work here.
			enc424j600_stopop();
			payload_dest_start = enc424j600_read_ctrl_reg16( EEGPWRPTL );

			//+4 = id + seqnum (we're DMAing that, too)
			enc424j600_copy_memory( payload_dest_start, payload_from_start, payloadsize + 4, RX_BUFFER_START, RX_BUFFER_END-1 );  
			enc424j600_write_ctrl_reg16( EEGPWRPTL, payload_dest_start + payloadsize + 4 );

			enc424j600_finish_callback_now();

			//Calculate header and ICMP checksums
			enc424j600_start_checksum( 8+6, 20 );
			unsigned short ppl = enc424j600_get_checksum();
			enc424j600_start_checksum( 28+6, payloadsize + 8 );
			enc424j600_alter_word( 18+6, ppl );
			ppl = enc424j600_get_checksum();
			enc424j600_alter_word( 30+6, ppl );

			enc424j600_endsend();
			icmp_out++;

			break;
		}
	}
}


static void HandleArp (void)
{
	unsigned short i;
	unsigned char sendermac_ip_and_targetmac[16];
//	unsigned char senderip[10]; //Actually sender ip + target mac, put in one to shrink code.

	unsigned short proto;
	unsigned char opcode;
//	unsigned char ipproto;

	enc424j600_pop16(); //Hardware type
	proto = enc424j600_pop16();
	enc424j600_pop16(); //hwsize, protosize
	opcode = enc424j600_pop16();  //XXX: This includes "code" as well, it seems.

	switch( opcode )
	{
		// ARP Request
		case 1: {
			unsigned char match;

			enc424j600_popblob( sendermac_ip_and_targetmac, 16 );

			match = 1;
	//sendhex2( 0xff );

			//Target IP (check for copy)
			for( i = 0; i < 4; i++ )
				if( enc424j600_pop8() != MyIP[i] )
					match = 0;

			if( match == 0 )
				return;

			//We must send a response, so we termiante the packet now.
			enc424j600_finish_callback_now();
			enc424j600_startsend( NetGetScratch() );
			send_etherlink_header( 0x0806 );

			enc424j600_push16( 0x0001 ); //Ethernet
			enc424j600_push16( proto );  //Protocol
			enc424j600_push16( 0x0604 ); //HW size, Proto size
			enc424j600_push16( 0x0002 ); //Reply

			enc424j600_pushblob( MyMAC, 6 );
			enc424j600_pushblob( MyIP, 4 );
			enc424j600_pushblob( sendermac_ip_and_targetmac, 10 ); // do not send target mac.

			enc424j600_endsend();

			// Have a match!
			break;
		}

		// ARP Reply
		case 2: {
			uint8_t sender_mac_and_ip_and_comp_mac[16];
			enc424j600_popblob( sender_mac_and_ip_and_comp_mac, 16 );
			enc424j600_finish_callback_now();


			//First, make sure that we're the ones who are supposed to receive the ARP.
			for( i = 0; i < 6; i++ )
			{
				if( sender_mac_and_ip_and_comp_mac[i+10] != MyMAC[i] )
					break;
			}

			if( i != 6 )
				break;

			//We're the right recipent.  Put it in the table.
			memcpy( &ClientArpTable[ClientArpTablePointer], sender_mac_and_ip_and_comp_mac, 10 );

			ClientArpTablePointer = (ClientArpTablePointer+1)%ARP_CLIENT_TABLE_SIZE;
			break;
		}

		default: {
			//???? don't know what to do.
			return;
		}
	}
}

int enc424j600_receivecallback( uint16_t packetlen )
{
	uint8_t is_the_packet_for_me = 1;
	unsigned char i;
	unsigned char ipproto;

	//First and foremost, make sure we have a big enough packet to work with.
	if (packetlen < 8) {
		// ERROR: Received runt packet
		return 1;
	}

	//macto (ignore) our mac filter handles this.
	enc424j600_dumpbytes( 6 );

	enc424j600_popblob( macfrom, 6 );

	//Make sure it's ethernet!
	if (enc424j600_pop8() != 0x08) {
		// ERROR: Not an ethernet packet
		return 1;
	}

	//Is it ARP?
	if (enc424j600_pop8() == 0x06) {
		HandleArp();
		return 0;
	}

	//otherwise it's standard IP

	//So, we're expecting a '45

	if (enc424j600_pop8() != 0x45) {
		// ERROR: Not an IP packet
		return 1;
	}

	enc424j600_pop8(); //differentiated services field.

	iptotallen = enc424j600_pop16();
	enc424j600_dumpbytes( 5 ); //ID, Offset+FLAGS+TTL
	ipproto = enc424j600_pop8();

	enc424j600_pop16(); //header checksum

	enc424j600_popblob( ipsource, 4 );

	for (i = 0; i < 4; i++) {
		unsigned char m = ~MyMask[i];
		unsigned char ch = enc424j600_pop8();
		if (ch == MyIP[i] || (ch & m) == 0xff) {
			continue;
		}
		is_the_packet_for_me = 0;
	}

	//Tricky, for DHCP packets, we have to detect it even if it is not to us.
	if (ipproto == 17) {
		remoteport = enc424j600_pop16();
		localport = enc424j600_pop16();
	}

	if (!is_the_packet_for_me) {
		// ERROR: Packet is not for us
		return 1;
	}

	//XXX TODO Handle IPL > 5  (IHL?)

	switch(ipproto) {
		// ICMP
		case 1: {
			HandleICMP();
			break;
		}

		// UDP
		case 17: {
			HandleUDP(enc424j600_pop16());
			break;
		}

		default: {
			break;
		}
	}

	return 0;

//finishcb:
//	enc424j600_finish_callback_now();
}


void util_finish_udp_packet( )// unsigned short length )
{
	volatile unsigned short ppl, ppl2;

	//Packet loaded.
	enc424j600_stopop();

	unsigned short length = enc424j600_get_write_length() - 6; //6 = my mac

	//Write length in packet
	enc424j600_alter_word( 10+6, length-14 ); //Experimentally determined 14, 0 was used for a long time.
	enc424j600_start_checksum( 8+6, 20 );
	enc424j600_alter_word( 32+6, length-34 );

	ppl = enc424j600_get_checksum();
	enc424j600_alter_word( 18+6, ppl );

	//Addenudm for UDP checksum

	enc424j600_alter_word( 34+6, 0x11 + 0x8 + length-42 ); //UDP number + size + length (of packet)
		//XXX I THINK 

	enc424j600_start_checksum( 20+6, length - 42 + 16 ); //sta

	ppl2 = enc424j600_get_checksum();
	if( ppl2 == 0 ) ppl2 = 0xffff;

	enc424j600_alter_word( 34+6, ppl2 );

	enc424j600_endsend();
}


void SwitchToBroadcast()
{
	unsigned short i;
	//Set the address we want to send to (broadcast)
	for( i = 0; i < 6; i++ )
		macfrom[i] = 0xff;
}

int8_t RequestARP( uint8_t * ip )
{
	uint8_t i;

	for( i = 0; i < ARP_CLIENT_TABLE_SIZE; i++ )
	{
		if( memcmp( (char*)&ClientArpTable[i].ip, ip, 4 ) == 0 ) //XXX did I mess up my casting?
		{
			return i;
		}
	}

	SwitchToBroadcast();

	//No MAC Found.  Send an ARP request.
	enc424j600_finish_callback_now();
	enc424j600_startsend( NetGetScratch() );
	send_etherlink_header( 0x0806 );

	enc424j600_push16( 0x0001 ); //Ethernet
	enc424j600_push16( 0x0800 ); //Protocol (IP)
	enc424j600_push16( 0x0604 ); //HW size, Proto size
	enc424j600_push16( 0x0001 ); //Request

	enc424j600_pushblob( MyMAC, 6 );
	enc424j600_pushblob( MyIP, 4 );
	enc424j600_push16( 0x0000 );
	enc424j600_push16( 0x0000 );
	enc424j600_push16( 0x0000 );
	enc424j600_pushblob( ip, 4 );

	enc424j600_endsend();

	return -1;
}

struct ARPEntry ClientArpTable[ARP_CLIENT_TABLE_SIZE];
uint8_t ClientArpTablePointer = 0;

struct PINGEntries ClientPingEntries[PING_RESPONSES_SIZE];

int8_t GetPingslot( uint8_t * ip )
{
	uint8_t i;
	for( i = 0; i < PING_RESPONSES_SIZE; i++ )
	{
		if( !ClientPingEntries[i].id ) break;
	}

	if( i == PING_RESPONSES_SIZE )
		return -1;

	memcpy( ClientPingEntries[i].ip, ip, 4 );
	return i;
}

void DoPing( uint8_t pingslot )
{
	unsigned short ppl;
	uint16_t seqnum = ++ClientPingEntries[pingslot].last_send_seqnum;
	uint16_t checksum = (seqnum + pingslot + 0x0800) ;

	int8_t arpslot = RequestARP( ClientPingEntries[pingslot].ip );

	if( arpslot < 0 ) return;

	//must set macfrom to be the IP address of the target.
	memcpy( macfrom, ClientArpTable[arpslot].mac, 6 );

	enc424j600_startsend( NetGetScratch() );
	send_etherlink_header( 0x0800 );
	send_ip_header( 32, ClientPingEntries[pingslot].ip, 0x01 );

	enc424j600_push16( 0x0800 ); //ping request + 0 for code
	enc424j600_push16( ~checksum ); //Checksum
	enc424j600_push16( pingslot ); //Idneitifer
	enc424j600_push16( seqnum ); //Sequence number

	enc424j600_push16( 0x0000 ); //Payload
	enc424j600_push16( 0x0000 );

	enc424j600_start_checksum( 8, 20 );
	ppl = enc424j600_get_checksum();
	enc424j600_alter_word( 18, ppl );

	enc424j600_endsend();
}
