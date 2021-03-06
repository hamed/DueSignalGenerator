/**
   @file
   @author  Hamed Seyed-allaei <hamed@ipm.ir>
   @version 1.0

   @section LICENSE

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details at
   http://www.gnu.org/copyleft/gpl.html
*/


/****************************************************************
      YOU DON'T NEED TO CHANGE THIS FILE FOR DAY TO DAY USES
 ****************************************************************/


/**
   Sampling Rate

   rc sets the sampling rates. Sampling Rate = 42MHz / rc.
   Given rc = 5*42; the sampling rate will be 200KHz.
   The theoretical limit is 1MHz (The maximum frequency of ADC in arduino due),
   The practical limit probably is less,
   because you can do only 84 operation for each sample at best,
   and, this program needs to send 4 bytes to computer for every sample.
*/
unsigned int rc; // = 42*5;       // 200KHz  the sampling rate will be: 42000000/rc
unsigned int samplingRate = 100000;
// the above variable are set from the host program.
// no need to change them here.

/**
   Number of Samples

   total number of samples to be recorded.
   2^22, four million samples, should give you a decent result.
   at the current rate, 200K sample per second, it will take 20 seconds.
*/
unsigned long nSamples = 1 << 22; // 1<<22 means 2^22 in alien language :-)


/**
   Buffer sizes, probably you don't need to touch it.

   We have two buffers, one for DAC, and one for ADC.
   Each buffer is divided to NUMBER_OF_BUFFERS buffers
   of BUFFER_SIZE of 32 bits integers.
   That is, NUMBER_OF_BUFFERS * BUFFER_SIZE * 4 bytes.
   In this way, we can be sure each task has its own dedicated buffer.
   In some part of USB codes of arduino,
   I saw a 512 bytes, so I am going to set BUFFER_SIZE = 128,
   (The best reasoning you have ever heard!).
   NUMBER_OF_BUFFERS, must be a power of 2, like 4, 8, 16, 32 or probably 64.
   I choose 32, for NUM_OF_BUF, so we take 32KB of arduino's memory.
   I guess, it is fine to raise it to 64.
   Every time we going to address a buffer we will do it in this way:
   dacBuffer[dacIndex & divider] That's why NUMBER_OF_BUFFERS must be power of 2.
*/

#define BUFFER_SIZE 128
#define NUMBER_OF_BUFFERS 4
/* indexes to access dac buffer and adc buffer,
   each process, DAC, ADC, TRNG has its own index.
   USB has two index, one for each buffer.
*/
unsigned long dacIndex = 0, trngIndex = 0, trngidx = 0;
unsigned short dacBuffer[NUMBER_OF_BUFFERS][BUFFER_SIZE];
float wavetable[BUFFER_SIZE];
const unsigned long divider = NUMBER_OF_BUFFERS - 1;

#include <Scheduler.h>
#include <math.h>
void setup() {
  Serial.begin(9600);  // To print debugging messages.

  /************************************************
     Turning devices on.
  */
  //  pmc_enable_periph_clk(ID_TC0);
  pmc_enable_periph_clk(ID_TC1);
  //pmc_enable_periph_clk(ID_TRNG);
  pmc_enable_periph_clk(ID_DACC);


  // It is good to have the timer 0 on PIN2, good for Debugging
  // You can connect it to oscilloscope or LED.
  int result = PIO_Configure( PIOB, PIO_PERIPH_B, PIO_PB25B_TIOA0, PIO_DEFAULT);

  /************************************************
     Scheduler

     I love this library!
     There are 2 loops:
     loopDAC sends DAC data to PC over USB.
  */


  for (int i = 0; i < BUFFER_SIZE; i++) {
    wavetable[i] = sin((2*PI * i) / BUFFER_SIZE);
  }

  dacIndex = 0; trngidx = 0; trngIndex = 0;
  Scheduler.startLoop(loopDAC);
}


