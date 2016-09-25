#include "stm32l476xx.h"
#include "SysClock.h"
#include "LED.h"
#include "UART.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

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
char out_range[] = "Error. you have entered a parameter out of the bounds of the Mnemonic command\r\n";
char prompt[] = "\r\n>";
char pause_first1[] = "Error: either Servo 1 needs to be paused before moving, or servo 1 is to the extreme position. Trouble shoot and try again.\r\n";
char pause_first2[] = "Error: either Servo 2 needs to be paused before moving, or servo 2 is to the extreme position. Trouble shoot and try again.\r\n";
char no_override1[] = "No over ride command executed on servo 1";
char no_override2[] = "No over ride command executed on servo 2";
char restart_recipe1[] = "Restarting recipe 1";
char restart_recipe2[] = "Restarting recipe 2";
unsigned char cmd[] = "        ";
int isr_flag = 0;
int enter = 0;
int recipe1[19] = {MOV+0,MOV+5,MOV+0,MOV+3,LOOP_START+0,MOV+1,MOV+4,END_LOOP,MOV+0,MOV+2,WAIT+0,MOV+3,WAIT+0,MOV+2,MOV+3,WAIT+31,WAIT+31,WAIT+31,MOV+4};
uint8_t temp = ' ';
unsigned char u_cmd[6];
int k;
bool run_servo1 = true;			// These flags will help us execute the user entered commands 
bool run_servo2 = true;
bool restart1 = false;
bool restart2 = false;

// function prototypes
static int post_test(void);
void config_port_a(void);
void config_timer(void);
int calc_servo_move(int position, int curr_duty_cycle);
int process_wait(int wait_factor);
void read_recipe(int recipe[]);
void key_board(void);
void user_cmd(void);
//uint8_t cmd_read(unsigned char cmd);


int main(void){
	char rxByte = 'y';
	int		pass = 0;
	
	
//	recipe1[0] = MOV+4;
//	recipe1[1] = WAIT+30;
//	recipe1[2] = MOV+1;
//	recipe1[3] = WAIT+20;
//	recipe1[4] = MOV+5;
	
	System_Clock_Init(); // Switch System Clock = 80 MHz
	LED_Init();
	UART2_Init();
	
	config_port_a();
	config_timer();
	// always have the pwm going?
	TIM2->CR1 |= TIM_CR1_CEN;
	
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
	
	read_recipe(recipe1);
	while(1){}
	
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
	TIM2->CR1 &= ~(TIM_CR1_CEN);
}

// 4 - 20 is what the guide says.. i think the math needs to be revisited
// with our psc we cant put in anything at the 10us precision ... we may need to lessen by factor of 10
// sending cur duty cycle as arg now because we need to delay in this function based on how far mov is.. yet to be implemented
int calc_servo_move(int pos, int curr_duty_cycle)
{
	double next_duty_cycle = 0;
	//double coeff = (16/5);
	int coeff = 4;
	//double offset = 3.88;
	int offset = 4;
	
	if(0 <= pos && pos <= 5)
		next_duty_cycle = (pos * coeff) + offset;
	else
		next_duty_cycle = 0;
	
	return next_duty_cycle;
}

// will process a wait by at least 1/10 of a second.
// returns 1 if argument was in bounds, 0 if not
int process_wait(int wait_factor)
{
	int status = 0;
	// since we are using 20ms periods, we need to wait for counter to reset 5 X wait_factor
	int num_iterations;
	int x;
	int test = 0;
	
	if(0 <= wait_factor && wait_factor <= 31)
	{
		num_iterations = 5 + (5 * wait_factor);
		for(x = 0; x < num_iterations; x++)
		{
			while(TIM2->CNT != 0){};
			test++;
		}
		status = 1;
	}
	
	return status;
}

