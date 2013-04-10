/// FSK weather station receiver
/// Receive packets echoes to serial.
/// Updates DCF77 time.
/// Supports Alecto WS3000, WS4000, Fine Offset WH1080 and similar 868MHz stations
/// National Geographic 265 requires adaptation of frequency to 915MHz band.
/// input handler and send functionality in code, but not implemented or used.
/// @see http://jeelabs.org/2010/12/11/rf12-acknowledgements/
//  2013-03-03<info@sevenwatt.com> http://opensource.org/licenses/mit-license.php

//  2013-04-09<andy@laughlinez.com v102- code tweaks to fit onto ATTINY84/85
//  also restructure to make little more modular


#define APP_VERSION "v102"

#include <RF12WS.h> //temporary branch of rfm12b library from jeelabs - hopefully merged when new version released.

//ALL variables that would normally need changing have moved to "_variables.h"
//that should be in a second Tab if using the Arduino IDE
//This is so that core logic can be kept in this file and only updated
//as bugs/new features come along. Its also a good way to structure the
//sketch when using source control like Git. i.e you can .gitignore the _variables.h mostly once 
//you have the basic sketch and do Fetch to update changes to this file, or should
//you wish to contribute, you can make a pull request to the origin of this file, 
//without sending your own config data etc.
#include "_variables.h"

//V2 protocol defines
#define MSG_WS4000 40
#define MSG_WS3000 42
#define LEN_WS4000 10
#define LEN_WS3000 9
#define LEN_MAX 10

//receiver tracking parameters
static unsigned long ok_ts;
static byte packet_found = 0;
static byte ok_cnt = 0;
static byte pkt_cnt = 0;
static uint8_t packet[10];
static uint8_t msgformat = 0;
static uint8_t pktlen = LEN_MAX;
static uint8_t txcnt = 0;
static byte dcf77 = 0;

//start of unixtime epoch - 1970 - will be used for mini time/stamp manager
const unsigned long unixStart = 2208988800UL;


#ifdef LED_PIN  // - see _variables.h
void activityLed (byte on) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, !on);
}
#endif

void configureWH1080 () {
    rf12_restore(NODE_ID, RF12_868MHZ, 0xD4); // group 212
    rf12_setBitrate(0x13);                    // 17.24 kbps
    rf12_control(0xA67C);                     // 868.300 MHz
    rf12_setFixedLength(LEN_MAX);             // receive fixed number of bytes  
}

#ifdef MEM_CHECKS  // - see _variables.h
int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
#endif

//This will reroute the weather station packet using the standard jeelabs rfm12b packet format 
#ifdef FWD_PKT
void reroutePacket() {
      byte sendbuf[15]; /* little more than we need but allows expansion whilst preserving memory map */
      byte sendlen;
      
#ifdef MEM_CHECKS
      Serial.print( "FR3:" );
      Serial.println( freeRam() );
#endif
      sendlen = 0;
      sendbuf[sendlen++] = txcnt++;
      sendbuf[sendlen++] = msgformat;
      sendbuf[sendlen++] = pktlen;
      memcpy(&sendbuf[sendlen], packet, pktlen);
      sendlen += pktlen;
      rf12_restore(NODE_ID, RF12_868MHZ, GROUP_ID);
      while (!rf12_canSend())
          rf12_recvDone(); // ignores incoming
#ifdef LED_PIN
      activityLed(1);
#endif
      rf12_sendStart(DEST_NODE_ID, sendbuf, sendlen, 1);
      configureWH1080();    
   
}
#endif

void setup() {

  
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif

    Serial.begin(SERIAL_BAUD);
    Serial.print(F("\n#[weatherstationFSK "));
    Serial.print(APP_VERSION);
    Serial.println("]");
    
#ifdef LED_PIN
    activityLed(0);
#endif

    rf12_initialize(NODE_ID, RF12_868MHZ, GROUP_ID); //reset RFM12B and initialize - see _variables.h
    configureWH1080();
    
}

