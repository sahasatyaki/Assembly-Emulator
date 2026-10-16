#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <netinet/in.h>
#include "emulator.h"
//#include <stdint.h>

#define XSTR(x) STR(x) // can be used for MAX_ARG_LEN in sscanf
#define STR(x) #x

// These are some macro variables and function to simplify translation between
// memory addresses and array indices.
#define ADDR_TEXT 0x00400000									   // where the .text area starts in which the program lives
#define ADDR_DATA 0x10010000									   // where the .text area starts in which the program lives
#define TEXT_POS(a) ((a == ADDR_TEXT) ? (0) : (a - ADDR_TEXT) / 4) // can be used to access text[]
#define TEXT_ADDR_POS(j) (j * 4 + ADDR_TEXT)					   // convert text index to address
#define DATA_POS(a) ((a == ADDR_DATA) ? (0) : (a - ADDR_DATA) / 4) // can be used to access data_str[]
#define DATA_ADDR_POS(j) (j * 4 + ADDR_DATA)					   // convert data index to address

const char *register_str[] = {"$zero",
							  "$at", "$v0", "$v1",
							  "$a0", "$a1", "$a2", "$a3",
							  "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
							  "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
							  "$t8", "$t9",
							  "$k0", "$k1",
							  "$gp",
							  "$sp", "$fp", "$ra"};

/* These variables are used to store the string representation of the code by the
 * load_program function */

/* Space for the instructions of the assembler program */
char prog_str[MAX_PROG_LEN][MAX_LINE_LEN];
int prog_len = 0;
/* Array to carry the data section of the assembly program */
char data_str[MAX_PROG_LEN][MAX_LINE_LEN];
int data_len = 0;

/* These arrays should store the content of the text and data sections of the program
 * affer the make_bytecode function. */
unsigned int data[MAX_PROG_LEN] = {0}; // the text memory with our instructions
unsigned int text[MAX_PROG_LEN] = {0}; // the text memory with our instructions

/* Elements for running the emulator. */
unsigned int registers[MAX_REGISTER] = {0}; // the registers
unsigned int pc = 0;						// the program counter

/* function to create bytecode for instruction nop
   conversion result is passed in bytecode
   function always returns 0 (conversion OK) */
typedef int (*opcode_function)(unsigned int, unsigned int *, char *, char *, char *, char *);

int add_imi(unsigned int *bytecode, short int imi)
{
	if (imi < -32768 || imi > 32767)
		return (-1);
	*bytecode |= (0xFFFF & imi);
	return (0);
}

int add_sht(unsigned int *bytecode, int sht)
{
	if (sht < 0 || sht > 31)
		return (-1);
	*bytecode |= (0x1F & sht) << 6;
	return (0);
}

int add_reg(unsigned int *bytecode, char *reg, int pos)
{
	int i;
	for (i = 0; i < MAX_REGISTER; i++)
	{
		if (!strcmp(reg, register_str[i]))
		{
			*bytecode |= (i << pos);
			return (0);
		}
	}
	return (-1);
}

int add_addr(unsigned int *bytecode, int addr)
{
	*bytecode |= ((addr >> 2) & 0x3FFFFF);
	return 0;
}

int add_lbl(unsigned int offset, unsigned int *bytecode, char *label)
{
	char l[MAX_ARG_LEN + 1];
	int j = 0;
	while (j < prog_len)
	{
		memset(l, 0, MAX_ARG_LEN + 1);
		sscanf(&prog_str[j][0], "%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label != NULL && !strcmp(l, label))
			return (add_imi(bytecode, j - (offset + 1)));
		j++;
	}
	return (-1);
}

int add_text_addr(unsigned int *bytecode, char *label)
{
	char l[MAX_ARG_LEN + 1];
	int j = 0;
	while (j < prog_len)
	{
		memset(l, 0, MAX_ARG_LEN + 1);
		sscanf(&prog_str[j][0], "%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label != NULL && !strcmp(l, label))
			return (add_addr(bytecode, TEXT_ADDR_POS(j)));
		j++;
	}
	return (-1);
}

int opcode_nop(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0;
	return (0);
}

int opcode_add(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x20; // op,shamt,funct
	if (add_reg(bytecode, arg1, 11) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source1 register
	if (add_reg(bytecode, arg3, 16) < 0)
		return (-1); // source2 register
	return (0);
}

int opcode_addi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x20000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source1 register
	if (add_imi(bytecode, (short int)strtol(arg3, NULL, 0)))
		return (-1); // constant
	return (0);
}

int opcode_andi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x30000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source1 register
	if (add_imi(bytecode, (short int)strtol(arg3, NULL, 0)))
		return (-1); // constant
	return (0);
}

int opcode_bne(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x14000000; // op
	if (add_reg(bytecode, arg1, 21) < 0)
		return (-1); // register1
	if (add_reg(bytecode, arg2, 16) < 0)
		return (-1); // register2
	if (add_lbl(offset, bytecode, arg3))
		return (-1); // jump
	return (0);
}

int opcode_srl(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x2; // op
	if (add_reg(bytecode, arg1, 11) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 16) < 0)
		return (-1); // source1 register
	if (add_sht(bytecode, atoi(arg3)) < 0)
		return (-1); // shift
	return (0);
}

