#include "stm32l476xx.h"
#include "SysClock.h"
#include "LED.h"
#include "UART.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MOV (32)
#define WAIT (64)
#define LOOP_START (128)
#define END_LOOP (160)
#define RECIPE_END (0)

uint8_t buffer[BufferSize];
char str[] = "POST routine starting...\r\n";
char post_ok[] = "POST routine successful\r\n";
char post_fail[] = "POST routine failed. Would you like to continue(y,n)?\r\n";
char good_bye[] = "Goodbye\r\n";
char out_range[] = "Error. you have entered a parameter out of the bounds of the Mnemonic command";
unsigned char cmd[] = "        ";

// function prototypes
static int post_test(void);
int init(void);
uint8_t cmd_read(unsigned char cmd);


int main(void){
	char rxByte = 'y';
	int		pass = 0;
	
	System_Clock_Init(); // Switch System Clock = 80 MHz
	LED_Init();
	UART2_Init();
	
	init();
	
	
	// write that we are starting post test
	USART_Write(USART2, (uint8_t *)str, strlen(str));
	// interface with user for post test
	while(!pass && rxByte == 'y')
	{
			if(post_test()) {
				pass = 1;
				USART_Write(USART2, (uint8_t *)post_ok, strlen(post_ok));
			} else {
				USART_Write(USART2, (uint8_t *)post_fail, strlen(post_fail));
				rxByte = USART_Read(USART2);
				if (rxByte == 'Y' || rxByte == 'y'){
					rxByte = 'y';
				}
			}
	}
	rxByte = 'n';
	while(rxByte == 'y' && pass)
	{		
		//USART_Write(USART2, (uint8_t *)another_session, strlen(another_session));
		rxByte = USART_Read(USART2);
		if (rxByte == 'Y' || rxByte == 'y'){
			rxByte = 'y';
		}
	}
	USART_Write(USART2, (uint8_t *)good_bye, strlen(good_bye));
}
int init(void)
{
	/*
		GPIO AND ALTERNATE FUNCTION SETUP
	
	*/
	// IO port A clock enable
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	// reset GPIO A for pa0 and pa1
	GPIOA->MODER &= ~(0x3);
	GPIOA->MODER &= ~(0xC);
	
	
	// set GPIO PA0 to be AF1 or, tim2
	GPIOA->MODER |= 0x2;
	GPIOA->MODER |= 0x8;
	
	// configure the correct AF register to select the alternate function (timer2)
	// we want pin 0 so we use AFR[0]. pins 1-7 are there, pins 8-15 are with AFR[1]
	// AF1 is '0001'
	GPIOA->AFR[0] &= ~(0xF);
	GPIOA->AFR[0] &= ~(0xF0);
	GPIOA->AFR[0] |= (0x1);
	GPIOA->AFR[0] |= (0x10);
	
	/*
		TIMER CAPTURE SETUP
	*/
	// enable clock for Tim2
	// APB1EN is peripheral clock enable register 1
	// TIM2 is the first bit! turn to 1 to enable
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
	// SET PRESCALE VAL - assuming freq of clock is 80MHz
	TIM2->PSC = 0x1F3F; // this 7999 + 1 by the hardware
	// force update by creating event trigger
	TIM2->EGR |= 0x1;
	
	// turn off bit 0 - allows for reconfiguring CC mode to output
	TIM2->CCER &= ~(0x1);
	TIM2->CCER &= ~(0x10);
	
	// 00 will set this to output mode
	TIM2->CCMR1 &= ~(0x00000003);
	// set bits 9:8 to make channel 2 output mode
	TIM2->CCMR1 &= ~(0x00000300);
	
	// this determines frequency - set this to 200 since our psc is 100us per tick
	TIM2->ARR = 0xC8;
	// this determines duty cycle
	TIM2->CCR1 = 4;
	TIM2->CCR2 = 4;
	// pwm mode 1 is 110. use ccmr1 to set this
	// bits 6:4 set channel 1, set 14:12 for channel 2!
	TIM2->CCMR1 &= ~(0x70);
	TIM2->CCMR1 |= 0x60;
	TIM2->CCMR1 &= ~(0x7000);
	TIM2->CCMR1 |= 0x6000;
	
	// set output compare preload enable bit
	TIM2->CCMR1 |= 0x4;
	TIM2->CCMR1 |= 0x800;
	// enable autoreload of the preload bit - bit 7
	TIM2->CR1 |= 0x80;
	// turn on bit 0 to reenable the timer2 ch1
	TIM2->CCER |= 0x1;
	// turn on bit 4 to reenable the timer2 ch2
	TIM2->CCER |= 0x10;
	// force update by creating event trigger
	TIM2->EGR |= 0x1;
	
	// make sure the timer is stopped
	TIM2->CR1 &= ~(TIM_CR1_CEN);
	return 0;
}
uint8_t cmd_read(unsigned char cmd)
{
	int wait_flag;
	uint8_t delay;
	static uint32_t cnt;
	static uint32_t cnt1;
	switch(0xE0 & cmd )
	{
		case(MOV):
			switch(0x1F & cmd)
			{
				case(0) :
					TIM2->CCR1 = 0;				//Sets the duty cycle to 0%, and Position to 0
				case(1) :
					TIM2->CCR1 = 4;				//Sets the duty cycle to 2%, and Position to 1
				case(2) :
					TIM2->CCR1 = 8;				//Sets the duty cycle to 4%, and Position to 2
				case(3) :
					TIM2->CCR1 = 12;				//Sets the duty cycle to 6%, and Position to 3
				case(4) :
					TIM2->CCR1 = 16;				//Sets the duty cycle to 8%, and Position to 4
				case(5) :
					TIM2->CCR1 = 20;				//Sets the duty cycle to 10%, and Position to 5
				if((0x1F & cmd) >= 6)
				{
					USART_Write(USART2, (uint8_t *)out_range, strlen(out_range));
				}
			}
	  case(WAIT):
			wait_flag = 0;
			delay = (0x1F & cmd);		// Im not sure what the units are of the result. Need to confirm they are same as cnt1 and cnt. also need to add (1\10) second to result.
		  cnt = TIM2->CCR1;
			while(wait_flag == 0)
			{								
				while(!(TIM2->SR & TIM_SR_CC1IF)){};			
				cnt1 = TIM2->CCR1;
				if((cnt1-cnt)>= delay)
				{
					wait_flag = 1;
				}
			}
		case(LOOP_START):
			
		case(END_LOOP):
			
		case(RECIPE_END):
			
	}
}

// POST, aka 'Power On Self Test' is meant to check that there is XXXX
static int post_test()
{
	// we give the time limit multiplied by the psc
	TIM2->CR1 |= TIM_CR1_CEN;
	
	while(1){}
	
	TIM2->CR1 &= ~(TIM_CR1_CEN);
	return 1;
}