#ifdef LOOP_ACTIVITY
uint16_t counter = 0;
#endif
void loop() {

#ifdef LOOP_ACTIVITY
    counter++;
    if (counter > 40000 )
    { counter = 0;
    Serial.print(".");
    }
#endif  
    if (rf12_recvDone()) {
        //check crc
        pkt_cnt++; //how many packets since got last good one
        byte crc_ok = 0;
        //get device type
        uint8_t mt = rf12_buf[1] >> 4;
        
        switch (mt) {
          //9 byte devices
          case 0x6: //ws3000 DCF77 packet
             dcf77=1;
          case 0x5: //ws3000 data packet
          {
            dcf77=0;
            crc_ok = rf12_buf[9] == _crc8(&rf12_buf[1], 8);
            if (crc_ok) ok_cnt++;
            msgformat = MSG_WS3000;
            pktlen = LEN_WS3000;
            break;
          }
          //10 byte devices
          case 0xB: //ws4000 DCF77 packet 
             dcf77=1;
          case 0xA: //ws4000 data packet
          {
            dcf77=0;
            crc_ok = rf12_buf[10] == _crc8(&rf12_buf[1], 9);
            if (crc_ok) ok_cnt++;
            msgformat = MSG_WS4000;
            pktlen = LEN_WS4000;
            break;
          }
          default: break; //crc_ok=0;
        }
        
#ifdef LOGRAW
        //Log all packages. Packages may be missed due to short intervals
        Serial.print(crc_ok ? F(" ok ") : F("nok "));
        for (byte i = 1; i < LEN_MAX+1; i++) {
            Serial.print(' ');
            Serial.print(rf12_buf[i] >> 4, HEX);
            Serial.print(rf12_buf[i] & 0x0F, HEX);
        }
        Serial.println();
#endif
        
        //save the first crc_ok package of a burst
        if ((!packet_found) && crc_ok){
          //start one second interval to count upto six identical packets.
          ok_ts=millis();
          packet_found = 1; //true
          memcpy(packet, (char *)&rf12_buf[1], 10);
        }
    } //rf12_recvDone
    
    //Report if transmission is finished (38ms after first package detected)
    if (packet_found && ((long)(millis()-ok_ts) > 50)) {
#ifdef LOGDCF
      //check both 9 & 10 byte packets for a DCF77 payload
      //uint8_t mt = packet[0] >> 4;
      //Set time if time packet received
      //if (mt == 0xB || mt == 0x6) {
      if (dcf77) {  
        update_time(/*msgformat,*/ packet);  //also prints timestamp
      }
#endif

#ifdef LOGPKT
      //Transmission of repeated packages is done. Report results.
      timestamp();
#ifdef MEM_CHECKS
      Serial.print( F("FR1:") );
      Serial.println( freeRam() );
#endif
      delay(2000);
      Serial.print(7, BYTE);
      Serial.print(F("pkt_cnt: "));
      Serial.print(pkt_cnt, DEC);
      Serial.print(F(" ok_cnt: "));
      Serial.print(ok_cnt, DEC);
      Serial.print(F(" pkt: "));
      for (byte i = 0; i < pktlen; i++) {
          Serial.print(packet[i] >> 4, HEX);
          Serial.print(packet[i] & 0x0F, HEX);
      }
      Serial.println("");
#endif

#ifdef LOGDAT
      if (!dcf77) {
        timestamp();
        decodeSensorData(msgformat, packet);
      }
#endif

#ifdef FWD_PKT
      //send the packet to the central node
      if (!dcf77) reroutePacket();    
#endif      
#ifdef FWD_DCF
      //send the packet to the central node
      if (dcf77) reroutePacket();    
#endif      

      //reset administration
      ok_cnt = pkt_cnt = 0;
      packet_found = 0; //false
      dcf77 = 0;
      
#ifdef LED_PIN
      activityLed(0);
#endif

    }
}

/*
* Function taken from Luc Small (http://lucsmall.com), itself
* derived from the OneWire Arduino library. Modifications to
* the polynomial according to Fine Offset's CRC8 calulations.
*/
uint8_t _crc8(volatile uint8_t *addr, uint8_t len)
{
	uint8_t crc = 0;

	// Indicated changes are from reference CRC-8 function in OneWire library
	while (len--) {
		uint8_t inbyte = *addr++;
		uint8_t i;
		for (i = 8; i; i--) {
			uint8_t mix = (crc ^ inbyte) & 0x80; // changed from & 0x01
			crc <<= 1; // changed from right shift
			if (mix) crc ^= 0x31;// changed from 0x8C;
			inbyte <<= 1; // changed from right shift
		}
	}
	return crc;
}

