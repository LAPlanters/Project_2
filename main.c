#include "stm32l476xx.h"
#include "SysClock.h"
#include "LED.h"
#include "UART.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define NUM_SERVO (2)

// OpCode Constants
#define MOV (32)
#define WAIT (64)
#define LOOP_START (128)
#define END_LOOP (160)
#define RECIPE_END (0)

// custom OpCodes
#define THE_SPRINKLER (96)
#define WAVE (192)

// User Override Command Constants
#define ENTER_USR_COMMAND (13)

#define NUM_SPRINKLER_STEPS (12)

// strings used for messaging with the UART
uint8_t buffer[BufferSize];
uint8_t allowed_user_input[] = {'p', 'P', 'c', 'C', 'r', 'R', 'l', 'L', 'n', 'N', 'b', 'B', 'x', 'X'};
char out_range[] = "Error. you have entered a parameter out of the bounds of the Mnemonic command\r\n";
char prompt[] = "\r\n>";
char no_override1[] = "No over ride command executed on servo 1";
char no_override2[] = "No over ride command executed on servo 2";
char restart_recipe1[] = "Restarting recipe 1";
char restart_recipe2[] = "Restarting recipe 2";
char error_prompt[] = "Error: You have entered an error state. Reset Required";
char pause_prompt[] = "Error: Please pause the servo before attempting to mannually move.";
char good_bye[] = "Goodbye";

int recipe1[] = {MOV+0,MOV+5,MOV+0,MOV+3,LOOP_START+0,MOV+1,MOV+4,END_LOOP,MOV+0,MOV+2,WAIT+0,MOV+3,WAIT+0,MOV+2,MOV+3,WAIT+31,WAIT+31,WAIT+31,MOV+4};
int recipe2[] = {MOV+0,MOV+2,WAIT+13,WAVE,LOOP_START+2,MOV+1,MOV+4,END_LOOP,THE_SPRINKLER,MOV+2,WAIT+8,MOV+3,MOV+1,MOV+2,MOV+3,LOOP_START+2,THE_SPRINKLER,END_LOOP,WAIT+2,RECIPE_END};
int recipe3[] = {LOOP_START+3, MOV+0, MOV+1, MOV+2, MOV+3, MOV+4, MOV+5, MOV+4, MOV+3, MOV+2, MOV+1, MOV+0, END_LOOP};
int recipe4[] = {WAVE, WAVE, WAVE, WAVE, WAVE, WAVE, MOV+3, WAIT+30, RECIPE_END};

// array for storing user override commands
unsigned char u_cmd[3];
int servo0_cursor;
int servo1_cursor;
int servo0_wait_bank;
int servo1_wait_bank;
int servo0_loop_state;
int servo0_num_loop_steps;
int servo1_loop_state;
int servo1_num_loop_steps;
int servo0_pause_state;
int servo1_pause_state;
int servo0_sprinkler_steps;
int servo1_sprinkler_steps;
	
// function prototypes
void config_port_a(void);
void config_timer(void);
int move_servo(int servo_no, int position, int curr_duty_cycle);
void set_wait_bank(int servo_no, int wait_factor);
void parse_recipe(int recipe[], int array_size);
void read_user_cmd(void);
void manage_scheduling(void);
void initiate_startup_sequence(int servo_num);
int is_servo_ready(int servo_no);
int get_servo_cursor(int servo_no);
void set_servo_cursor(int servo_no, int servo_cursor);
int get_loop_state(int servo_no);
void set_loop_state(int servo_no, int loop_state);
void set_num_loop_steps(int servo_no, int num_steps);
int get_num_loop_steps(int servo_no);
int find_end_loop(int recipe[], int curr_index, int array_size);
void set_pause_state(int servo_no, int state);
int get_curr_duty_cycle(int servo_no);
void reset_wait_bank(int servo_no);
int get_servo_pos(int servo_no);
void do_the_sprinkler(int servo_no, int loops);
void set_exec_sprinkler_steps(int servo_no, int steps);
int get_exec_sprinkler_steps(int servo_no);

