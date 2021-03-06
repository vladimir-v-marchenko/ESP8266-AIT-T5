/*
 * $Id: DHT_Test.ino,v 1.27 2016/01/19 01:09:58 anoncvs Exp $
 *
 * Test program for the AI Thinker "Black board T5", after
 * modification to remove the 8051-based QFP micro and wire
 * the DHT11 sensor and blue LED directly to the ESP8266.
 *
 * Spends most of the time with the ESP8266 in deep-sleep;
 * waking every 10-minutes to read the DHT11 sensor and
 * publish the data to MQTT.
 *
 * NOTE - All user-specific settings (SSID/Passwd, etc)
 *        are in user_config.h.  These settings -MUST-
 *        be customized for -your- network.
 */

#include <ESP8266WiFi.h>
#include <pgmspace.h>
#include <DHT.h>
#include <MQTT.h>
#include "user_config.h"

/* Globals (Terribly bad style, old boy!) */
boolean flgIsConnected = false;	// MQTT connected-to-server flag.
boolean flgIsSubscribed = false;	// MQTT subscribed-to-topic flag.
boolean flgIsPublished = false;	// MQTT publish-completed flag.
int BLED = BLED_PIN;		// Blue debug LED (set in user_config.h).
int GLED = GLED_PIN;		// Green debug LED (set in user_config.h).
int RLED = RLED_PIN;		// Red debug LED (set in user_config.h).
int K2 = K2_PIN;		// "K2" Mode switch (set in user_config.h).
char buff[MQ_TOPIC_SIZE];
char clidbuff[STRBUFF_SIZE], epochbuff[STRBUFF_SIZE];
char vbuff[TEMPR_SIZE], tbuff[TEMPR_SIZE], hbuff[TEMPR_SIZE];
float temperature = 0;
float humidity = 0;

/* The following line is required for proper operation of VCC measurements. */
ADC_MODE(ADC_VCC);

/* Initialize the DHT sensor type to DHT11 (pin configured in usr_config.h). */
DHT dht(DHT_PIN, DHT11, 15);

/* Create WiFi client object. */
WiFiClient wclient;

/* NOTE:- All of these values are configured in user_config.h.  */
const byte ip[] = WIFI_IPADDR;
const byte gateway[] = WIFI_GATEWY;
const byte netmask[] = WIFI_NETMSK;
const byte dnssrv[] = WIFI_DNSSRV;


/* 
 * Add a "yield()" into non-critical delay loops to
 * give the ESP some time for housekeeping operations
 * (just use the normal, unmodified delay() call when
 *  you have specific timing requirements).
 */
void Ydelay(ulong dcount) {
    yield();
    delay(dcount);
}


/*
 * Turn on the individual LEDs for a short
 * flash. Note that on the T5 board the LEDs
 * are driven by low-side switches.
 */
void LedFlash(int led_pin) {
    digitalWrite(led_pin, LOW);
    Ydelay(40);
    digitalWrite(led_pin, HIGH);
}


/*
 * Wrappers for individual colour LED flash routine.
 */
void BFlash() {
    LedFlash(BLED);
}
void GFlash() {
    LedFlash(GLED);
}
void RFlash() {
    LedFlash(RLED);
}


/*
 * Spinning wheel effect.
 */
void LEDSpin(bool direction) {
    for (int lsi = 0; lsi < 3; lsi++) {
	BFlash();
	Ydelay(30);
	if (direction == CLKWISE) {
	    RFlash();
	    Ydelay(30);
	    GFlash();
	    Ydelay(30);
	} else {
	    GFlash();
	    Ydelay(30);
	    RFlash();
	    Ydelay(30);
	}
    }
}


/*
 * Show the world that we're alive.
 */
void OnFlash() {
    GFlash();
    GFlash();
}


/*
 * Blinkenlights display.
 */
void LED_Display() {
    int ldi, ldj;

    for (ldi = 0; ldi <= 3; ldi++) {
	for (ldj = 0; ldj <= 5; ldj++) {
	    yield();
	    LEDSpin(CLKWISE);
	}
	for (ldj = 0; ldj <= 5; ldj++) {
	    yield();
	    LEDSpin(CCLWISE);
	}
    }
}


/*
 * Try to get a valid reading from the DHT11 sensor
 * (not always easy).
 */
void getSensorData() {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    while ((isnan(temperature) || isnan(humidity))
	   || (temperature == 0 && humidity == 0)) {
#ifdef DEBUG
	RFlash();
	Serial.print(F("!"));
#endif
	Ydelay(500);
	temperature = dht.readTemperature();
	humidity = dht.readHumidity();
    }
#ifdef DEBUG
    Serial.println("");
    RFlash();
#endif
}


