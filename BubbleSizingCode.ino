//Bubble Sizing Code 
//Version 1.3 - This version is for testing purposes.
//It records timing data for all three detectors

#include <digitalWriteFast.h>
#include <BubbleDetector.h>

/*************************************************
 * PIN DECLARATIONS
 *************************************************/
const uint8_t ledOne = 2;
const uint8_t ledTwo = 3;
const uint8_t ledThree = 4;
const uint8_t debugger2 = 5;   //use this pin to debug - timing in concordence with oscilloscope
const uint8_t debugger3 = 6;   //use this pin to debug - timing in concordence with oscilloscope

const uint8_t detOne = A0;
const uint8_t detTwo = A1;
const uint8_t detThree = A2;

// I think A4 and A5 are needed for communication with memory chip

/*************************************************
 * GLOBAL CONSTANTS
 *************************************************/
const uint8_t LOOK_FOR_START = 0;
const uint8_t LOOK_FOR_END = 1;

const unsigned long MAX_TIMEOUT = 10000000;  //10 seconds
const uint8_t MAX_NUMBER_BUBBLES = 8;  //The maximum number of bubbles that can be
//stored at the same time.

const uint16_t MIN_THRESHOLD = 20;	//The minimum change in value required
//for detection.

const uint8_t NOISE_MULTIPLILER = 2;  //The amount to multiply the difference
//betwen max/min backgound readings
//for estimate of backgournd noise threshold

const uint16_t BKGD_STORAGE = 1000;    //Store every BKGD_STORAGE adc reading in background 
//readings (helps with slow-moving bubbles) - 1000 should mean approximately 1 every second

const float FUNNEL_AREA = 6.5;  //square mm area of funnel flow path

/*************************************************
 * GLOBAL VARIABLE DECLARATIONS
 *************************************************/

//Each of the detectors has a state variable.
//The detector is always either waiting to see the start 
//of a bubble or waiting to see the end end of a bubble
uint8_t detOneState = LOOK_FOR_START;
uint8_t detTwoState = LOOK_FOR_START;
uint8_t detThreeState = LOOK_FOR_START;

//Arrays for keeping track of the start and end times for the
//bubbles passing the detectors
//An array of 8 means that up to 8 bubbles can be recorded in
//a row (e.g. in the tube at the same time)
unsigned long detOneStartTime [MAX_NUMBER_BUBBLES];
unsigned long detTwoStartTime [MAX_NUMBER_BUBBLES];
unsigned long detThreeStartTime [MAX_NUMBER_BUBBLES];
unsigned long detOneEndTime [MAX_NUMBER_BUBBLES];
unsigned long detTwoEndTime [MAX_NUMBER_BUBBLES];
unsigned long detThreeEndTime [MAX_NUMBER_BUBBLES];

unsigned long timeoutClockStart = 0;

//Structures defined in BubbleDetector.h file 
//They store estimates of background readings 
//and noise
backgrounddata detOneBkgd;
backgrounddata detTwoBkgd;
backgrounddata detThreeBkgd;

uint8_t detOneNumBubbles;
uint8_t detTwoNumBubbles;
uint8_t detThreeNumBubbles;

uint16_t bkgdCounter;

/*************************************************
 * HELPER VARIABLES - these are generic variables but
 * declaring them here give better estimate of RAM usage
 *************************************************/
uint16_t adcReading = 0;
uint8_t i;  //use for counting in loops
uint8_t numBubblesInTube;
unsigned long tempTime;  //used to temporarily store micros() readings
uint8_t nextState;   

/*************************************************
 * PERFROM SETUP TASKS
 *************************************************/