int main(void)
{
	const int len = sizeof(recipe2)/sizeof(recipe2[0]);
	int get_servo_pos(int servo_no);
	
	System_Clock_Init(); // Switch System Clock = 80 MHz
	LED_Init();
	UART2_Init();
	
	config_port_a();
	config_timer();
	Green_LED_On();
	Red_LED_Off();
	
	// enable timer
	TIM2->CR1 |= TIM_CR1_CEN;
	
	initiate_startup_sequence(0);
	initiate_startup_sequence(1);
	manage_scheduling();

	USART_Write(USART2, (uint8_t *)prompt, strlen(prompt));
	parse_recipe(recipe2, len);

	USART_Write(USART2, (uint8_t *)good_bye, strlen(good_bye));
	TIM2->CR1 &= ~(TIM_CR1_CEN);
}

// 4 - 20 is what the guide says.. i think the math needs to be revisited
// with our psc we cant put in anything at the 10us precision ... we may need to lessen by factor of 10
int move_servo(int servo_no, int pos, int curr_duty_cycle)
{
	int status = 0;
	//double coeff = (16/5);
	int coeff = 4;
	//double offset = 3.88;
	int offset = 4;
	int next_duty_cycle = 0;
	
	if (0 <= pos && pos <= 5)
	{
		status = 1;
		next_duty_cycle = (pos * coeff) + offset;

		if (servo_no == 0)
		{
			Green_LED_On();
			Red_LED_Off();
			TIM2->CCR1 = next_duty_cycle;
		}
		else if (servo_no == 1)
		{
			Green_LED_On();
			Red_LED_Off();
			TIM2->CCR2 = next_duty_cycle;
		}

		// we want to wait 200ms per position - that means we get abs value of our pos delta
		// we would then div by 4 to get our 'grades' of positions, and then we need to mult by 2
		// in order to use our process_wait position which takes in an arg that is our mult of tenth of second
		set_wait_bank(servo_no, abs(next_duty_cycle - curr_duty_cycle) / 2);
	}	
	return status;
}

void do_the_sprinkler(int servo_no, int num_executions)
{
	// cannot directly call back to parse_recipe from here so we simulate the follwing
	// {MOV+5, WAIT+10, MOV+4, MOV+5, MOV+3, MOV+4, MOV+2, MOV+3, MOV+1, MOV+2, MOV+0};
	int curr_duty_cycle = get_curr_duty_cycle(servo_no);
	
	switch(num_executions)
	{
		case 0:
			move_servo(servo_no, 5, curr_duty_cycle);
		case 1:
			set_wait_bank(servo_no, 3);
			break;
		case 2:
			move_servo(servo_no, 4, curr_duty_cycle);
			break;
		case 3:
			move_servo(servo_no, 5, curr_duty_cycle);
			break;
		case 4:
			move_servo(servo_no, 3, curr_duty_cycle);
			break;
		case 5:
			move_servo(servo_no, 4, curr_duty_cycle);
			break;
		case 6:
			move_servo(servo_no, 2, curr_duty_cycle);
			break;
		case 7:
			move_servo(servo_no, 3, curr_duty_cycle);
			break;
		case 8:
			move_servo(servo_no, 1, curr_duty_cycle);
			break;
		case 9:
			move_servo(servo_no, 2, curr_duty_cycle);
			break;
		case 10:
			move_servo(servo_no, 0, curr_duty_cycle);
			break;
		case 11:
			move_servo(servo_no, 5, curr_duty_cycle);
			break;
		default:
			set_wait_bank(servo_no, 10);
	}
}

void set_wait_bank(int servo_no, int wait_factor)
{
	// since we are using 20ms periods, we need to wait for counter to reset 5 X wait_factor
	int num_clock_cycles;

	num_clock_cycles = (5 * wait_factor);
	if (servo_no == 0)
		servo0_wait_bank += num_clock_cycles;
	else if (servo_no == 1)
		servo1_wait_bank += num_clock_cycles;
}