/*
 *  Create callbacks for the MQTT functions.
 */
void dsConnectedCb() {
#ifdef DEBUG
    Serial.println(F("MQTT connected."));
#endif
    flgIsConnected = true;
}


void dsDisconnectedCb() {
#ifdef DEBUG
    Serial.println(F("MQTT disconnected."));
#endif
    flgIsConnected = false;
    flgIsPublished = false;
}


void dsPublishedCb() {
#ifdef DEBUG
    Serial.println(F("Published."));
#endif
    flgIsPublished = true;
}


void dsDataCb(String & topic, String & data) {
#ifdef DEBUG
    Serial.print(topic);
    Serial.print(F(": "));
    Serial.println(data);
#endif
    /* Store epoch timestamp to temporary buffer. */
    snprintf(epochbuff, STRBUFF_SIZE, "%s", data.c_str());
    flgIsSubscribed = true;
}


/*
 * Call deep-sleep.  The DSLEEP_SECS seconds count is set in
 * user_config.h.
 * Note that "NOTREACHED" should never be displayed if
 * deep-sleep is working correctly, as the processor will do
 * a full reset when it wakes up.
 */
void DSleep() {
    /*
     * Close down our WiFi connections cleanly before sleeping.
     */
    WiFi.disconnect(true);	// Switch-off WiFi = "true".
    Ydelay(500);

#ifdef DEBUG
    Serial.println(F("Zzzz..."));
#endif
    // ESP.deepSleep(DSLEEP_SECS * 1000 * 1000, WAKE_RF_DISABLED);
    ESP.deepSleep(DSLEEP_SECS * 1000 * 1000);

    /*
     * Pause everything to give the system time to set up deep-sleep
     * and shut itself down before restarting the default loop.
     */
    Ydelay(5000);
    Serial.println(F("/*NOTREACHED*/"));	/* Lint on the ESP?!? :-) */
}