void setup(){
  // Initialize pins
  // It might be, that for power reasons these should be kept as inputs until they need
  // to be used as outputs - check documentation
  pinMode(ledOne, OUTPUT);
  pinMode(ledTwo, OUTPUT);
  pinMode(ledThree, OUTPUT);
  pinMode(debugger2, OUTPUT);
  pinMode(debugger3, OUTPUT);

  pinMode(detOne, INPUT);
  pinMode(detTwo, INPUT);
  pinMode(detThree, INPUT);

  //setup serial for debug purposes
  Serial.begin(115200);

  //Check if this is correct!!!
  analogReference(INTERNAL);

  //Initialize SPI for communication with memory chip
  //TODO: think of how to implement way to store data without overwriting
  //    - maybe just look for some special character

  //Set ADC to fastest read speed allowed for resolution
  //This is an optimzation that can be done once basic code is tested

  //Maybe Set up timer here for sleeping/waking
  //Must use either timer1 or timer2 - timer2 is probably preferred

  //Initialize background data structs.
  InitializeBkgdStructs();
  
  bkgdCounter = 0;  //This counter increments every time detector 1 does not sense a bubble. 
                    //When it is equal to 
  
  //Turn on all LEDs
  digitalWriteFast(ledOne, HIGH);   //Turn on the LED
  digitalWriteFast(ledTwo, HIGH);   //Turn on the LED
  digitalWriteFast(ledThree, HIGH);   //Turn on the LED
}