void reset_wait_bank(int servo_no)
{
	if (servo_no == 0)
		servo0_wait_bank = 0;
	else if (servo_no == 1)
		servo1_wait_bank = 0;
}

void parse_recipe(int recipe[], int array_size)
{
	int opcode = 0;
	int opcode_argument = 0;
	int op_mask = 0xE0;
	int op_arg_mask = 0x1F;
	int inner_itr = 0;
	int servo_cursor = 0;
	int loop_state = 0;
	int num_loop_steps = 0;
	int executed_sprinkler_steps = 0;
	int curr_duty_cycle = 0;	

	while (servo0_cursor != -1 || servo1_cursor != -1)
	{
		// we send the iterator of this loop to methods that execute commands so that we don't concern ourselves
		// with hardware specific stuff here
		for (inner_itr = 0; inner_itr < NUM_SERVO; inner_itr++)
		{
			servo_cursor = get_servo_cursor(inner_itr);
			// make sure we can even read the next command
			if (is_servo_ready(inner_itr) && servo_cursor < array_size)
			{
				// mask the element to get the opcode - the top 3 bits
				opcode = recipe[servo_cursor] & op_mask;
				opcode_argument = recipe[servo_cursor] & op_arg_mask;

				if (opcode == MOV)
				{
					curr_duty_cycle = get_curr_duty_cycle(inner_itr);
					// make sure we didn't violate the bounds -- method will return 0 if so
					if (!move_servo(inner_itr, opcode_argument, curr_duty_cycle))
					{
						Red_LED_On();
						Green_LED_Off();
						USART_Write(USART2, (uint8_t *)out_range, strlen(out_range));
						USART_Write(USART2, (uint8_t *)error_prompt, strlen(error_prompt));
						while(1);
					}
					else
						set_servo_cursor(inner_itr, ++servo_cursor);
				}
				else if (opcode == WAIT)
				{
					set_wait_bank(inner_itr, opcode_argument);
					set_servo_cursor(inner_itr, ++servo_cursor);
				}
				else if (opcode == THE_SPRINKLER)
				{
					// since the sprinkler is really a recipe itself, we dont actually advance
					// from this step in the real recipe until we know we've completed the number
					// of sprinkler steps
					executed_sprinkler_steps = get_exec_sprinkler_steps(inner_itr);
					do_the_sprinkler(inner_itr, executed_sprinkler_steps);
					set_exec_sprinkler_steps(inner_itr, ++executed_sprinkler_steps);
					if(executed_sprinkler_steps == NUM_SPRINKLER_STEPS)
					{
						set_exec_sprinkler_steps(inner_itr, 0);
						set_servo_cursor(inner_itr, ++servo_cursor);
					}
				}
				else if (opcode == LOOP_START)
				{
					// loop state holds the number of loops left to execute
					loop_state = get_loop_state(inner_itr);
					if (loop_state)
						set_loop_state(inner_itr, loop_state - 1);
					else
					{
						set_loop_state(inner_itr, opcode_argument + 1);
						set_num_loop_steps(inner_itr, find_end_loop(recipe, servo_cursor, array_size));
					}

					set_servo_cursor(inner_itr, ++servo_cursor);
				}
				else if (opcode == END_LOOP)
				{
					loop_state = get_loop_state(inner_itr);
					if (loop_state) {
						num_loop_steps = get_num_loop_steps(inner_itr);
						set_servo_cursor(inner_itr, servo_cursor - num_loop_steps);
					}
					else
					{
						set_num_loop_steps(inner_itr, 0);
						set_servo_cursor(inner_itr, ++servo_cursor);
					}
				}
				else if (opcode == RECIPE_END)
				{
					set_servo_cursor(inner_itr, -1);
				}
				else if (opcode == WAVE)
				{
					curr_duty_cycle = get_curr_duty_cycle(0);
					move_servo(0, 0, curr_duty_cycle);
					manage_scheduling();
					curr_duty_cycle = get_curr_duty_cycle(1);
					move_servo(1, 0, curr_duty_cycle);
					manage_scheduling();
					
					curr_duty_cycle = get_curr_duty_cycle(1);
					move_servo(1, 5, curr_duty_cycle);	
					manage_scheduling();
					curr_duty_cycle = get_curr_duty_cycle(0);
					move_servo(0, 5, curr_duty_cycle);
					manage_scheduling();
					curr_duty_cycle = get_curr_duty_cycle(0);
					move_servo(0, 0, curr_duty_cycle);
					manage_scheduling();
					curr_duty_cycle = get_curr_duty_cycle(1);
					move_servo(1, 0, curr_duty_cycle);
					manage_scheduling();
					
					curr_duty_cycle = get_curr_duty_cycle(1);
					move_servo(1, 5, curr_duty_cycle);
					manage_scheduling();					
					curr_duty_cycle = get_curr_duty_cycle(0);
					move_servo(0, 5, curr_duty_cycle);
					manage_scheduling();
					curr_duty_cycle = get_curr_duty_cycle(0);
					move_servo(0, 0, curr_duty_cycle);
					manage_scheduling();
					curr_duty_cycle = get_curr_duty_cycle(1);
					move_servo(1, 0, curr_duty_cycle);					
				}
			}
		}
		// we execute the pausing and check for user input after we have executed every servo's next command
		manage_scheduling();
	}
}