int opcode_sll(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0; // op
	if (add_reg(bytecode, arg1, 11) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 16) < 0)
		return (-1); // source1 register
	if (add_sht(bytecode, atoi(arg3)) < 0)
		return (-1); // shift
	return (0);
}

int opcode_lui(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x3c000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_imi(bytecode, (short int)strtol(arg2, NULL, 0)))
		return (-1); // constant
	return (0);
}

int opcode_lw(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x8c000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source register
	if (add_imi(bytecode, (short int)strtol(arg3, NULL, 0)))
		return (-1); // constant
	return (0);
}

int opcode_sw(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0xAC000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source register
	if (add_imi(bytecode, (short int)strtol(arg3, NULL, 0)))
		return (-1); // constant
	return (0);
}

int opcode_lhu(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0x94000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source register
	if (add_imi(bytecode, (short int)strtol(arg3, NULL, 0)))
		return (-1); // constant
	return (0);
}

int opcode_sh(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3)
{
	*bytecode = 0xA4000000; // op
	if (add_reg(bytecode, arg1, 16) < 0)
		return (-1); // destination register
	if (add_reg(bytecode, arg2, 21) < 0)
		return (-1); // source register
	if (add_imi(bytecode, (short int)strtol(arg3, NULL, 0)))
		return (-1); // constant
	return (0);
}

/* These arrays can be used to make your make_bytecode code more readable. You can use the array opcode_funct to store pointers
 * to instruction serialization functions. You can then use the array opcode_str in order to match instruction
 * names to function pointers. */
const char *opcode_str[] = {"nop", "add", "addi", "andi", "bne", "srl", "sll", "lui", "lw", "sw", "lhu", "sh"};
opcode_function opcode_func[] = {&opcode_nop, &opcode_add, &opcode_addi, &opcode_andi, &opcode_bne, &opcode_srl, &opcode_sll, &opcode_lui, &opcode_lw, &opcode_sw, &opcode_lhu, &opcode_sh};

/* a function to print the state of the machine */
int print_registers()
{
	int i;
	printf("registers:\n");
	for (i = 0; i < MAX_REGISTER; i++)
	{
		printf(" %d: %d\n", i, registers[i]);
	}
	printf(" Program Counter: 0x%08x\n", pc);
	return (0);
}

/* a function to print the content of the memory in hex */
int print_memory(int start, int len)
{
	int i, end = start + len;
	printf("Memory region (0x%08x):\n\t\t", start);
	for (i = 0; i < 8; i++)
		printf("\tValue(+%02x)", (i * 4));
	printf("\n");
	for (; start < end; start += 0x20)
	{
		printf("0x%08x\t", start);
		for (i = 0; i < 8; i++)
			printf("\t0x%08x", ntohl(data[DATA_POS(start + i * 4)]));
		printf("\n");
	}
	return (0);
}

// THIS IS MY TASK STARTING POINT

