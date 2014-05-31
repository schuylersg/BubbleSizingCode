//Bubble Sizing Code 
//Version 1.2

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

const unsigned long MAX_TIMEOUT = 10000000;  //5 seconds
const uint8_t MAX_NUMBER_BUBBLES = 8;  //The maximum number of bubbles that can be
//stored at the same time.

const uint16_t MIN_THRESHOLD = 20;	//The minimum change in value required
//for detection.

const uint8_t NOISE_MULTIPLILER = 2;  //The amount to multiply the difference
//betwen max/min backgound readings
//for estimate of backgournd noise threshold

const uint8_t BKGD_STORAGE = 50;    //Store every 1000th adc reading in background 
//readings (helps with slow-moving bubbles)

const uint8_t FUNNEL_AREA = 6.5;  //square mm area of funnel flow path

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
unsigned long detTwoStartTime [MAX_NUMBER_BUBBLES];
unsigned long detThreeStartTime [MAX_NUMBER_BUBBLES];
unsigned long detTwoEndTime [MAX_NUMBER_BUBBLES];
unsigned long detThreeEndTime [MAX_NUMBER_BUBBLES];

unsigned long timeoutClockStart = 0;

//Structures defined in BubbleDetector.h file 
//They store estimates of background readings 
//and noise
backgrounddata detOneBkgd;
backgrounddata detTwoBkgd;
backgrounddata detThreeBkgd;

uint8_t detTwoNumBubbles;
uint8_t detThreeNumBubbles;

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
  //initialize pins
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
}