// Create function to parse user command entries via the keyboard
void read_user_cmd(void)
{
	int index = 10;
	int x = 0;
	int len_whitelist = sizeof(allowed_user_input);
	uint8_t user_in = USART_NonBlock_Read(USART2);
	uint8_t new_line = '\n';
	
	if(user_in != 0)
	{
		USART_Write(USART2, &user_in, 1);
		
		if (user_in == 'x' || user_in == 'X')
		{
			memset(&u_cmd[0], 0, sizeof(u_cmd));
			USART_Write(USART2, (uint8_t *)prompt, strlen(prompt));
		}
		else {
			// need to figure out which index we are at
			if (u_cmd[0] == '\0')
			{
				index = 0;
			}
			else if (u_cmd[1] == '\0')
			{
				index = 1;
			}
			else if (u_cmd[2] == '\0' && user_in == ENTER_USR_COMMAND)
			{
				u_cmd[2] = user_in;
				USART_Write(USART2, &new_line, 1);
			}
		}

		if (index < 2) 
		{
			while (x < len_whitelist)
			{
				if (user_in == allowed_user_input[x])
				{
					u_cmd[index] = user_in;
					break;
				}
				x++;
			}
		}
	}
}

void manage_scheduling()
{
	// we initialize to 5 so that we always wait at least 1/10 of a second whenever this is called
	int num_clock_cycles = 5;
	uint8_t servo_pos;
	int cur_duty_cycle;
	int x;
	int y;

	// we want to take the smallest value of the two UNLESS
	// the smallest value happens to be in a paused state
	if(servo0_pause_state == 1)
		num_clock_cycles += servo1_wait_bank;
	else if(servo1_pause_state == 1)
		num_clock_cycles += servo0_wait_bank;
	else if(servo0_wait_bank < servo1_wait_bank)
		num_clock_cycles += servo0_wait_bank;
	else if(servo1_wait_bank <= servo0_wait_bank)
		num_clock_cycles += servo1_wait_bank;
	
	for(x = 0; x < num_clock_cycles; x++)
	{
		while(TIM2->CNT == 0){}
		while (TIM2->CNT != 0){}

		if (servo0_wait_bank - 1 >= 0 && servo0_pause_state != 1)
			servo0_wait_bank--;
		if (servo1_wait_bank - 1 >= 0 && servo1_pause_state != 1)
			servo1_wait_bank--;

		read_user_cmd();
		if (u_cmd[2] == ENTER_USR_COMMAND)
		{
			for (y = 0; y < NUM_SERVO; y++)
			{
				if (u_cmd[y] == 'p' || u_cmd[y] == 'P')
				{
					set_pause_state(y, 1);
				}
				else if (u_cmd[y] == 'c' || u_cmd[y] == 'C')
				{
					set_pause_state(y, 0);
				}
				else if (u_cmd[y] == 'r' || u_cmd[y] == 'R')
				{
					if( y == 0 && servo0_pause_state == 1)
					{
						cur_duty_cycle = get_curr_duty_cycle(y);
						servo_pos = ((cur_duty_cycle - 4)/4)-1;
						if(servo_pos > 0)
							move_servo(y, servo_pos, cur_duty_cycle);
						else if(servo_pos == 0)
						{
							Red_LED_On();
							Green_LED_Off();
							USART_Write(USART2, (uint8_t *)error_prompt, strlen(error_prompt));
							while(1);
						}
					}
					else if(servo0_pause_state != 1)
					{
						Red_LED_On();
						Green_LED_Off();
						USART_Write(USART2, (uint8_t *)pause_prompt, strlen(pause_prompt));
					}				
					
					if( y == 1 && servo1_pause_state == 1)
					{
						cur_duty_cycle = get_curr_duty_cycle(y);
						servo_pos = ((cur_duty_cycle - 4)/4)-1;
						if(servo_pos > 0)
							move_servo(y, servo_pos, cur_duty_cycle);
						else if(servo_pos == 0)
						{
							Red_LED_On();
							Green_LED_Off();
							USART_Write(USART2, (uint8_t *)error_prompt, strlen(error_prompt));
							while(1);
						}
					}
					
					else if(servo1_pause_state != 1)
					{
						Red_LED_On();
						Green_LED_Off();
						USART_Write(USART2, (uint8_t *)pause_prompt, strlen(pause_prompt));
					}					
				}				
					
				else if (u_cmd[y] == 'l' || u_cmd[y] == 'L')
				{
					if( y == 0 && servo0_pause_state == 1)
					{
						cur_duty_cycle = get_curr_duty_cycle(y);
						servo_pos = ((cur_duty_cycle - 4)/4)+1;
						if(servo_pos < 5)
							move_servo(y, servo_pos, cur_duty_cycle);
						else if(servo_pos >= 5)
						{
							Red_LED_On();
							Green_LED_Off();
							USART_Write(USART2, (uint8_t *)error_prompt, strlen(error_prompt));
							while(1);
						}
					}
					
					else if(servo0_pause_state != 1)
					{
						Red_LED_On();
						Green_LED_Off();
						USART_Write(USART2, (uint8_t *)pause_prompt, strlen(pause_prompt));
					}									
					
					if( y == 1 && servo1_pause_state == 1)
					{
						cur_duty_cycle = get_curr_duty_cycle(y);
						servo_pos = ((cur_duty_cycle - 4)/4)+1;
						if(servo_pos < 5)
							move_servo(y, servo_pos, cur_duty_cycle);
						else if(servo_pos >= 5)
						{
							Red_LED_On();
							Green_LED_Off();
							USART_Write(USART2, (uint8_t *)error_prompt, strlen(error_prompt));
							while(1);
						}	
					}					
					else if(servo1_pause_state != 1)
					{
						Red_LED_On();
						Green_LED_Off();
						USART_Write(USART2, (uint8_t *)pause_prompt, strlen(pause_prompt));
					}
				}
				else if (u_cmd[y] == 'n' || u_cmd[y] == 'N')
					continue;
				else if (u_cmd[y] == 'b' || u_cmd[y] == 'B')
				{
					set_servo_cursor(y, 0);
					reset_wait_bank(y);
					set_pause_state(y, 0);
					set_loop_state(y, 0);
					set_exec_sprinkler_steps(y, 0);
				}
			}
			memset(&u_cmd[0], 0, sizeof(u_cmd));
			USART_Write(USART2, (uint8_t *)prompt, strlen(prompt));
			break;
		}
	}
}