/* function to execute bytecode */
int exec_bytecode()
{
	printf("EXECUTING PROGRAM ...\n");
	pc = ADDR_TEXT; // set program counter to the start of our program
	

	while(pc<ADDR_TEXT+(prog_len*4)) // pc IS THE ADDR_TEXT [FIRST LINE OF THE PROGRAM] | BASICALLY, THIS LOOP HAS THE CONDITION TO RUN TILL ADDR_TEXT IS LESS THAN ADDR_TEXT+(prog_len{TOTAL NUMBER OF LINES IN THE CODE} * 4 {AS PC INCREMENTS BY 4})
	{
		unsigned int tempByteCode = text[TEXT_POS(pc)];		// A TEMPORARY VARIABLE WITH BASE 10 REPRESENTATION OF THE BYTECODE
		unsigned int op_code = (tempByteCode >> 26) & 0x3F; //0xFC000000;     //0x3F // SHIFTING 26 TIMES TO FIND THE op_code AND MASK IT
		printf("Text curr:0x%08x\n", ADDR_TEXT + (4 * pc));
		printf("%08x\n", op_code);

		// NOW, I WILL BE CHECKING FOR R - TYPE INSTRUCTIONS, SINCE EVERY R - TYPE INSTRUCTION IS ALWAYS 0.

		//NOP check first 
		if (tempByteCode == 0)
		{
	 		// NOP operation - DOES NOTHING
	 		printf("nop");
	 		pc=pc+4;
	 	}

		else if (op_code == 0)
		{
			// 11111
			// 111110000
			// 11111000000
			unsigned int rs = (tempByteCode & 0x03E00000) >> 21;	 // SOURCE REGISTER 1 FOUND AND MASKED
			unsigned int rd = (tempByteCode & 0xF800) >> 11;	 // DESTINATION REGISTER FOUND AND MASKED
			unsigned int rt = (tempByteCode & 0x001F0000) >> 16;	 // SOURCE REGISTER 2 FOUND AND MAKED
			unsigned int shamt = (tempByteCode & 0x7C0) >> 6; // SHIFT AMOUNT FOUND AND MASKED
			unsigned int function = (tempByteCode) & 0x3F;	 // FUNCTION CODE FOUND [USE AND OPERATOR]
			//unsigned immediate = text[TEXT_POS(pc)] & 0xFFFF; // sign-extend 16-bit immediate.
			//unsigned int memory[4096];

			// ADD
			if (op_code == 0 && function == 0x20) // CHECKING IF THE GIVEN INSTRUCTION IS AN ADD
			{
				// ADD operation
				printf("register[rd]=%d\n", registers[rd]);
				printf("register[rs]=%d\n", registers[rs]);
				printf("register[rt]=%d\n", registers[rt]);
				printf("register[rs]+registers[rt]=%d\n", registers[rs]+registers[rt]);
				registers[rd] = registers[rs] + registers[rt]; // ADDS THE VALUE OF registers[rs] and registers[rt] AND, STORES IT IN registers[rd]
				printf("add");
				printf("%d", rd);
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4
			}


			// SRL
			if (op_code == 0 && function == 0x2) // CHECKING IF THE GIVEN INSTRUCTION IS AN SRL [ SHIFT RIGHT LOGICAL ]
			{
				// SRL operation
				registers[rd] = registers[rt] >> shamt; // PERFORM A LOGICAL RIGHT SHIFT OF THE VALUE IN registers[rt] AND, STORES IT IN THE DESTINATION REGISTER { registers[rd] }
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4
			}

			// SLL
			if (op_code == 0 && function == 0x0) // CHECKING IF THE GIVEN INSTRUCTION IS AN SLL [ SHIFT LEFT LOGICAL ]
			{
				// SLL operation
				registers[rd] = registers[rt] << shamt; // PERFORM A LOGICAL LEFT SHIFT OF THE VALUE IN registers[rt] AND, STORES IT IN THE DESTINATION REGISTER { registers[rd] }
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4
			}
		}

		else{

			unsigned int rs = (tempByteCode & 0x03E00000) >> 21;	 // SOURCE REGISTER 1 FOUND AND MASKED
			unsigned int rt = (tempByteCode & 0x001F0000) >> 16;	 // SOURCE REGISTER 2 FOUND AND MAKED
			signed immediate = text[TEXT_POS(pc)] & 0x0000FFFF; // sign-extend 16-bit immediate.

			// ADDI
			if (op_code == 0x08) // CHECKING IF THE GIVEN INSTRUCTION IS AN ADDI [ ADD IMMEDIATE ]
			{
				// ADDI operation
				if (immediate & 0x8000)
					{
						immediate = ((0xffff ^ immediate)+1) * -1; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
						printf("%d\n", immediate);
					}
				registers[rt] = registers[rs] + immediate; // BASICALLY ADDING THE registers[rs] WITH THE immediate AND STORING IT IN registers[rt]
				printf("addi");
			
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4
			}

			// ANDI
			if (op_code == 0x0C) // CHECKING IF THE GIVEN INSTRUCTION IS AN ANDI [ AND IMMEDIATE ]
			{
				// ANDI operation
				registers[rt] = registers[rs] & immediate; // BASICALLY MULTIPLYING THE registers[rs] WITH THE immediate AND STORING IT IN registers[rt]
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4
			}

			// LUI
			if (op_code == 0x0F) // CHECKING IF THE GIVEN INSTRUCTION IS AN LUI [ LOAD UPPER IMMEDIATE ]
			{
				// LUI operation

				if (immediate & 0x8000)
					{
						immediate = ((0xffff ^ immediate)+1) * -1; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
						printf("%d\n", immediate);
					}
				registers[rt] = immediate << 16; // TAKES THE immediate value, THEN LIFT - SHIFTS BY 16 - BITS, AND STORES THE VALUE IN registers[rt] | This operation effectively loads the immediate value into the upper 16 bits of the register, setting the lower 16 bits to zero.
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4
			}

			// BNE
			if (op_code == 0x05) // CHECKING IF THE GIVEN INSTRUCTION IS AN BNE [ BRANCH NOT EQUAL ]
			{
				
				// BNE operation
				if (registers[rs] != registers[rt]) // THE BRANCH IS TAKEN, IF THE VALUE IN THE 2 REGISTERS ARE NOT EQUAL
				{
					printf("%i\n", pc);
					//printf("%i", immediate&0x8000);
					if (immediate & 0x8000)
					{
						printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
						pc -= (0xffff - immediate) * 4; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
					}else{
						printf("?????????????????????????????????");
						pc += immediate*4; // ACTION OF BBRANCH NOT EQUAL  
						pc += 4; // INCREMENTS THE PROGRAM COUNTER BY 4 IF THE REGISTERS ARE UNEQUAL
					}
					
					printf("%i\n", pc);
					
				}
				else{
					pc+=4; // INCREMENTS THE PROGRAM COUNTER BY 4 ANYWAYS EVEN IF THE REGISTERS ARE EQUAL
				}
		
				
			}

			// LW
			if (op_code == 0x23) // CHECKING IF THE GIVEN INSTRUCTION IS AN LW [ LOAD WORD ]
			{
				printf("Error");
				// LW operation

				if (immediate & 0x8000)
					{
						immediate = ((0xffff ^ immediate)+1) * -1; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
						printf("%d\n", immediate);
					}
				registers[rt] = ntohl(data[DATA_POS((registers[rs]+immediate))]); // It computes the effective memory address by adding the value of registers[rs] and the immediate value, then accesses the data array at that memory location. The ntohl() function is used to convert MARS'S Big Endian to our Emulator's Little Endian.

				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4 ANYWAYS EVEN IF THE REGISTERS ARE EQUAL

			
			}

			// SW
			if (op_code == 0x2B) // CHECKING IF THE GIVEN INSTRUCTION IS AN SW [ STORE WORD ]
			{
				// SW operation
				if (immediate & 0x8000)
					{
						immediate = ((0xffff ^ immediate)+1) * -1; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
						printf("%d\n", immediate);
					}
				data[DATA_POS((registers[rs]+immediate))] = htonl(registers[rt]);  //  It computes the effective memory address by adding the value of registers[rs] and the immediate value, then stores the value in the register rt into the data array at that memory location.
				pc=pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4 ANYWAYS EVEN IF THE REGISTERS ARE EQUAL
			}

			
			// LHU
			if (op_code == 0x25) // CHECKING IF THE GIVEN INSTRUCTION IS AN LHU [ LOAD HALFWORD UNSIGNED ]
			{
				// LHU operation
				if (immediate & 0x8000)
					{
						immediate = ((0xffff ^ immediate)+1) * -1; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
						printf("%d\n", immediate);
					}
				int temporary = data[DATA_POS(registers[rs])]; // TAKING THE VALUE FROM THE DATA MEMORY AT THE SPECIFIC POSITION INDICATED BY THE ADDRESS STORED in registers[rs]
			
				int temporary1 = (temporary & 0x000000ff);       //
				int temporary2 = (temporary & 0x0000ff00) >> 8;  //  EXTRACTING INDIVIDUAL BYTES OF THE MEMORY USING AND OPERATION AND MASKING IT.  ALSO, I SHIFTED TO THE RIGHT
				int temporary3 = (temporary & 0x00ff0000) >> 16; //  EACH TEMPOARY VALUE STORES 1 BYTE OF THE MEMORY ADDRESS
				int temporary4 = (temporary & 0xff000000) >> 24; //

				temporary = (temporary1 << 24) + (temporary2 << 16) + (temporary3 << 8) + temporary4; // AS I BROKE THE temporary VALUE IN THE PREVIOUS LINE, NOW I AM REASSEMBLING THE BYTES BY SHIFTING EACH BYTE TO THEIR POSITION.

				if (registers[rs] % 4 == 2) // CHECKS IF THE rs REGISTER IS PARALLEL TO A MEMORY ADDRESS i.e, MULTIPLE OF 4 AS IT IS MIPS [I REQUIRED THE HELP OF GOOGLE]
				{
					registers[rt] = (temporary & 0xffff0000) >> 16; // IF IT IS, THAT MEANS THE ADDRESS IS AT THE SECOND BYTE OF A WORD.  SO< I MASK THE UPPER 16 - BITS and STORE IT.
				}
				
				else{

					registers[rt] = (temporary & 0x0000ffff); // ELSE, I MASK THE LOWER 16 - BITS
				}

				pc = pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4 ANYWAYS EVEN IF THE REGISTERS ARE EQUAL
				
				
			}

			// SH
			if (op_code == 0x29) // CHECKING IF THE GIVEN INSTRUCTION IS AN SH [ STORE HALFWORD ]
			{
				// SH operation
				if (immediate & 0x8000)
					{
						immediate = ((0xffff ^ immediate)+1) * -1; // 2'S COMPLEMENT TO CHECK IF THE NUMBERS ARE NEGATIVE
						printf("%d\n", immediate);
					}

					if ((registers[rs]+immediate) %4 == 2)
					{
						unsigned int second_half = ntohl(data[DATA_POS((registers[rs]+immediate))]) & 0xffff; // GETTING THE SECOND - HALF OF THE VALUE AND MASKING IT.
						data[DATA_POS((registers[rs]+immediate))] = htonl(registers[rt] << 16 + second_half); // SHIFTING rt VALUE TO THE FIRST 16 - BITS OF THE DATA POSITION, AND ADDING THE LAST 16 - BITS STORED IN THAT POSITION
					}else
					{
						unsigned int first_half = (ntohl(data[DATA_POS((registers[rs]+immediate))]) >> 16) << 16; // REVERSE OF THE if STATEMENT
						data[DATA_POS((registers[rs]+immediate))] = htonl((registers[rt] & 0xffff) + first_half);
					}

				pc = pc+4; // INCREMENTS THE PROGRAM COUNTER BY 4 ANYWAYS EVEN IF THE REGISTERS ARE EQUAL
			}

			

		}

		print_registers(); // SHOWING STEP - BY - STEP EXECUTIONS LIKE IN MARS
	    print_memory(ADDR_DATA, 0x40);
	}

	// print out the state of registers and memory at the end of execution
	print_registers();
	print_memory(ADDR_DATA, 0x40);

	printf("... DONE!\n");
	return (0);
}