void read_recipe(int recipe[])
{
	int opcode1 = 0;
	int opcode_argument1 = 0;
	int opcode2 = 0;
	int opcode_argument2 = 0;
	int op_mask = 0xE0;
	int op_arg_mask = 0x1F;
	double command_result = 0;
	int recipe_len = sizeof(recipe1);		// If left as just recipe it gave the wrong size of the array. need to trouble shoot, otherwise can only use 1 recipe at a time
	int x;
	int y;
	int loop1;
	int loop2;
	int num_loop1 = 0;
	bool loop_flag1 = false;
	int num_loop2 = 0;
	bool loop_flag2 = false;
	bool loop_end1 = false;
	bool loop_end2 = false;
	
	loop1 = 0;			//Will want this to be zero again if we do a second recipe
	loop2 = 0;
	num_loop1 = 0;
	num_loop2 = 0;
	x = 0;
	y = 0;
	while( x <= recipe_len && y <= recipe_len)
	{
		if(restart1)			// If the flag is set the recipe read will move to the start of the array.
		{
			x = 0;
		}
		if(restart2)
		{
			y = 0;
		}
		if((TIM2->SR & 2) != 0)
		{
			key_board();
		}
		// mask the element to get the opcode - the top 3 bits
		opcode1 = recipe[x] & op_mask;
		opcode_argument1 = recipe[x] & op_arg_mask;
		opcode2 = recipe[y] & op_mask;
		opcode_argument2= recipe[y] & op_arg_mask;		
		if(opcode1 == MOV)
		{
			command_result = calc_servo_move(opcode_argument1, TIM2->CCR1);  		// I dont think we need to include current duty cycle in this function as it is not used
			// make sure we didn't violate the bounds -- method will return 0 if so
			if(command_result)
			{
				if(run_servo1)
				{
					TIM2->CCR1 = command_result;
				}				
			}
			else
			{
				USART_Write(USART2, (uint8_t *)out_range, strlen(out_range));
			}
		}
		if(opcode2 == MOV)
		{
			command_result = calc_servo_move(opcode_argument2, TIM2->CCR2);  		// I dont think we need to include current duty cycle in this function as it is not used
			// make sure we didn't violate the bounds -- method will return 0 if so
			if(command_result)
			{
				if(run_servo2)
				{
					TIM2->CCR2 = command_result;
				}
			}
			else
			{
				USART_Write(USART2, (uint8_t *)out_range, strlen(out_range));
			}
		}
		if(opcode1 == WAIT)
		{
			process_wait(opcode_argument1);
		}
		if(opcode2 == WAIT)
		{
			process_wait(opcode_argument2);
		}
		if(opcode1 == LOOP_START)			// Use boolen flag to allow counter below to count the following number of elements to be in the loop
		{
			loop_flag1 = true;
			num_loop1 = opcode_argument1;			// Number of times the loop is iterated
		}
		if(opcode2 == LOOP_START)			// Use boolen flag to allow counter below to count the following number of elements to be in the loop
		{
			loop_flag2 = true;
			num_loop2 = opcode_argument2;			// Number of times the loop is iterated
		}
		if(opcode1 == END_LOOP)				
		{
			loop_flag1 = false;
			loop_end1 = true;								// This boolean flag tells us when to decrement our num_loop
		}
		if(opcode2 == END_LOOP)				
		{
			loop_flag2 = false;
			loop_end2 = true;								// This boolean flag tells us when to decrement our num_loop
		}
		if(opcode1 == RECIPE_END)
		{
			x = recipe_len+1;
		}
		if(opcode2 == RECIPE_END)
		{
			y = recipe_len+1;
		}
		if(loop_flag1)
		{
			loop1++;
		}
		if(loop_flag2)
		{
			loop2++;
		}
		if(loop_end1)
		{
			if(num_loop1 > 0)					// but x will be iterated after this loop so just add 1 
			{
				x = x - loop1;
				num_loop1--;
				loop_end1 = false;
			}
		}
		if(loop_end2)
		{
			if(num_loop2 > 0)					// but x will be iterated after this loop so just add 1 
			{
				y = y - loop2;
				num_loop2--;
				loop_end2 = false;
			}
		}
		if(x < recipe_len)				// increment for next recipe.
		{
			if(run_servo1)
			{
				x++;
			}
		}
		if(y < recipe_len)
		{
			if(run_servo2)
			{
				y++;
			}
		}
	}
}