// this method is for ensuring that each servo begins a recipe from position 0
// and the other variables associated to the servo are reset
void initiate_startup_sequence(int servo_num)
{

	if (servo_num == 0)
	{
		servo0_wait_bank = 0;
		move_servo(0, 0, TIM2->CCR1);
		servo0_cursor = 0;
		servo0_loop_state = 0;
	}
	else if (servo_num == 1)
	{
		servo1_wait_bank = 0;
		move_servo(1, 0, TIM2->CCR2);
		servo1_cursor = 0;
		servo1_loop_state = 0;
	}

}

// GETTERS AND SETTERS
int get_servo_cursor(int servo_no)
{
	int servo_cursor = 0;
	if (servo_no == 0)
		servo_cursor = servo0_cursor;
	else if (servo_no == 1)
		servo_cursor = servo1_cursor;
	
	return servo_cursor;
}

void set_servo_cursor(int servo_no, int new_val)
{
	if (servo_no == 0)
		servo0_cursor = new_val;
	else if (servo_no == 1)
		servo1_cursor = new_val;
}

int get_loop_state(int servo_no)
{
	int loop_state = 0;
	if (servo_no == 0)
		loop_state = servo0_loop_state;
	else if (servo_no == 1)
		loop_state = servo1_loop_state;
	
	return loop_state;
}