void setup() {
    int conn_tries = 0;

    /* Hardware set-up. */
    pinMode(BLED, OUTPUT);	// Ensure our blue LED driver pin is an output,    
    pinMode(GLED, OUTPUT);	// ...and the green LED pin, 
    pinMode(RLED, OUTPUT);	// ...and the red LED pin, too.
    pinMode(K2, INPUT_PULLUP);	// Switch K2 is an input.

    /* Initial state for LEDs is off. */
    digitalWrite(BLED, HIGH);
    digitalWrite(GLED, HIGH);
    digitalWrite(RLED, HIGH);

#ifdef DEBUG
    OnFlash();			// Show that we're alive.
#endif

    Serial.begin(115200);
    Ydelay(500);

// #ifdef DEBUG
//     Serial.print(F("Reset Info: "));
//     Serial.println(ESP.getResetInfo());
// #endif

#ifdef BLINKEN
    Serial.println(F("\n\n\t Press K2 within 3 seconds to"));
    Serial.println(F("\t enter \"blinken\" mode."));

    /*
     * Check for K2 button push and change to
     * LED display mode if detected.  Otherwise
     * continue with normal start-up.
     */
    for (int k2i = 0; k2i <= 30; k2i++) {
	if (digitalRead(K2) == LOW) {
	    LED_Display();
	    break;
	}
	Ydelay(100);
    }

    Serial.println(F("\n\t End of blinken mode."));
#endif

    dht.begin();		// Initialize the DHT11 interface.

    /* Get a unique client-ID for this ESP8266. */
    sprintf(clidbuff, MQTT_CLIENT_ID, ESP.getChipId());
    float vdd = ESP.getVcc() / 1000.0;	// Grab the current battery voltage.
    dtostrf(vdd, 4, 2, vbuff);	// Convert to ASCII and store.

    /*
     * This delay is for use when testing =without= using deep-sleep
     * and should normally be commented-out.
     */
//    Ydelay(LDELAY);    // Disable this loop delay if using deep-sleep.

    /*
     * Get reading from DHT11 and then pack data into
     * MQTT topic buffer, ready for transmission.
     */
    getSensorData();
    dtostrf(temperature, 4, 2, tbuff);	// Convert and store.
    dtostrf(humidity, 4, 2, hbuff);	// Convert and store.

#ifdef DEBUG
    Serial.print(F("   ClientID: "));
    Serial.println(clidbuff);
    Serial.print(F("Temperature: "));
    Serial.print(tbuff);
    Serial.println(F("C"));
    Serial.print(F("   Humidity: "));
    Serial.print(hbuff);
    Serial.println(F("%"));
    Serial.print(F("    Voltage:  "));
    Serial.print(vbuff);
    Serial.println(F("V"));
#endif

    /*
     * Now we have the temperature, humidity and voltage readings, so
     * we're ready to bring up the network and talk to the outside
     * world.
     */
#ifdef DEBUG
    Serial.println();
    Serial.print(F("Connecting to "));
    Serial.println(STA_SSID);
#endif

    /*
     * An open-ended loop  here will flatten the battery on
     * the sensor if, for instance, the access point is 
     * down, so limit our connection attempts.
     */
    Serial.print("WiFi Stat: ");
    Serial.println(WiFi.status());	// Reputed to fix slow-start problem.
    WiFi.mode(WIFI_STA);
    WiFi.config(IPAddress(ip), IPAddress(gateway),
		IPAddress(netmask), IPAddress(dnssrv));
//    wifi_station_set_auto_connect(false);
    WiFi.begin(STA_SSID, STA_PASS, WIFI_CHANNEL, NULL);
    yield();

    while ((WiFi.status() != WL_CONNECTED)
	   && (conn_tries++ < WIFI_RETRIES)) {
	Ydelay(500);
#ifdef DEBUG
	Serial.print(".");
#endif
    }
    if (conn_tries == WIFI_RETRIES) {
#ifdef DEBUG
	Serial.println(F("No WiFi connection ...sleeping."));
#endif
	DSleep();
    }
#ifdef DEBUG
    Serial.println();
    WiFi.printDiag(Serial);
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
#endif

    /*
     * Now that we have a WiFi link, publish our data to the MQTT
     * server.  As with the WiFi connection, the MQTT publish
     * attempts are in a limited loop to prevent a failed connection
     * from holding the ESP8266 on and draining the battery.
     */
    MQTT mqttClient(clidbuff, MQTT_HOST, MQTT_PORT);

    /* Configure MQTT callbacks.  */
    mqttClient.onConnected(dsConnectedCb);
    mqttClient.onDisconnected(dsDisconnectedCb);
    mqttClient.onPublished(dsPublishedCb);
    mqttClient.onData(dsDataCb);

    /*
     * MQTT connect/subscribe loop.
     *      Subscribe to epoch timestamp server.
     */
    conn_tries = 0;		// Reset tries counter to zero.
    while (!flgIsSubscribed && (conn_tries++ < WIFI_RETRIES)) {
	Ydelay(MQCONDEL);
	if (flgIsConnected) {
	    mqttClient.subscribe(TOPIC1, 0);	// Topics set in user_config.h.
	} else {
#ifdef DEBUG
	    if (conn_tries == 1) {
		Serial.print(F("Connecting to MQTT server: "));
		Serial.print(MQTT_HOST);
	    } else {
		Serial.print(F("."));
	    }
#endif
	    mqttClient.connect();
	}
    }
#ifdef DEBUG
    Serial.println(F(""));
#endif

    /*
     * MQTT publish loop.
     *      If we're not connected, just drop straight through
     *      to the deep-sleep.  Otherwise, fill the buffer with
     *      the data we've accumulated so far and then publish
     *      it to our selected topic.
     */
    if (flgIsConnected) {
	snprintf(buff, MQ_TOPIC_SIZE, "%s, %s, %sC, %s%% %sV", epochbuff,
		 clidbuff, tbuff, hbuff, vbuff);
#ifdef DEBUG
	Serial.println(buff);
#endif
	conn_tries = 0;		// Reset tries counter to zero.
	while (!flgIsPublished && (conn_tries++ < WIFI_RETRIES)) {
	    mqttClient.publish(TOPIC3, buff, strlen(buff), 0, 0);	// Topics set in user_config.h.
	    Ydelay(MQCONDEL * conn_tries);
	}
    }

    /*
     * Even if we didn't manage to publish for whatever reason, we're
     * going to give up and go back to sleep to save battery power.
     */
#ifdef DEBUG
    if (!flgIsConnected) {
	Serial.print(F("MQTT Connect Failed. "));
    }
    if (!flgIsSubscribed) {
	Serial.print(F("Subscribe Failed. "));
    }
    if (!flgIsPublished) {
	Serial.print(F("Publish Failed. "));
    }
    Serial.println(F(""));
#endif
    mqttClient.disconnect();
    Ydelay(250);

    /*
     * Put the unit into deep sleep (requires GPIO16 to be connected
     * to RST for wake-up).
     */
    DSleep();
}


/* We should never get as far as the loop() function. */
void loop() {
// Nuthin' to see here.  Move along now!
    ESP.restart();
}
