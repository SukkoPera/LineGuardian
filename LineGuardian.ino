/*******************************************************************************
 * This file is part of LineGuardian.                                          *
 *                                                                             *
 * Copyright (C) 2015-2016 by SukkoPera <software@sukkology.net>               *
 *                                                                             *
 * LineGuardian is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU General Public License as published by        *
 * the Free Software Foundation, either version 3 of the License, or           *
 * (at your option) any later version.                                         *
 *                                                                             *
 * LineGuardian is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU General Public License for more details.                                *
 *                                                                             *
 * You should have received a copy of the GNU General Public License           *
 * along with LineGuardian. If not, see <http://www.gnu.org/licenses/>.        *
 *******************************************************************************
 *
 * LineGuardian - Let your Arduino check that your Net connection is up and
 * running and restart your modem modem/router otherwise
 *
 * Only supports ENC28J60-based Ethernet interfaces, at the moment.
 *
 * Please refer to the GitHub page and wiki for any information:
 * https://github.com/SukkoPera/LineGuardian
 */


/*******************************************************************************
 * CUSTOMIZABLE SETTINGS
 ******************************************************************************/

// ethernet interface mac address, must be unique on the LAN
const byte mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

const unsigned int ETHERNET_BUFSIZE = 800;

#define PINGED_HOST "www.google.com"

// Time (seconds) to allow the router to power on and connect
const unsigned long POWER_ON_TIME = 180;

// Time (seconds) to leave the router off
const unsigned long POWER_OFF_TIME = 10;

// Time interval (seconds) to test for connectivity
const unsigned long PING_INTERVAL = 180;

// Time (seconds) after which a ping is considered to time out
const unsigned long PING_TIMEOUT = 15;

// Number of failures after which router is restarted
const byte MAX_FAILURES = 3;

// Pin connected to red led
const byte LED_PIN_R = 2;

// Pin connected to green led
const byte LED_PIN_G = 3;

// Pin driving relay
const byte RELAY_PIN = 4;

// Define this if relay is active-low
#define RELAY_ACTIVE_LOW

/* PushingBox device ID to notify when router is rebooted.
 * Do not define if unwanted.
 */
//~ #define PUSHINGBOX_DEVID_REBOOT ""

/* PushingBox device ID to notify when connection is lost briefly.
 * Do not define if unwanted.
 */
//~ #define PUSHINGBOX_DEVID_DROPOUT ""

// NTP Server (Only used if PushingBox support is required)
const char timeServer[] PROGMEM = "pool.ntp.org";

// Local UTC offset
const int UTC_OFFSET = 1;     // Central European Time
//const int UTC_OFFSET = -5;  // Eastern Standard Time (USA)
//const int UTC_OFFSET = -4;  // Eastern Daylight Time (USA)
//const int UTC_OFFSET = -8;  // Pacific Standard Time (USA)
//const int UTC_OFFSET = -7;  // Pacific Daylight Time (USA)

// NTP port (UDP)
const unsigned int NTP_PORT = 123;

// Time to wait for a reply from the NTP server (ms)
const unsigned int NTP_TIMEOUT = 3000;

/*******************************************************************************
 * END OF SETTINGS
 ******************************************************************************/


#if defined (PUSHINGBOX_DEVID_REBOOT) || defined (PUSHINGBOX_DEVID_DROPOUT)
	#define ENABLE_PUSHINGBOX
#endif

#include <EtherCard.h>
#include <IPAddress.h>
#include <TimeLib.h>

#ifdef ENABLE_PUSHINGBOX
#include <PString.h>
#endif

#define NDEBUG
#include "debug.h"


// Time (micros()) of last ECHO request sent
unsigned long pingStartTime = 0;		// 0 -> No ping in progress

// Time (millis()) at which current state was entered
unsigned long stateEnteredTime = 0;

// Number of consecutive ping failures
byte pingFailures = 0;

// Ethernet packet buffer
byte Ethernet::buffer[ETHERNET_BUFSIZE];

// True if network is connected
boolean lineUp = false;

// Time at which line last went up
time_t timeLineWentUp = 0;

// Time at which line last went down
time_t timeLineWentDown = 0;

// Time at which we last rebooted the router
time_t timeRebooted = 0;

// Program version
#define PROGRAM_VERSION "20160904"


enum State {
	ST_INIT,			// Power on router, wait POWER_ON_TIME
	ST_WAIT,			// Wait until time for next ping
	ST_SEND_REQUEST,	// Send echo request
	ST_WAIT_REPLY,		// Wait for echo reply
	ST_OK,				// Connection is up,
	ST_FAIL,			// Connection failed
	ST_POWER_CYCLE		// Power off router, wait POWER_OFF_TIME
};

State state = ST_INIT;