// Create interupt from a keyboard entry
void key_board(void)
{
	temp = ' ';
	temp = USART_Read(USART2);	
	u_cmd[k] = temp;		// adjust the ASCII values to mean something 
	USART_Write(USART2,&temp,1);	
	if(temp != ' ')
	{
		k++;
	}
	if(u_cmd[2] == 60 && (u_cmd[3] == 67 || u_cmd[3] == 99) && (u_cmd[4] == 82 || u_cmd[4] == 114) && u_cmd[5] == 62) 	// checks for "<CR>" in terms of ascii values
	{
		user_cmd();
	}	
  if(k >= 6)
	{
		memset(&u_cmd[0], 0, sizeof(u_cmd));		// clear the array. either too many charecters entered, the wrong charecters were entered, or we already processed the user command
		USART_Write(USART2, (uint8_t *)prompt, strlen(prompt));						// Shows user the prompt charecter ">"
		k = 0;
	}
}
// Create function to parse user command entries via the keyboard
void user_cmd(void)
{
	switch(u_cmd[0])
	{
		case(80):			//Create a pause mechanism
			run_servo1 = false;
			break;
		case(112):
			run_servo1 = false;
			break;
		case(67):			//Create a continue mechanism
			run_servo1 = true;
			break;
		case(99):
			run_servo1 = true;
			break;
		case(82):			 //Create a move 1 to the right position mechanism
			if(run_servo1 == false && TIM2->CCR1 < 24)
			{
				TIM2->CCR1 = TIM2->CCR1 + 4;			// For now we will assume moving right means to add. Will need to verify when testing
			}
			if(run_servo1 == true || (TIM2->CCR1 == 24))
			{
				USART_Write(USART2, (uint8_t *)pause_first1, strlen(pause_first1));
			}
			break;
		case(114):
			if(run_servo1 == false && TIM2->CCR1 < 24)
			{
				TIM2->CCR1 = TIM2->CCR1 + 4;			// For now we will assume moving right means to add. Will need to verify when testing
			}
			if(run_servo1 == true || (TIM2->CCR1 == 24))
			{
				USART_Write(USART2, (uint8_t *)pause_first1, strlen(pause_first1));
			}
			break;
		case(76):				// Create a move 1 to the left postion mechanism
			if(run_servo1 == false && TIM2->CCR1 > 4)
			{
				TIM2->CCR1 = TIM2->CCR1 - 4;			// For now we will assume moving left means to sutract. Will need to verify when testing
			}
			if(run_servo1 == true || (TIM2->CCR1 == 4))
			{
				USART_Write(USART2, (uint8_t *)pause_first1, strlen(pause_first1));
			}
			break;
		case(108):
			if(run_servo1 == false && TIM2->CCR1 > 4)
			{
				TIM2->CCR1 = TIM2->CCR1 - 4;			// For now we will assume moving left means to subtract. Will need to verify when testing
			}
			if(run_servo1 == true || (TIM2->CCR1 == 4))
			{
				USART_Write(USART2, (uint8_t *)pause_first1, strlen(pause_first1));
			}
			break;
		case(78):					// Create a no operation command/prompt
			USART_Write(USART2, (uint8_t *)no_override1, strlen(no_override1));
			break;
		case(110):
			USART_Write(USART2, (uint8_t *)no_override1, strlen(no_override1));
			break;
		case(66):			// Raise a flag which will reset x in the for loop for the recipe_read
			restart1 = true;
			USART_Write(USART2, (uint8_t *)restart_recipe1, strlen(restart_recipe1));
			break;
		case(98):
			restart1 = true;
			USART_Write(USART2, (uint8_t *)restart_recipe1, strlen(restart_recipe1));
			break;			
	}
	
	switch(u_cmd[1])
	{
		case(80):			//creat a pause mechanism
			run_servo2 = false;
			break;
		case(112):
			run_servo2 = false;
			break;
		case(67):			//Create a continue mechanism
			run_servo2 = true;
			break;
		case(99):
			run_servo2 = true;
			break;
		case(82):			 //Create a move 1 to the right position mechanism
			if(run_servo2 == false && TIM2->CCR2 < 24)
			{
				TIM2->CCR2 = TIM2->CCR2 + 4;			// For now we will assume moving right means to add. Will need to verify when testing
			}
			if(run_servo2 == true || (TIM2->CCR2 == 24))
			{
				USART_Write(USART2, (uint8_t *)pause_first2, strlen(pause_first2));
			}
			break;
		case(114):
			if(run_servo2 == false && TIM2->CCR2 < 24)
			{
				TIM2->CCR2 = TIM2->CCR2 + 4;			// For now we will assume moving right means to add. Will need to verify when testing
			}
			if(run_servo2 == true || (TIM2->CCR2 == 24))
			{
				USART_Write(USART2, (uint8_t *)pause_first2, strlen(pause_first2));
			}
			break;
		case(76):				// Create a move 1 to the left postion mechanism
			if(run_servo2 == false && TIM2->CCR2 > 4)
			{
				TIM2->CCR2 = TIM2->CCR2 - 4;			// For now we will assume moving left means to sutract. Will need to verify when testing
			}
			if(run_servo2 == true || (TIM2->CCR2 == 4))
			{
				USART_Write(USART2, (uint8_t *)pause_first2, strlen(pause_first2));
			}
			break;
		case(108):
			if(run_servo2 == false && TIM2->CCR2 > 4)
			{
				TIM2->CCR2 = TIM2->CCR2 - 4;			// For now we will assume moving left means to subtract. Will need to verify when testing
			}
			if(run_servo2 == true || (TIM2->CCR2 == 4))
			{
				USART_Write(USART2, (uint8_t *)pause_first2, strlen(pause_first2));
			}
			break;
		case(78):					// Create a no operation command/prompt
			USART_Write(USART2, (uint8_t *)no_override2, strlen(no_override2));
			break;
		case(110):
			USART_Write(USART2, (uint8_t *)no_override2, strlen(no_override2));
			break;
		case(66):			// Raise a flag which will reset x in the for loop for the recipe_read
			restart2 = true;
			USART_Write(USART2, (uint8_t *)restart_recipe2, strlen(restart_recipe2));
			break;
		case(98):
			restart2 = true;
		USART_Write(USART2, (uint8_t *)restart_recipe2, strlen(restart_recipe2));
			break;			
	}
			
}

void config_port_a()
{
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
}

void config_timer()
{
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
}

/*uint8_t cmd_read(unsigned char cmd)
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
		//case(LOOP_START):
			
		//case(END_LOOP):
			
		//case(RECIPE_END):
			
	}
}*/

// POST, aka 'Power On Self Test' is meant to check that there is XXXX
static int post_test()
{
	return 1;
}
