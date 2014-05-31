/**
Description:	Header file for all constants and definitions for
				bubble detector code.

Author: 		Schuyler Senft-Grupp
Version: 		1.1
Date: 			5/15/2013

**/

#ifndef __BUBBLE_DETECTOR__
#define __BUBBLE_DETECTOR__

#include <Arduino.h>

const uint8_t NUM_BKGD_POINTS = 8;	//the number of background points to store

/*************************************************
Declare structs to hold background measurements for each sensor
These use continuous ring buffers 
*************************************************/
struct backgrounddata{
  uint16_t rb [NUM_BKGD_POINTS]; //ring buffer for data
  uint8_t pos;      		//pos in the ring buffer - 0 to 7
  uint16_t minvalue;  		//minimum value in the ring buffer
  uint16_t maxvalue;  		//maximum value in the ring buffer
  uint32_t total;    		//the sum of all values in the ring buffer - divide by 8 to get avg value
  uint16_t startdetvalue;	//the value required to signify a bubble start event
  uint16_t enddetvalue;         //the value required to signify a bubble end event
};

#endif
