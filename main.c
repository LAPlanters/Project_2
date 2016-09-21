// Include neccessary Libraries
#include "stm32l476xx.h"
#include "SysClock.h"
#include "UART.h"
#include "LED.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>

// Global Variables


// Create proto-type functions
uint8_t init(void);			// Initialize function


int main(void)
{
	// Call Initialize function
	init();
	
}

uint8_t init(void)
{
	System_Clock_Init(); // Switch System Clock = 80 MHz
		
	//Enable timer 2 TIM2, and GPIOA
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;		
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;	
		
	//Set the prescaler value
	TIM2->PSC =  8000; 		
	
	//Create an update event
	TIM2->EGR |= TIM_EGR_UG;
	 
	// Setup Channel 2
	
	// Setup the output capture/compare 2 for PWM mode 1
	TIM2->CCMR1 |= 0x6000;
	
	// Enable the output capture/compare 2
	TIM2->CCMR1 |= 0x800;
	
	// Enable Channel 2
	TIM2->CCER |= 0x10;
	
	// Set up channel 1

	// Setup the output capture/compare 2 for PWM mode 1
	TIM2->CCMR1 |= 0x60;
	
	// Enable the output capture/compare 2
	TIM2->CCMR1 |= 0x8;
	
	// Enable the Auto-Reload of the preload bit (ARPE)
	TIM2->CR1 |= 0x40;
	
	// Enable Channel 2
	TIM2->CCER |= 0x1;
	
	//Create an update event
	TIM2->EGR |= TIM_EGR_UG;	
	
	//Set Port A up as a alternate function
	GPIOA->MODER &= ~(GPIO_MODER_MODER0_0); 
					
	//set the alternate function of Port A
	GPIOA->AFR[0] &= ~GPIO_AFRL_AFRL0;  // Port A pin 0 is set to alternate function 1
	GPIOA->AFR[0] |= 0x0001;	
	
	return 0;
}