inline void enterState (State s) {
	state = s;
	stateEnteredTime = millis ();

	DPRINT (F("Entering state "));
	DPRINTLN (s);
}

// Checks if the current state was entered longer than sec seconds
inline boolean inStateSince (unsigned long sec) {
	return millis () - stateEnteredTime >= sec * 1000UL;
}

// Called when a ping comes in (replies to it are automatic)
void gotPinged (byte* src) {
	DPRINT (F("Pinged from: "));
	DPRINTLN (IPAddress (src));
}

boolean start_ping () {
	boolean ret = false;

	if (pingStartTime != 0) {
		DPRINTLN (F("Ping already in progress"));
	} else {
		DPRINTLN (F("Looking up host to ping..."));
		if (!ether.dnsLookup (PSTR (PINGED_HOST))) {
			DPRINTLN (F("DNS failed"));
			pingStartTime = 0;
		} else {
			DPRINT (F("Resolved to: "));
			DPRINTLN (IPAddress (ether.hisip));

			ether.clientIcmpRequest (ether.hisip);
			pingStartTime = micros ();
			ret = true;
		}
	}

	return ret;
}

boolean check_for_ping_reply () {
	if (ether.packetLoopIcmpCheckReply (ether.hisip)) {
		unsigned long replyTime = micros ();
		DPRINT (F("Got ping reply: "));
		DPRINT ((replyTime - pingStartTime) / 1000.0, 1);
		DPRINTLN (F(" ms"));

		pingStartTime = 0;

		return true;
	} else {
		return false;
	}
}

inline void enableRelay () {
#ifdef RELAY_ACTIVE_LOW
	digitalWrite (RELAY_PIN, LOW);
#else
	digitalWrite (RELAY_PIN, HIGH);
#endif
}

inline void disableRelay () {
#ifdef RELAY_ACTIVE_LOW
	digitalWrite (RELAY_PIN, HIGH);
#else
	digitalWrite (RELAY_PIN, LOW);
#endif
}

void led_off () {
	digitalWrite (LED_PIN_R, LOW);
	digitalWrite (LED_PIN_G, LOW);
}

void led_orange () {
	digitalWrite (LED_PIN_R, HIGH);
	digitalWrite (LED_PIN_G, HIGH);
}

void led_red () {
	digitalWrite (LED_PIN_R, HIGH);
	digitalWrite (LED_PIN_G, LOW);
}

void led_green () {
	digitalWrite (LED_PIN_R, LOW);
	digitalWrite (LED_PIN_G, HIGH);
}

time_t prevDisplay = 0; // when the digital clock was displayed
void setup () {
	DSTART (9600);
	DPRINT (F("LineGuardian "));
	DPRINTLN (F(PROGRAM_VERSION));

	// Setup leds
	pinMode (LED_PIN_R, OUTPUT);
	pinMode (LED_PIN_G, OUTPUT);
	led_orange ();

	if (!ether.begin (sizeof (Ethernet::buffer), mac)) {
		DPRINTLN (F("Failed to access Ethernet controller"));

		// Halt here blinking fast
		while (42) {
			led_red ();
			delay (150);
			led_off ();
			delay (150);
		}
	}

	DPRINTLN (F("Trying to get configuration from DHCP"));
	if (!ether.dhcpSetup ()) {
		DPRINTLN (F("Failed to get configuration from DHCP"));

		// Halt here blinking more slowly
		while (42) {
			led_red ();
			delay (500);
			led_off ();
			delay (500);
		}
	}

	DPRINTLN (F("DHCP configuration done:"));
	DPRINT (F("- IP: "));
	DPRINTLN (IPAddress (EtherCard::myip));
	DPRINT (F("- Netmask: "));
	DPRINTLN (IPAddress (EtherCard::netmask));
	DPRINT (F("- Default Gateway: "));
	DPRINTLN (IPAddress (EtherCard::gwip));

#ifdef ENABLE_PUSHINGBOX
	// Sync with NTP
	DPRINTLN (F("Syncing time with NTP..."));
	//setSyncProvider (getNtpTime);            // Use this for GMT time
	setSyncProvider (getDstCorrectedTime);     // Use this for local, DST-corrected time
#endif

	// Call this when others ping us
	ether.registerPingCallback (gotPinged);

	// Setup relay
#ifdef RELAY_ACTIVE_LOW
	// Use this order to avoid a spurious turn on at startup
	disableRelay ();
	pinMode (RELAY_PIN, OUTPUT);
#else
	pinMode (RELAY_PIN, OUTPUT);
	disableRelay ();
#endif

	// Let's roll!
	enterState (ST_INIT);
}

void digitalClockDisplay () {
	// digital clock display of the time
	DPRINT (hour ());
	printDigits (minute ());
	printDigits (second ());
	DPRINT (" ");
	DPRINT (day ());
	DPRINT (" ");
	DPRINT (month ());
	DPRINT (" ");
	DPRINT (year ());
	DPRINTLN ();
}