void loop(){

  //Create loop that checks if a bubble has entered
  //If it hasn't, go to sleep until woken by timer 

  detOneState = LOOK_FOR_START;

  while(detOneState == LOOK_FOR_START){

    tempTime = micros();   //record current time
    detOneStartTime[0] = tempTime;
    
    adcReading = analogRead(detOne);  //Read the ADC

    //Compare ADC reading to background readings
    
    detOneState = CheckForBubble(&detOneBkgd, adcReading, LOOK_FOR_START);
    
    //Check if it's time to store a background reading - but only do this 
    //no bubble was detected
    if(bkgdCounter >= BKGD_STORAGE && detOneState == LOOK_FOR_START){
      UpdateBkgd(&detOneBkgd, adcReading);  //Update background data struct
      adcReading = analogRead(detTwo);      //Read the ADC
      UpdateBkgd(&detTwoBkgd, adcReading);       
      adcReading = analogRead(detThree);      //Read the ADC
      UpdateBkgd(&detThreeBkgd, adcReading);

      //Reset background counter
      bkgdCounter = 0;
    }
    
    //Increment background counter
    bkgdCounter = bkgdCounter + 1;

  }

  /*********************************************************
   * At this point a bubble has been detected entering the tube
   *********************************************************/
  
  // Serial.println("det");  //indicate when bubble has been detected
    
  numBubblesInTube = 1;  //Initially set to one because the first bubble has been detected 

  //Initialize detTwo and detThree state and the number
  //of bubbles they've seen
  detTwoState = LOOK_FOR_START;
  detThreeState = LOOK_FOR_START;
  detOneNumBubbles = 0;
  detTwoNumBubbles = 0;
  detThreeNumBubbles = 0;

  //Get the current time so that we know when to timeout the operation of looking for a bubble
  timeoutClockStart = micros();
  tempTime = timeoutClockStart;
  while ((numBubblesInTube > 0) && (tempTime - timeoutClockStart < MAX_TIMEOUT)) {
    //Take ADC reading from detector 1 - this is to make sure a new bubble hasn't entered
    //We only really care about detecting the start of a new bubble
    tempTime = micros();   //record current time
    //If we are currently looking for the start of a bubble, save the time
    if(detOneState == LOOK_FOR_START){
      detOneStartTime[detOneNumBubbles] = tempTime;
    }
    adcReading = analogRead(detOne);  //Read the ADC
    
    nextState = CheckForBubble(&detOneBkgd, adcReading, detOneState);
    if(detOneState == LOOK_FOR_START && nextState == LOOK_FOR_END){
      numBubblesInTube += 1; //there is a new bubble in the tube  
    } else if(detOneState == LOOK_FOR_END && nextState == LOOK_FOR_START){
      detOneEndTime [detOneNumBubbles] = tempTime;
      detOneNumBubbles += 1;
    }
    detOneState = nextState;

    // * Take a measurement at detector two
    //  At this detector we care about capturing both bubble edges
    tempTime = micros();   //record current time
    //If we are currently looking for the start of a bubble, save the time
    if(detTwoState == LOOK_FOR_START){
      detTwoStartTime[detTwoNumBubbles] = tempTime;
    }
    adcReading = analogRead(detTwo);  //Read the ADC
    nextState = CheckForBubble(&detTwoBkgd, adcReading, detTwoState); 

    //if the end of a bubble has occured store the current time
    if(detTwoState == LOOK_FOR_END && nextState == LOOK_FOR_START){
      detTwoEndTime [detTwoNumBubbles] = tempTime;
      detTwoNumBubbles += 1;
    }
    detTwoState = nextState;

    //Take a measurement for detector three.
    //This is almost the same as detector two.
    tempTime = micros(); 
    if(detThreeState == LOOK_FOR_START){
      detThreeStartTime[detThreeNumBubbles] = tempTime;
    }
    adcReading = analogRead(detThree);  //Read the ADC
    nextState = CheckForBubble(&detThreeBkgd, adcReading, detThreeState);  
    //if the end of a bubble has occured store the current time
    //and decrease the number of bubbles in the tube
    
    if(detThreeState == LOOK_FOR_END && nextState == LOOK_FOR_START){
      detThreeEndTime[detThreeNumBubbles] = tempTime;
      detThreeNumBubbles += 1;
      numBubblesInTube -= 1;
    }
    detThreeState = nextState;

    //error checking to make sure we don't overflow the max number of bubbles
    //that can be counted
    if(detTwoNumBubbles == MAX_NUMBER_BUBBLES || detThreeNumBubbles == MAX_NUMBER_BUBBLES){
      timeoutClockStart = 0;  //this will cause while loop to exit
    }
  }
  //No more bubbles in tube, so save the data we have
  //or transmit if in debug mode.

  //For debug, print if timeout error.
  if((tempTime - timeoutClockStart > MAX_TIMEOUT)){
    Serial.println("TO ERROR");
  }

  //For debug, check to make sure that each detector actually 
  //saw the end of the bubble 
  if(detOneState == LOOK_FOR_END){
    Serial.println("D_ONE ERROR");
    detOneState = LOOK_FOR_START;
  }

  if(detTwoState == LOOK_FOR_END){
    Serial.println("D_TWO ERROR");
  }

  if(detThreeState == LOOK_FOR_END){
    Serial.println("D_THREE ERROR");
  }

  //For debug, make sure detector 2 and 3 sense the same
  //number of bubbles
  if(detTwoNumBubbles != detThreeNumBubbles){
    Serial.print("NUM_ERROR: ");
    Serial.print(detTwoNumBubbles);
    Serial.print(", ");
    Serial.print(detThreeNumBubbles);
  }

  //Print out the bubble information - this is where the code
  //to actually save the bubbles should go
  for(int i = 0; i < detThreeNumBubbles; i++){
    Serial.print("Bubble #");
    Serial.println(i);
    Serial.print(detTwoStartTime[i]);
    Serial.print(", ");
    Serial.print(detTwoEndTime[i]); 
    Serial.print(" : ");
    Serial.print(detThreeStartTime[i]);
    Serial.print(", ");
    Serial.println(detThreeEndTime[i]);
    Serial.print("DetectorTime: ");
    Serial.print(detTwoEndTime[i] - detTwoStartTime[i]);
    Serial.print(", ");
    Serial.println(detThreeEndTime[i] - detThreeStartTime[i]);
    Serial.print("RiseTime (ms): ");
    Serial.print(0.001*detThreeStartTime[i] - 0.001*detTwoStartTime[i]);
    Serial.print(", ");
    Serial.println(0.001*detThreeEndTime[i] - 0.001*detTwoEndTime[i]);
    Serial.print("NumBubs: ");
    Serial.println(numBubblesInTube);
    //calculate rise velocity for det 2 and 3
    Serial.print("RiseVelocity_mm/sec: ");
    Serial.print(5.08 * 1000000 / (detThreeStartTime[i] - detTwoStartTime[i]));
    Serial.print(", ");
    Serial.println(5.08 * 1000000 / (detThreeEndTime[i] - detTwoEndTime[i]));
    Serial.print("BubbleVolume_ml: ");
    Serial.print((.00508  / (detThreeStartTime[i] - detTwoStartTime[i])) * FUNNEL_AREA * (detTwoEndTime[i] - detTwoStartTime[i]));
    Serial.print(",");
    Serial.print((.00508 / (detThreeStartTime[i] - detTwoStartTime[i])) * FUNNEL_AREA * (detThreeEndTime[i] - detThreeStartTime[i]));
    Serial.print(",");
    Serial.print((.00508  / (detThreeEndTime[i] - detTwoEndTime[i])) * FUNNEL_AREA * (detTwoEndTime[i] - detTwoStartTime[i]));
    Serial.print(",");
    Serial.print((.00508 / (detThreeEndTime[i] - detTwoEndTime[i])) * FUNNEL_AREA * (detThreeEndTime[i] - detThreeStartTime[i]));
    Serial.println();

    Serial.print("BubbleDiameter_mm: ");
    Serial.print((5.08 / (detThreeStartTime[i] - detTwoStartTime[i])) * (detTwoEndTime[i] - detTwoStartTime[i]));
    Serial.print(", ");
    Serial.print((5.08 / (detThreeStartTime[i] - detTwoStartTime[i])) * (detThreeEndTime[i] - detThreeStartTime[i]));
    Serial.print(", ");
    Serial.print((5.08 / (detThreeEndTime[i] - detTwoEndTime[i])) * (detTwoEndTime[i] - detTwoStartTime[i]));
    Serial.print(", ");
    Serial.println((5.08 / (detThreeEndTime[i] - detTwoEndTime[i])) * (detThreeEndTime[i] - detThreeStartTime[i]));
    Serial.println();
  }
}  // end of loop()


