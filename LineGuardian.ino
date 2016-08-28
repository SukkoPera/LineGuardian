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

// Pin driving relay
const byte RELAY_PIN = 2;

// Define this if relay is active-low
#define RELAY_ACTIVE_LOW

// Pin connected to red led
const byte LED_PIN_R = 3;

// Pin connected to green led
const byte LED_PIN_G = 4;

/*******************************************************************************
 * END OF SETTINGS
 ******************************************************************************/


#include <EtherCard.h>
#include <IPAddress.h>

// Time (micros()) of last ECHO request sent
unsigned long pingStartTime = 0;		// 0 -> No ping in progress

// Time (millis()) at which current state was entered
unsigned long stateEnteredTime = 0;

// Number of consecutive ping failures
byte pingFailures = 0;

// Ethernet packet buffer
byte Ethernet::buffer[ETHERNET_BUFSIZE];

// Program version
#define PROGRAM_VERSION "20160829"


enum State {
	ST_INIT,			// Power on router, wait POWER_ON_TIME
	ST_SEND_REQUEST,	// Send echo request
	ST_WAIT_REPLY,		// Wait for echo reply
	ST_OK,				// Connection is up, send ping every PING_INTERVAL
	ST_FAIL,			// Connection failed, send ping every PING_INTERVAL, if MAX_FAILURES is reached, restart router
	ST_POWER_CYCLE		// Power off router, wait POWER_OFF_TIME
};

State state = ST_INIT;

inline void enterState (State s) {
	state = s;
	stateEnteredTime = millis ();

	Serial.print (F("Entering state "));
	Serial.println (s);
}

// Checks if the current state was entered longer than sec seconds
inline boolean inStateSince (unsigned long sec) {
	return millis () - stateEnteredTime >= sec * 1000UL;
}

// Called when a ping comes in (replies to it are automatic)
void gotPinged (byte* src) {
	Serial.print (F("Pinged from: "));
	Serial.println (IPAddress (src));
}

boolean start_ping () {
	boolean ret = false;

	if (pingStartTime != 0) {
		Serial.println (F("Ping already in progress"));
	} else {
		Serial.println (F("Looking up host to ping..."));
		if (!ether.dnsLookup (PSTR (PINGED_HOST))) {
			Serial.println (F("DNS failed"));
			pingStartTime = 0;
		} else {
			Serial.print (F("Resolved to: "));
			Serial.println (IPAddress (ether.hisip));

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
		Serial.print (F("Got ping reply: "));
		Serial.print ((replyTime - pingStartTime) / 1000.0, 1);
		Serial.println (" ms");

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


void setup () {
	Serial.begin (9600);
	Serial.print (F("LineGuardian "));
	Serial.println (F(PROGRAM_VERSION));

	// Setup leds
	pinMode (LED_PIN_R, OUTPUT);
	pinMode (LED_PIN_G, OUTPUT);

	if (!ether.begin (sizeof (Ethernet::buffer), mac)) {
		Serial.println (F("Failed to access Ethernet controller"));

		// Halt here blinking fast
		while (42) {
			led_red ();
			delay (150);
			led_off ();
			delay (150);
		}
	}

	Serial.println (F("Trying to get configuration from DHCP"));
	if (!ether.dhcpSetup ()) {
		Serial.println (F("Failed to get configuration from DHCP"));

		// Halt here blinking more slowly
		while (42) {
			led_red ();
			delay (500);
			led_off ();
			delay (500);
		}
	}

	Serial.println (F("DHCP configuration done:"));
	Serial.print (F("- IP: "));
	Serial.println (IPAddress (EtherCard::myip));
	Serial.print (F("- Netmask: "));
	Serial.println (IPAddress (EtherCard::netmask));
	Serial.print (F("- Default Gateway: "));
	Serial.println (IPAddress (EtherCard::gwip));

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

void loop () {
	word len = ether.packetReceive (); // go receive new packets
	word pos = ether.packetLoop (len); // respond to incoming pings

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
				pingFailures++;
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
				pingFailures++;
				enterState (ST_FAIL);
			}
			break;
		case ST_OK:
			led_green ();
			if (inStateSince (PING_INTERVAL)) {
				enterState (ST_SEND_REQUEST);
			}
			break;
		case ST_FAIL:
			led_red ();
			if (pingFailures >= MAX_FAILURES) {
				enterState (ST_POWER_CYCLE);
			} else if (inStateSince (PING_INTERVAL)) {
				enterState (ST_SEND_REQUEST);
			}
			break;
		case ST_POWER_CYCLE:
			led_orange ();
			disableRelay ();
			if (inStateSince (POWER_OFF_TIME)) {
				enterState (ST_INIT);
			}
			break;
	}
}
