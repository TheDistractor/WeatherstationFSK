
//All of the LOGxxx #defines below are related to serial output of data, so if you are
//short on space, comment them out. This is probably more relevant to
//those using the ATTINY MCU's like ATTINY85, 84 etc
//As it currently stands an 8K ATTINY can use this code with the ALL the LOGxxx available
//(using TinyDebugSerial on the relevant PIN)
//I am hopeful that I can also re-add a small timestamp clock back shortly when I get time (pun ;)
//hence the allocated memory for some clock related variables.

//My aim is to keep this script under 8K as for general weatherstation relaying
//you dont really need a 328 cpu, as a tiny84/85 suffice with a few spare pins
//if really needed.
//I also considered bmp085 support on 84, but I figured I could easily press 
//another 84/84 into service to do this task.

//Define the printed output
//#define LOGRAW 1 //comment = disable, uncomment = enable logging received package group_id 212 (0xD4)
#define LOGPKT 1 //comment = disable, uncomment = enable logging unique package passing crc
#define LOGDCF 1 //comment = disable, uncomment = enable updating time and logging of DCF77 values
#define LOGDAT 1 //comment = disable, uncomment = enable logging of parsed/decoded formatted sensor data


#define SERIAL_BAUD 38400  //38400 tinydebug  //57600 jeenode

#define FWD_PKT 1  //do we fwd data packets onto another network using the data below
#define FWD_DCF 1  //do we fwd DCF77 packets as above, not a code saver
#define NODE_ID 22 //any node works with modified driver (RF12WS or new jeelib, NYR). Use 31 for unmodified driver.
#define GROUP_ID 5
#define DEST_NODE_ID 1 //1=central node, 0=broadcast


//decoder output constants - you should only need one of these
#define UNITS_METRIC 1    //decoded data in metric (m/s, 'C) etc
#define UNITS_IMPERIAL 1  //decoded data in imperial (mph, 'F) etc

//only really neded for debugging
#define MEM_CHECKS 1 //print free mem as 3 strategic points
#define LOOP_ACTIVITY 1 //print dot to console every n loops
#define LED_PIN     3   // activity LED, comment out to remove the led debugging.

//save some more space
#ifdef LOGRAW  
#undef LOOP_ACTIVITY //we dont need LOOP_ACTIVITY if we are logging all inputs as we see things happening regularly
#undef LED_PIN       //we also dont need debug led if we log raw data
#endif