/******************************************************************
 * This method checks to see if a bubble is no longer being detected
 * The value to check for is dependent on whether the detector is 
 * looking for a bubble to start or to end. 
 ******************************************************************/
uint8_t CheckForBubble(backgrounddata* bkgd, uint16_t newValue, uint8_t state){

  if(state == LOOK_FOR_START){
    if(newValue < bkgd->startdetvalue){
      return LOOK_FOR_END;    // bubble detected
    }
    else{
      return LOOK_FOR_START;
    }
  }
  else{
    if(newValue > bkgd->enddetvalue){
      return LOOK_FOR_START;
    }
    else{
      return LOOK_FOR_END;
    }
  }
}

/******************************************************************
 * This method updates the max, min, and detectionvalue of the 
 * backgrounddata struct
 ******************************************************************/
void UpdateBkgd(backgrounddata * bkgd, uint16_t newValue){
  
  //update the total value
  bkgd->total = bkgd->total - bkgd->rb[bkgd->pos] + newValue;
  //store the new value
  bkgd->rb[bkgd->pos] = newValue;
  //update the pos value
  bkgd->pos = bkgd->pos + 1;
  if(bkgd->pos == NUM_BKGD_POINTS){
    bkgd->pos = 0;
  }

  //Update max/min and total values of ringbuffer 
  bkgd->maxvalue = 0;
  bkgd->minvalue = 1024;  //maximum value of 10 bit adc

  for (uint8_t j = 0; j < NUM_BKGD_POINTS; j++){
    if(bkgd->rb[j] > bkgd->maxvalue){
      bkgd->maxvalue = bkgd->rb[j];
    }
    if(bkgd->rb[j] < bkgd->minvalue){
      bkgd->minvalue = bkgd->rb[j];
    }

    //Pick the more conservative noise threshold
    uint16_t noiseThreshold = max((bkgd->maxvalue - bkgd->minvalue) * NOISE_MULTIPLILER, 
    MIN_THRESHOLD);  

    //if the threshold is greater than the average value, then detection is impossible
    if((bkgd->total/NUM_BKGD_POINTS) < noiseThreshold){
      bkgd->startdetvalue = 0;
      bkgd->enddetvalue = 1023; //2^10-1
    }
    else{
      //pick the more conservative detection value
      bkgd->startdetvalue = (bkgd->total/NUM_BKGD_POINTS) - noiseThreshold;
      
      //The enddetvalue is arbitrarily larger than the startdetvalue by half the noise threshold.
      //This should add significant hysteresis so that a bubble insn't instantly detected and then "undetected"
      bkgd->enddetvalue = bkgd->startdetvalue + (noiseThreshold/2);
    }
  }
}

/******************************************************************
 * This method initializes the backgrounddata structs used in the 
 * program and must be called in setup(). 
 ******************************************************************/
void InitializeBkgdStructs(){
  for (i = 0; i < NUM_BKGD_POINTS; i++){
    detOneBkgd.rb[i] = 0;
    detTwoBkgd.rb[i] = 0;
    detThreeBkgd.rb[i] = 0; 
  }
  detOneBkgd.pos = 0;
  detTwoBkgd.pos = 0;
  detThreeBkgd.pos = 0;
  UpdateBkgd(&detOneBkgd, 0);
  UpdateBkgd(&detTwoBkgd, 0);
  UpdateBkgd(&detThreeBkgd, 0);
}