void timestamp()
{
  //This is where we will simulate a unixtime() time keeping device using millis() at approx 1sec resolution.
  //for now just print a placeholder
  Serial.println( "--TIMESTAMP--");

  //Serial.print(year()); 
  //Serial.print("-");
  //printDigits(month());
  //Serial.print("-");
  //printDigits(day());
  //Serial.print(" "); 
  //printDigits(hour());
  //Serial.print(":");
  //printDigits(minute());
  //Serial.print(":");
  //printDigits(second());
  //Serial.print(" ");
}


int BCD2bin(uint8_t BCD) {
  return (10 * (BCD >> 4 & 0xF) + (BCD & 0xF));
}

void update_time(uint8_t* tbuf) {

  //This is where we will simulate a unixtime() time keeping device using millis() at approx 1sec resolution.
  //for now just print a placeholder
  
//  //setTime(BCD2bin(tbuf[2] & 0x3F),BCD2bin(tbuf[3]),BCD2bin(tbuf[4]),BCD2bin(tbuf[7]),BCD2bin(tbuf[6] & 0x1F),BCD2bin(tbuf[5]));
  //hrs mins secs
  Serial.print( (int)BCD2bin(tbuf[2] & 0x3F) ); //h
  Serial.print(":");
  Serial.print( (int)BCD2bin(tbuf[3]) ); //m
  Serial.print(":");
  Serial.print( (int)BCD2bin(tbuf[4]) ); //s
  
  //day month year
  Serial.print(" ");
  Serial.print( (int)BCD2bin(tbuf[7] ) );
  Serial.print("-");
  Serial.print( (int)BCD2bin(tbuf[6] & 0x1F) );
  Serial.print("-");
  Serial.print( (int)BCD2bin(tbuf[5]) );
  
  uint32_t ut = BCD2bin(tbuf[4]) ; //add secs
  ut+= ((uint32_t)BCD2bin(tbuf[3])) * 60ul; //mins
  ut+= ((uint32_t)BCD2bin(tbuf[2] & 0x3F)) * 60ul * 60ul; //hrs 
  
  
  timestamp();
  Serial.println(F("Time sync DCF77: "));
}


//given an int value, print as decimal to presision places
//so (uint16_t) 65535 with precision 2 = 655.35, does not print (char)32 (spaces)
void ultodstrp(uint16_t val, byte precision) {
  char buff[10];
  for(uint8_t i = 0; i<sizeof(buff); i++) {
     buff[i] = '\0';
  }

  ultoa(val,buff,sizeof(buff));

  uint8_t e = 0;
  for(uint8_t i = 0; i<sizeof(buff);i++) {
     if( buff[i] == '\0') {
        e = i;
        break;
     }  
  }

  e = precision <= 0 ? e+1 : (e-precision);

  uint8_t i=0;    
    while( buff[i] != '\0') {
           if( i == e && i > 0) {
             Serial.print(".");
           }
           Serial.print( buff[i] );
           i++;
    }   
} 