void set_loop_state(int servo_no, int state)
{
	if (servo_no == 0)
		servo0_loop_state = state;
	else if (servo_no == 1)
		servo1_loop_state = state;
}

int get_num_loop_steps(int servo_no)
{
	int num_steps = 0;
	if (servo_no == 0)
		num_steps = servo0_num_loop_steps;
	else if (servo_no == 1)
		num_steps = servo1_num_loop_steps;
	
	return num_steps;
}

void set_num_loop_steps(int servo_no, int no_steps)
{
	if (servo_no == 0)
		servo0_num_loop_steps = no_steps;
	else if (servo_no == 1)
		servo1_num_loop_steps = no_steps;
}

// 0 if not paused, 1 if paused
void set_pause_state(int servo_no, int state)
{
	if (servo_no == 0)
		servo0_pause_state = state;
	else if (servo_no == 1)
		servo1_pause_state = state;
}

int get_curr_duty_cycle(int servo_no)
{
	int duty_cycle = 0;
	if (servo_no == 0)
		duty_cycle = TIM2->CCR1;
	else if (servo_no == 1)
		duty_cycle = TIM2->CCR2;
	
	return duty_cycle;
}

void set_exec_sprinkler_steps(int servo_no, int steps)
{
	if(servo_no == 0)
		servo0_sprinkler_steps = steps;
	else if(servo_no == 1)
		servo1_sprinkler_steps = steps;
}

int get_exec_sprinkler_steps(int servo_no)
{
	int steps = 0;
	if(servo_no == 0)
		steps = servo0_sprinkler_steps;
	else if(servo_no == 1)
		steps = servo1_sprinkler_steps;
	
	return steps;
}

int find_end_loop(int recipe[], int start_index, int len)
{
	int op_mask = 0xE0;
	int steps = 0;

	while (start_index < len)
	{
		if ((recipe[start_index] & op_mask) == END_LOOP)
			break;
		steps++;
		start_index++;
	}

	return steps;
}

int is_servo_ready(int servo_no) 
{
	int is_ready = 0;
	if (servo_no == 0)
	{
		if(servo0_wait_bank == 0 && servo0_pause_state == 0 && servo0_cursor != -1)
			is_ready = 1;
	}
	else if (servo_no == 1)
	{
		if(servo1_wait_bank == 0 && servo1_pause_state == 0 && servo1_cursor != -1)
			is_ready = 1;
	}

	return is_ready;
}

/*int get_servo_pos(int servo_no)
{
	int cur_duty_cycle;
	//int pos;
	if (servo_no == 0)
			cur_duty_cycle = (TIM2->CCR1 & 0xFF);
		else if (servo_no == 1)
			cur_duty_cycle = (TIM2->CCR2 & 0xFF);	
	return cur_duty_cycle;
}*/

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