void printDigits (int digits) {
	// utility for digital clock display: prints preceding colon and leading 0
	DPRINT (":");
	if(digits < 10)
		DPRINT ('0');
	DPRINT (digits);
}

void loop () {
	if (timeStatus () != timeNotSet) {
		if (now() != prevDisplay) { //update the display only if time has changed
			prevDisplay = now();
			digitalClockDisplay();
		}
	}


	// Ethernet loop
	word len = ether.packetReceive (); // go receive new packets
	/*word pos =*/ ether.packetLoop (len); // respond to incoming pings

	switch (state) {
		case ST_INIT:
			led_orange ();
			enableRelay ();
			pingFailures = 0;
			if (inStateSince (POWER_ON_TIME)) {
				enterState (ST_SEND_REQUEST);
			}
			break;
		case ST_SEND_REQUEST:
			if (start_ping ()) {
				// Ping started successfully, wait for reply
				enterState (ST_WAIT_REPLY);
			} else {
				/* Could not send ping request, possibly because of a DNS
				 * failure, so connection may be down already
				 */
				enterState (ST_FAIL);
			}
			break;
		case ST_WAIT_REPLY:
			if (len > 0 && check_for_ping_reply ()) {
				// Ping successful!
				pingFailures = 0;
				enterState (ST_OK);
			} else if (inStateSince (PING_TIMEOUT)) {
				// Ping timeout
				enterState (ST_FAIL);
			}
			break;
		case ST_OK:
			led_green ();
			lineUp = true;
			timeLineWentUp = now ();

#ifdef ENABLE_PUSHINGBOX
			if (timeLineWentDown != 0) {
				if (timeRebooted > 0) {
					sendRebootNotification (timeLineWentDown, timeRebooted, timeLineWentUp);
				} else {
					sendDropoutNotification (timeLineWentDown, timeLineWentUp);
				}
				timeLineWentDown = 0;
				timeRebooted = 0;
			}
#endif

			enterState (ST_WAIT);
			break;
		case ST_FAIL:
			led_red ();
			lineUp = false;
			timeLineWentDown = now ();
			if (++pingFailures >= MAX_FAILURES) {
				enterState (ST_POWER_CYCLE);
			} else {
				enterState (ST_WAIT);
			}
			break;
		case ST_WAIT:
			if (inStateSince (PING_INTERVAL)) {
				enterState (ST_SEND_REQUEST);
			}
			break;
		case ST_POWER_CYCLE:
			led_orange ();
			timeRebooted = now ();
			disableRelay ();
			if (inStateSince (POWER_OFF_TIME)) {
				enterState (ST_INIT);
			}
			break;
	}
}


/*******************************************************************************
 * PUSHINGBOX STUFF
 ******************************************************************************/

#ifdef ENABLE_PUSHINGBOX

// SyncProvider that returns UTC time
time_t getNtpTime () {
	// Send request
	DPRINTLN (F("Transmit NTP Request"));
	if (!ether.dnsLookup (timeServer)) {
		DPRINTLN (F("DNS failed"));
		return 0; // return 0 if unable to get the time
	} else {
		//ether.printIp("SRV: ", ether.hisip);
		ether.ntpRequest (ether.hisip, NTP_PORT);

		// Wait for reply
		unsigned long beginWait = millis ();
		while (millis () - beginWait < NTP_TIMEOUT) {
			word len = ether.packetReceive ();
			ether.packetLoop (len);

			unsigned long secsSince1900 = 0L;
			if (len > 0 && ether.ntpProcessAnswer (&secsSince1900, NTP_PORT)) {
				DPRINTLN (F("Receive NTP Response"));
				return secsSince1900 - 2208988800UL;
			}
		}

		DPRINTLN (F("No NTP Response :-("));
		return 0;
	}
}

/* Alternative SyncProvider that automatically handles Daylight Saving Time
 * (DST) periods, at least in Europe, see below.
 */
time_t getDstCorrectedTime (void) {
	time_t t = getNtpTime ();

	if (t > 0) {
		TimeElements tm;
		breakTime (t, tm);
		t += (UTC_OFFSET + dstOffset (tm.Day, tm.Month, tm.Year + 1970, tm.Hour)) * SECS_PER_HOUR;
	}

	return t;
}

/* This function returns the DST offset for the current UTC time.
 * This is valid for the EU, for other places see
 * http://www.webexhibits.org/daylightsaving/i.html
 *
 * Results have been checked for 2012-2030 (but should work since
 * 1996 to 2099) against the following references:
 * - http://www.uniquevisitor.it/magazine/ora-legale-italia.php
 * - http://www.calendario-365.it/ora-legale-orario-invernale.html
 */