/* Number of samples to be written to DAC and read from .
   0 means there is no sample left.
   -1 means that, no sample left and interrupts and clocks are stopped.
*/
signed long nDACSamples = -1;

void loopDAC() {

  if ((trngIndex - dacIndex) <= divider) { // TRNG! Stop! DAC is using dacBuffer[dacIndex & divider], dont mess with it.
    for(int i=0; i<BUFFER_SIZE; i++){
      dacBuffer[trngIndex & divider][i] =  0x7FF * wavetable[i]+0x800; //0x7FF * wavetable[i]+0x800;
    }
//    dacBuffer[0][0] =  0x0;
//    dacBuffer[0][1] =  0xFFF;
    trngIndex++;
  }
  yield();  // Ok, we are done.
}

// Scheduler is cool, isn't it?


void loop() {
  /************************************************
    Timer Counter

    We set 2 separate timers for each DAC and ADC devices: Channel 0 and 1 of TC0.
    This timers are in the same block and synchronized. But their output signal is
    a bit different (TIOA0 and TIOA0).
    TIOA1 controls DAC.
    TIOA0 controls ADC.
    ADC is half sampling time late, Because we want to measure the center of the signal,
    generated by DAC.
  */
  TC_Configure(TC0, 1, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_ACPA_SET | TC_CMR_ACPC_CLEAR | TC_CMR_ASWTRG_CLEAR | TC_CMR_TCCLKS_TIMER_CLOCK1);
  //  TC_Configure(TC0, 0, TC_CMR_WAVE|TC_CMR_WAVSEL_UP_RC|TC_CMR_ACPA_CLEAR|TC_CMR_ACPC_SET|TC_CMR_ASWTRG_CLEAR|TC_CMR_TCCLKS_TIMER_CLOCK1);

  /************************************************
     Digital to Analog Convertor

     DAC, its DMA and its Interrupts are set here.
     I use pin DAC1 for output.
     I think I destroyed DAC0 while I was experimenting :-(
     You are better to take care of this fragile beast!
  */
  dacc_reset(DACC);                 // Reset DACC registers
  dacc_set_writeprotect(DACC, 0);
  dacc_set_transfer_mode(DACC, 1);  // Full word transfer mode.
  dacc_set_power_save(DACC, 0, 0);  // sleep mode-0 (disabled), fast wakeup-0 (disabled)
  dacc_set_timing(DACC, 0x08, 1, DACC_MR_STARTUP_0); // refresh - 0x08 (1024*8 dacc clocks), max speed mode - 0 (disabled), startup time   - 0x10 (1024 dacc clocks)
  dacc_set_analog_control(DACC, DACC_ACR_IBCTLCH0(0x02) | DACC_ACR_IBCTLCH1(0x02) | DACC_ACR_IBCTLDACCORE(0x01)); // Setting currents, I don't know much about it! any comment or helps is appereciated.
  dacc_set_channel_selection(DACC, 1);  // Select Channel 1 of DACC, (I just destroyed my channel 0 so this is my only choice.)
  DACC->DACC_MR |= DACC_MR_TRGEN;       // We want to use trigger.
  DACC->DACC_MR |= DACC_MR_TRGSEL(2);   // This triger is TIOA1
  DACC->DACC_IDR = ~(DACC_IDR_ENDTX);   // Disabling Interrupts.
  DACC->DACC_IER = DACC_IER_ENDTX;      // Enabling Interrupts.
  DACC->DACC_PTCR = DACC_PTCR_TXTEN | DACC_PTCR_RXTDIS;
  DACC->DACC_CHER = 2;  // enable channel 1. for channel 0 use 1

  rc = 42000000 / samplingRate;

  /*
     Timings

     To see the following figure properly, you should use mono space font.

    A      C  A      C  A      C  A      C  A      C  A      C  A
    __|______|__|______|__|______|__|______|__|______|__|______|__|_    TC1
     ______    ______    ______    ______    ______    ______    _
    __|      |__|      |__|      |__|      |__|      |__|      |__|     TIOA1
       _________                     _________
      |         |_________          |         |          _________
    ____|                   |_________|         |_________|         |   DAC1
            _         _         _         _         _         _
    _________| |_______| |_______| |_______| |_______| |_______| |___   ADC clock
           |-|<- ADC sample and hold period.
            ____      ____      ____      ____      ____      ____
    _________|    |____|    |____|    |____|    |____|    |____|    |   TIOA0

           C    A    C    A    C    A    C    A    C    A    C    A
    _________|____|____|____|____|____|____|____|____|____|____|____|   TC0
  */

  TC_SetRC(TC0, 1, rc);      // TIOA1 goes LOW  on RC.
  TC_SetRA(TC0, 1, rc / 2 - 25); 
  /* TIOA1 goes HIGH on RA. DAC starts on TIOA rising,
                                and its result is ready after 25 clocks,
                                so we start 25 clocks earlier.
*/



  nDACSamples = nSamples;

  Serial.print("Filling DAC buffers with random numbers ");
  while ((trngIndex - dacIndex) <= divider) {
    delay(1);
    Serial.print('.');
  }
  Serial.println(" done");

  Serial.print("Sending random numbers to host ");
  //while((dacUSBIndex < trngIndex) && nDACUSBSamples) {delay(1); Serial.print('.');}
  Serial.println(" done");

  Serial.print("Setting up DAC DMA buffer ...");
  DACC->DACC_TPR  = (unsigned long) dacBuffer[dacIndex & divider];  // DMA buffer
  DACC->DACC_TCR  = (unsigned int)  BUFFER_SIZE; // DMA buffer counter
  DACC->DACC_TNPR = (unsigned long) dacBuffer[(dacIndex + 1) & divider];  // next DMA buffer
  DACC->DACC_TNCR = (unsigned int)  BUFFER_SIZE; // next DMA buffer counter
  nDACSamples -= 2 * BUFFER_SIZE;
  Serial.println(" done");

  Serial.print("Enabling DACC interrupts ...");
  NVIC_EnableIRQ(DACC_IRQn);
  Serial.println(" done");

  /************************************************
    We did a great job up to now.
    now we rest for a couple of  second and then we start.
  */

  Serial.print("Writing to DAC, reading from ADC and  Sending to host ...");

  TC0->TC_CHANNEL[1].TC_SR;  // removal of this doesn't make any problem. What does this do?
  TC0->TC_CHANNEL[1].TC_CCR = TC_CCR_CLKEN;

  TC0->TC_BCR = TC_BCR_SYNC;  // Start the clocks.   3, 2, 1 and GO!
  while (nDACSamples > -1) {
    yield(); // If you have got something to do, do it.
  }
  Serial.println(" done");

  yield();
}

void DACC_Handler() {
  /**
     This interrupt is called when the next buffer counter is zero.
     That means, we have finished with the current buffer,
     dacIndex points to the current buffer, so we increment it by one,
     so it will point to the new current buffer.
     then we set the value of the next buffer (dacIndex+1).
  */
  unsigned long status =  DACC->DACC_ISR;
  if (status & DACC_ISR_ENDTX) {
    if (nDACSamples > 0) {
      dacIndex++;  // Move dacIndex to the current buffer.
      DACC->DACC_TNPR = (unsigned long) dacBuffer[(dacIndex + 1) & divider]; // Set the next buffer.
      DACC->DACC_TNCR = BUFFER_SIZE/2;  // set the next buffer counter.
      nDACSamples -= BUFFER_SIZE;
    }
    if ((status & DACC_ISR_TXBUFE) && (nDACSamples == 0))  {

      nDACSamples = -1;
      dacIndex += 2;  // Set the buffer index for the next run.
      // and the data will be written up to this
      NVIC_DisableIRQ(DACC_IRQn);  // Disable Interrupt
      NVIC_DisableIRQ(TRNG_IRQn);  // No need to produce random number.
      TC_Stop(TC0, 1);
    }
  }
}