// THE BELOW PART IS DONE

/* function to create bytecode */
int make_bytecode()
{
	unsigned int
	bytecode; // holds the bytecode for each converted program instruction
	int i, j = 0; // instruction counter (equivalent to program line)

	char label[MAX_ARG_LEN + 1];
	char opcode[MAX_ARG_LEN + 1];
	char arg1[MAX_ARG_LEN + 1];
	char arg2[MAX_ARG_LEN + 1];
	char arg3[MAX_ARG_LEN + 1];
	char data_line[MAX_LINE_LEN], *val;

	printf("\nASSEMBLING PROGRAM ...\n");
	printf("DATA AREA:\n");

	// First need to parse the data section and store the data in array data.
	i = 0;
	while (j < data_len)
	{
		if (sscanf(
				&data_str[j][0],
				"%" XSTR(MAX_ARG_LEN) "[^:]: .word %[^\n]",
				label, data_line) < 2)
		{ // parse the line with label
			printf("parse data error line %d %s %s\n", j, label, data_line);
			return (-1);
		}

		// parsing data entry values
		val = strtok(data_line, " ");
		while (val != NULL)
		{
			*(data + (i++)) = htonl(strtol(val, NULL, 0));
			printf("%08x: %08x\n", ADDR_DATA + i * 4, ntohl(*(data + i - 1)));
			val = strtok(NULL, " ");
		};
		j++;
	}

	j = 0;
	printf("TEXT AREA:\n");
	while (j < prog_len)
	{
		// These variables will store the elements of each instruction.
		memset(label, 0, sizeof(label));
		memset(opcode, 0, sizeof(opcode));
		memset(arg1, 0, sizeof(arg1));
		memset(arg2, 0, sizeof(arg2));
		memset(arg3, 0, sizeof(arg3));

		bytecode = 0;

		// First, parse and remove label.
		if ((val = strchr(&prog_str[j][0], ':')) != NULL)
		{ // check if the line contains a label
			*val = '\0';
			strcpy(label, &prog_str[j][0]);
			val++;
		}
		else
		{
			val = &prog_str[j][0];
		}

		// Parse a load/store instruction using an appropriate pattern match
		// We match a 2 strings up to MAX_ARG_LEN characters, a string of chars and a string
		// that should not contain a closing parentheses.
		// If we do not read 4 arguments, the instruction is not load/store
		if (sscanf(val, "%" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %[A-Fa-f0-9-](%[^)]",
				   opcode, arg1, arg3, arg2) < 4)
		{
			// Reset the last two arguments to empty strings
			memset(arg2, 0, sizeof(arg2));
			memset(arg3, 0, sizeof(arg3));

			// Scan using attern for any other supported instruction
			// The pattern scans for 4 string of max length MAX_ARG_LEN.
			// The scanning will return the number matched fields and our instructions
			// need at least two arguments.
			if (sscanf(val, "%" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
					   opcode, arg1, arg2, arg3) < 1)
			{ // parse the line with label
				printf("parse error line %d\n", j);
				return (-1);
			}
		}

		for (i = 0; i < MAX_OPCODE; i++)
		{
			if (!strcmp(opcode, opcode_str[i]) && ((*opcode_func[i]) != NULL))
			{
				if ((*opcode_func[i])(j, &bytecode, opcode, arg1, arg2, arg3) < 0)
				{
					printf("ERROR: line %d opcode error (assembly: %s %s %s %s)\n", j, opcode, arg1, arg2, arg3);
					return (-1);
				}
				else
				{
					printf("0x%08x 0x%08x\n", ADDR_TEXT + 4 * j, bytecode);
					text[j] = bytecode; // STORAGE - POINT OF BYTE-CODE
					break;
				}
			}
			if (i == (MAX_OPCODE - 1))
			{
				printf("ERROR: line %d unknown opcode\n", j);
				return (-1);
			}
		}

		j++;
	}
	printf("... DONE!\n");
	return (0);
}