void loop(){

  //Create loop that checks if a bubble has entered
  //If it hasn't, go to sleep until woken by timer 

  detOneState = LOOK_FOR_START;

  while(detOneState == LOOK_FOR_START){

    //Put sleep function here and removed delay
    delay(5);   

    //Take ADC reading 
    digitalWriteFast(ledOne, HIGH);   //Turn on the LED
    delayMicroseconds(100);   //delay necessary to allow phototransistor to catch up
    adcReading = analogRead(detOne);  //Read the ADC
    digitalWriteFast(ledOne, LOW);    //Turn off the LED
    //Serial.println(adcReading);
    //Serial.println(MIN_THRESHOLD);
    //Serial.println(NOISE_MULTIPLILER);
    //Compare ADC reading to background readings
    detOneState = CheckForBubbleStart(&detOneBkgd, adcReading);
  }


 // Serial.println("det");  //indicate when bubble has been detected
  /*********************************************************
   * At this point a bubble has been detected entering the tube
   *********************************************************/

  //Take background readings - this will probably take around 2 milliseconds.
  //Zero the previous background totals
  detTwoBkgd.total = 0;
  detThreeBkgd.total = 0;
  digitalWriteFast(ledTwo, HIGH);
  digitalWriteFast(ledThree, HIGH);
  delayMicroseconds(200);
  for(i = 0; i < NUM_BKGD_POINTS; i++){
    //Take ADC reading for detector two
    // digitalWriteFast(ledTwo, HIGH);   //Turn on the LED
    adcReading = analogRead(detTwo);  //Read the ADC
    //Serial.println(adcReading);
    //digitalWriteFast(ledTwo, LOW);    //Turn off the LED
    detTwoBkgd.rb[i] = adcReading;
    //Need to manually update the total here too
    detTwoBkgd.total = detTwoBkgd.total + adcReading; 
    //Serial.print(detTwoBkgd.rb[i]);

    //Take ADC reading for detector three
    // digitalWriteFast(ledThree, HIGH);   //Turn on the LED
    adcReading = analogRead(detThree);  //Read the ADC
    //digitalWriteFast(ledThree, LOW);    //Turn off the LED
    detThreeBkgd.rb[i] = adcReading;
    detThreeBkgd.total = detThreeBkgd.total + adcReading;     
  }

  //Update the background inofrmation for each detector
  //This calculates new max, min, and detectionvalue values
  UpdateBkgd(&detTwoBkgd);
  UpdateBkgd(&detThreeBkgd);

  /**********************************************************************
   *Now we are ready to start looking for bubbles moving past the detector
   **********************************************************************/

  numBubblesInTube = 1;  //Initially set to one because the first bubble has been detected 

  //Initialize detTwo and detThree state and the number
  //of bubbles they've seen
  detTwoState = LOOK_FOR_START;
  detThreeState = LOOK_FOR_START;
  detTwoNumBubbles = 0;
  detThreeNumBubbles = 0;

  //Get the current time so that we know when to timeout the operation of looking for a bubble
  timeoutClockStart = micros();
  tempTime = timeoutClockStart;
  digitalWriteFast(ledOne, HIGH);
  while ((numBubblesInTube > 0) && (tempTime - timeoutClockStart < MAX_TIMEOUT)) {
    //Take ADC reading from detector 1 - this is to make sure a new bubble hasn't entered
    //We only really care about detecting the start of a new bubble
    //digitalWriteFast(ledOne, HIGH);   //Turn on the LED
    adcReading = analogRead(detOne);  //Read the ADC
    //digitalWriteFast(ledOne, LOW);    //Turn off the LED

    nextState = CheckForBubble(&detOneBkgd, adcReading, detOneState);
    if(detOneState == LOOK_FOR_START && nextState == LOOK_FOR_END){
      numBubblesInTube += 1; //there is a new bubble in the tube
    }
    detOneState = nextState;

    // * Take a measurement at detector two
    //  At this detector we care about capturing both bubble edges
    tempTime = micros();   //record current time
    //If we are currently looking for the start of a bubble, save the time
    if(detTwoState == LOOK_FOR_START){
      detTwoStartTime[detTwoNumBubbles] = tempTime;
    }
    //toggle led and take sensor reading
    //digitalWriteFast(ledTwo, HIGH);   //Turn on the LED
    adcReading = analogRead(detTwo);  //Read the ADC
    //digitalWriteFast(ledTwo, LOW);    //Turn off the LED
    nextState = CheckForBubble(&detTwoBkgd, adcReading, detTwoState); 

    //set up mechanism to measure bubble detection times
    if(nextState = LOOK_FOR_END && nextState == LOOK_FOR_END){
      digitalWriteFast(debugger2, HIGH); //turn on debugging pin to check times
    } 
    //if the end of a bubble has occured store the current time
    if(detTwoState == LOOK_FOR_END && nextState == LOOK_FOR_START){
      detTwoEndTime [detTwoNumBubbles] = tempTime;
      detTwoNumBubbles += 1;
      digitalWriteFast(debugger2, LOW);

    }



    detTwoState = nextState;

    //Take a measurement for detector three.
    //This is almost the same as detector two.
    tempTime = micros(); 
    if(detThreeState == LOOK_FOR_START){
      detThreeStartTime[detThreeNumBubbles] = tempTime;
    }
    //digitalWriteFast(ledThree, HIGH);   //Turn on the LED
    adcReading = analogRead(detThree);  //Read the ADC
    //digitalWriteFast(ledThree, LOW);    //Turn off the LED
    nextState = CheckForBubble(&detThreeBkgd, adcReading, detThreeState);  
    //Serial.println(nextState);
    //if the end of a bubble has occured store the current time
    //and decrease the number of bubbles in the tube
    if(nextState = LOOK_FOR_END && nextState == LOOK_FOR_END){
      digitalWriteFast(debugger3, HIGH); //turn on debugging pin to check times
    } 

    if(detThreeState == LOOK_FOR_END && nextState == LOOK_FOR_START){
      detThreeEndTime[detThreeNumBubbles] = tempTime;
      detThreeNumBubbles += 1;
      numBubblesInTube -= 1;
      digitalWriteFast(debugger3, LOW); //turn off debugging pin to check times
    }
    detThreeState = nextState;

    //error checking to make sure we don't overflow the max number of bubbles
    //that can be counted
    if(detTwoNumBubbles == MAX_NUMBER_BUBBLES || detThreeNumBubbles == MAX_NUMBER_BUBBLES){
      timeoutClockStart = 0;  //this will cause while loop to exit
    }
  }
  digitalWriteFast(ledOne, LOW);
  digitalWriteFast(ledTwo, LOW);
  digitalWriteFast(ledThree, LOW);

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
 * This method checks to see if a bubble is being detected
 * If no bubble is detected, it updates the variables in backgrounddata
 * to reflect the most recent measurements
 ******************************************************************/
uint8_t CheckForBubbleStart(backgrounddata* bkgd, uint16_t newValue){
  //Serial.println(bkgd->detectionvalue);
  //for(uint8_t j = 0; j<8; j++){
  //Serial.print(bkgd->rb[j]);
  //Serial.print(" "); 
  //}
  if(newValue < bkgd->detectionvalue){
    return LOOK_FOR_END;    // bubble detected, so immediately return
  }

  //update the total value
  bkgd->total = bkgd->total - bkgd->rb[bkgd->pos] + newValue;
  //store the new value
  bkgd->rb[bkgd->pos] = newValue;
  //update the pos value
  bkgd->pos = bkgd->pos + 1;
  if(bkgd->pos == NUM_BKGD_POINTS){
    bkgd->pos = 0;
  }

  //call update background to find new min, max, and detection levels
  UpdateBkgd(bkgd);

  return LOOK_FOR_START;
}

/******************************************************************
 * This method checks to see if a bubble is no longer being detected
 ******************************************************************/
uint8_t CheckForBubble(backgrounddata* bkgd, uint16_t newValue, uint8_t state){

  //add hysterisis so detectors can see end of slow-moving bubbles
  if(state == LOOK_FOR_START){
    if(newValue < bkgd->detectionvalue){
      return LOOK_FOR_END;    // bubble detected

    }
    else{
      return LOOK_FOR_START;
    }
  }
  else{
    //0.02 factor for detectionvalue chosen by trial-error, may change later
    if(newValue > bkgd->detectionvalue + 0.02*bkgd->detectionvalue){
      //Serial.print(0.01*bkgd->detectionvalue);
      return LOOK_FOR_START;
      //digitalWriteFast(debugger, LOW);
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
void UpdateBkgd(backgrounddata * bkgd){
  //Update max/min and total values of ringbuffer 
  bkgd->maxvalue = 0;
  bkgd->minvalue = 1024;  //maximum value of 10 bit adc
  uint16_t bkgd_counter = 0;      //initialize counter to 0
  //only store some adc readings, helps with slow bubbles
  while(bkgd_counter < BKGD_STORAGE){
    bkgd_counter = bkgd_counter + 1;  //increase counter by 1 and exit loop
    // Serial.print("counter");
    //Serial.println(bkgd_counter);
  }

  for (uint8_t j = 0; j < NUM_BKGD_POINTS; j++){

    //bkgd_counter = 0;   //re-set counter to 0
    //Serial.println(bkgd_counter);
    if(bkgd->rb[j] > bkgd->maxvalue){
      bkgd->maxvalue = bkgd->rb[j];
    }
    if(bkgd->rb[j] < bkgd->minvalue){
      bkgd->minvalue = bkgd->rb[j];
    }

    //Pick the more conservative noise threshold
    uint16_t noiseThreshold = max((bkgd->maxvalue - bkgd->minvalue) * NOISE_MULTIPLILER, 
    MIN_THRESHOLD);  //the multiply by 4 is made up

    //if the threshold is greater than the average value, then detection is impossible
    if((bkgd->total/NUM_BKGD_POINTS) < noiseThreshold){
      bkgd->detectionvalue = 0;
    }
    else{
      //pick the more conservative detection value
      bkgd->detectionvalue = (bkgd->total/NUM_BKGD_POINTS) - noiseThreshold;
    }
//    delay(5);
    // Serial.println("detectionvalue");
    //Serial.println(bkgd->detectionvalue);
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
  UpdateBkgd(&detOneBkgd);
  UpdateBkgd(&detTwoBkgd);
  UpdateBkgd(&detThreeBkgd);
}