byte dstOffset (byte d, byte m, unsigned int y, byte h) {
	// Day in March that DST starts on, at 1 am
	byte dstOn = (31 - (5 * y / 4 + 4) % 7);

	// Day in October that DST ends  on, at 2 am
	byte dstOff = (31 - (5 * y / 4 + 1) % 7);

	if ((m > 3 && m < 10) ||
		(m == 3 && (d > dstOn || (d == dstOn && h >= 1))) ||
		(m == 10 && (d < dstOff || (d == dstOff && h <= 1))))
		return 1;
	else
		return 0;
}


// PushingBox API access point
const char PUSHINGBOX_API_HOST[] PROGMEM = "api.pushingbox.com";

// called when the client request is complete
// FIXME: Parse reply to see if request was successful
static void pbApiCallback (byte status, word off, word len) {
	DPRINTLN (F(">>>"));
	Ethernet::buffer[off + 500] = 0;
	DPRINT ((const char*) Ethernet::buffer + off);
	DPRINTLN (F("..."));
}

class PStringWithEncoder: public PString {
private:
	static const char *HEX_DIGITS;
	static const byte BUFSIZE;

public:
	PStringWithEncoder (char *buf, size_t size): PString (buf, size) {
	}

	// Inspired from http://hardwarefun.com/tutorials/url-encoding-in-arduino
	void printEncoded (const char* s) {
		for (; *s; s++) {
			if (('a' <= *s && *s <= 'z')
					 || ('A' <= *s && *s <= 'Z')
					 || ('0' <= *s && *s <= '9')) {

				print (*s);
			} else {
				print ('%');
				print (HEX_DIGITS[*s >> 4]);
				print (HEX_DIGITS[*s & 15]);
			}
		}
	}
};

const char *PStringWithEncoder::HEX_DIGITS = "0123456789abcdef";
const byte PStringWithEncoder::BUFSIZE = 32;

const unsigned int MAX_POSTDATA_LEN = 128;
char postDataBuf[MAX_POSTDATA_LEN];
PStringWithEncoder postData (postDataBuf, MAX_POSTDATA_LEN);

//~ #define USE_SECONDS

char *formatTime (time_t t) {
	static char pbuf[32];
	PString p (pbuf, 32);

	TimeElements tm;
	breakTime (t, tm);

	p.print (tm.Day);
	p.print ('/');
	p.print (tm.Month);
	p.print ('/');
	p.print (tm.Year + 1970);
	p.print (' ');
	p.print (tm.Hour);
	p.print (':');
	if (tm.Minute < 10)
		p.print ('0');
	p.print (tm.Minute);
#ifdef USE_SECONDS
	p.print (':');
	if (tm.Second < 10)
		p.print ('0');
	p.print (tm.Second);
#endif

	return pbuf;
}

boolean sendRebootNotification (time_t time_lost, time_t time_reboot, time_t time_ok) {
	boolean ret = false;

#ifdef PUSHINGBOX_DEVID_REBOOT
	if (ether.dnsLookup (PUSHINGBOX_API_HOST)) {
		DPRINTLN (F("Unable to resolve address of PushingBox API"));
	} else {
		postData.begin ();
		postData.print (F("devid="));
		postData.print (F(PUSHINGBOX_DEVID_REBOOT));
		postData.print (F("&time_reboot="));
		postData.printEncoded (formatTime (time_reboot));
		postData.print (F("&time_lost="));
		postData.printEncoded (formatTime (time_lost));
		postData.print (F("&time_ok="));
		postData.printEncoded (formatTime (time_ok));

		//~ DPRINT ("POSTDATA: ");
		//~ DPRINTLN (postData);

		ether.httpPost (PSTR ("/pushingbox"), PUSHINGBOX_API_HOST, NULL, postData, pbApiCallback);
		ret = true;
	}
#endif

	return ret;
}

boolean sendDropoutNotification (time_t time_lost, time_t time_ok) {
	boolean ret = false;

#ifdef PUSHINGBOX_DEVID_DROPOUT
	if (ether.dnsLookup (PUSHINGBOX_API_HOST)) {
		DPRINTLN (F("Unable to resolve address of PushingBox API"));
	} else {
		postData.begin ();
		postData.print (F("devid="));
		postData.print (F(PUSHINGBOX_DEVID_DROPOUT));
		postData.print (F("&time_lost="));
		postData.printEncoded (formatTime (time_lost));
		postData.print (F("&time_ok="));
		postData.printEncoded (formatTime (time_ok));

		//~ DPRINT ("POSTDATA: ");
		//~ DPRINTLN (postData);

		ether.httpPost (PSTR ("/pushingbox"), PUSHINGBOX_API_HOST, NULL, postData, pbApiCallback);
		ret = true;
	}
#endif

	return ret;
}

#endif		// ENABLE_PUSHINGBOX