// A helper function to trim whitespace from front and back of a string
char *trimwhitespace(char *str)
{
	char *end;

	// Trim leading space
	while (isspace((unsigned char)*str))
		str++;

	if (*str == 0) // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end))
		end--;

	// Write new null terminator character
	end[1] = '\0';

	return str;
}

/* loading the program into memory */
int load_program(char *filename)
{
	int j = 0;
	FILE *f;
	char line[MAX_LINE_LEN], *str;

	printf("LOADING PROGRAM %s ...\n", filename);

	f = fopen(filename, "r");
	if (f == NULL)
	{
		printf("ERROR: Cannot open program %s...\n", filename);
		return -1;
	}

	// skip any empty lines
	do
	{
		if (fgets(&line[0], MAX_LINE_LEN, f) == NULL)
		{
			fprintf(stderr, "ERROR: Input assembly file is empty\n");
			exit(1);
		}
		str = trimwhitespace(line);
	} while (!strlen(str));

	// Reading the data section of the file
	if (!strcmp(str, ".data"))
	{
		while (fgets(&line[0], MAX_LINE_LEN, f) != NULL)
		{

			// Remove whitespaces
			str = trimwhitespace(line);

			// Skip empty lines
			if (!strlen(str))
				continue;

			// terminate loop if beginning of data section found
			if (strcmp(str, ".text") == 0)
				break;
			strcpy(data_str[data_len++], str);
		}
	}

	// Reading the assembly text section

	// Skip the .text macro and get the first instruction
	if (!strcmp(str, ".text"))
	{
		if (fgets(&line[0], MAX_LINE_LEN, f) == NULL)
		{
			fprintf(stderr, "ERROR: Input assembly file has no instructions\n");
			exit(1);
		}
	}

	// copy instructions to final prog_str array
	do
	{
		str = trimwhitespace(line);
		if (!strlen(str))
			break;
		strcpy(prog_str[prog_len++], str);
	} while (fgets(&line[0], MAX_LINE_LEN, f) != NULL);

	printf("DATA:\n");
	for (j = 0; j < data_len; j++)
	{
		printf("%d: %s\n", j, &data_str[j][0]);
	}

	printf("PROGRAM:\n");
	for (j = 0; j < prog_len; j++)
	{
		printf("%d: %s\n", j, &prog_str[j][0]);
	}

	return (0);
}