#ifdef LOGDAT
void decodeSensorData(uint8_t fmt, uint8_t* sbuf) {
    char compass[16][4] = {"N  ","NNE", "NE ", "ENE", "E  ", "ESE", "SE ", "SSE", "S  ", "SSW", "SW ", "WSW", "W  ", "WNW", "NW ", "NNW"};
    byte windbearing = 0;

#ifdef MEM_CHECKS
    Serial.print(F("FR2:"));
    Serial.println( freeRam() );
#endif

    // station id
    uint8_t stationid = (sbuf[0] << 4) | (sbuf[1] >>4);
    // temperature
    uint8_t sign = (sbuf[1] >> 3) & 1;
    int16_t temp = ((sbuf[1] & 0x07) << 8) | sbuf[2];
    if (sign) {
      temp = (~temp)+sign;
    }
    int16_t temp_f = (((9.0/5.0)*((float)temp/10.0))+32.0)*10;
    
    //humidity
    uint16_t humidity = sbuf[3] & 0x7F;
    //wind
    #ifdef UNITS_METRIC
    uint16_t windspeed = ((float)sbuf[4] * 0.34)*10;
    uint16_t windgust = ((float)sbuf[5] * 0.34)*10;
    #endif
    #ifdef UNITS_IMPERIAL
    uint16_t ws_mph = (((float)sbuf[4] * 0.34) * 2.23693629)*10;
    uint16_t wg_mph = (((float)sbuf[5] * 0.34) * 2.23693629)*10;
    #endif

    //rainfall
    #ifdef UNITS_METRIC
    uint16_t rain = (((float)(((sbuf[6] & 0x0F) << 8) | sbuf[7]) * 0.3)*10.0);
    #endif
    #ifdef UNITS_IMPERIAL
    uint16_t rain_ins = ((((float)(((sbuf[6] & 0x0F) << 8) | sbuf[7]) * 0.3)*0.039370)*10.0);
    #endif 


    if (fmt == MSG_WS4000) {
      //wind bearing
      windbearing = sbuf[8] & 0x0F;
    }

/*     
    uint16_t q = 65535u;
    ultodstrp(q,2);
    Serial.println();
*/

    Serial.print(F("ID: "));
    Serial.print(stationid, DEC);


    Serial.print(F(" ,T="));
    #ifdef UNITS_METRIC
    ultodstrp(temp,1);
    Serial.print(F("`C "));
    #endif
    #ifdef UNITS_IMPERIAL
    ultodstrp(temp_f,1);
    Serial.print(F("`F "));
    #endif
    
    Serial.print(F(", relH="));
    ultodstrp(humidity,0);

    Serial.print(F("%, Wvel="));
    #ifdef UNITS_METRIC
    ultodstrp(windspeed,1);
    Serial.print(F("m/s "));
    #endif
    #ifdef UNITS_IMPERIAL
    ultodstrp(ws_mph,1);
    Serial.print(F("mph "));
    #endif


    Serial.print(F(", Wmax="));
    #ifdef UNITS_METRIC
    ultodstrp(windgust,1);
    Serial.print(F("m/s "));
    #endif
    #ifdef UNITS_IMPERIAL
    ultodstrp(wg_mph,1);
    Serial.print(F("mph "));
    #endif

    if (fmt == MSG_WS4000) {
      Serial.print(F(", Wdir="));
      int b = (int)windbearing;
      char dir[4];
      strcpy(dir,compass[b]);
      char *s = strchr( dir, ' ');
      if( *s != NULL) {
        s[0] = '\0';
      }
      Serial.print( dir );
    }
    
    Serial.print(F(", Rain="));
    #ifdef UNITS_METRIC
    ultodstrp(rain,1);
    Serial.print(F("mm "));    
    #endif
    #ifdef UNITS_IMPERIAL
    ultodstrp(rain_ins,1);
    Serial.print(F("in "));    
    #endif
    
    //char str[10]; //str[110];
    //str[0] = 0;
    //if (fmt == MSG_WS4000) {
      //snprintf(str,sizeof(str),"ID: %2X, T=%5s`C, relH=%3d%%, Wvel=%5sm/s, Wmax=%5sm/s, Wdir=%3s, Rain=%6smm",
      //        stationid,
      //        tstr,
      //        humidity,
      //        wsstr,
      //        wgstr,
      //        compass[windbearing],
      //        rstr);
    //}
    //if (fmt == MSG_WS3000) {
      //snprintf(str,sizeof(str),"ID: %2X, T=%5s`C, relH=%3d%%, Wvel=%5sm/s, Wmax=%5sm/s, Rain=%6smm",
      //        stationid,
      //        tstr,
      //        humidity,
      //        wsstr,
      //        wgstr,
      //        rstr);
    //}
    Serial.println();
#ifdef MEM_CHECKS
    Serial.println(F("FR2<:"));
#endif
}

#endif
