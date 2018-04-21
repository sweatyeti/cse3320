/*
 * Name: Matt Hamrick
 * ID: 1000433109
 */

/* The MIT License (MIT)
 * 
 * 
 * Copyright (c) 2016, 2017 Trevor Bakker 
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
**/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// We want to split our command line up into tokens
// so we need to define what delimits our tokens.
// In this case  white space will separate the tokens on our command line
#define WHITESPACE " \t\n"

// The maximum command-line size
#define MAX_COMMAND_SIZE 255

// mfs has commands that accept 2 arguments at most
#define MAX_NUM_ARGUMENTS 2

// number of blocks the virt file sys will contain
#define NUM_BLOCKS 4226

// size in bytes of each block
#define BLOCK_SIZE 8192

// max number of files the vfs allows
#define MAX_NUM_FILES 128

// max individual file size in bytes
#define MAX_FILE_SIZE 259072

// the max length any given filename can be
#define MAX_FILENAME_LENGTH 32

// based on the max file size and block size, a file can consume no more than 32 blocks
#define MAX_BLOCKS_PER_FILE 32

// enable/disable debug output
bool DBG = false;

// create the virtual file system structure  
unsigned char vfs[NUM_BLOCKS][BLOCK_SIZE];

// this array keeps track of the free blocks
int freeBlocks[NUM_BLOCKS];

// define the struct that holds the index of data blocks for a file
struct inode 
{
  int dataBlocks[MAX_BLOCKS_PER_FILE];
  bool isValid;

}__attribute__((__packed__));

// define the struct that describes a directory entry in the vfs
struct DirectoryEntry
{
  char name[32];
  uint32_t size;
  bool isValid;
  uint8_t inodeBlockIndex;
} __attribute__((__packed__));

// define and init the main counter to keep track of the number of entries in the vfs
uint8_t numDirEntries = 0;

// function declarations
void handleDf( void );
void handleList( void );

int main( int argc, char *argv[] )
{
	// check for any configured cmdline options
	char c;
  while((c = getopt(argc,argv,"d"))!=-1) 
	{
    switch(c) {
      case 'd':
        DBG = true;
        break;
			default:
				break;
    }
  }

	if(DBG)
	{
		printf("DEBUG: main() starting (after getopt)...\n");
	}

	// allocate memory to hold the string entered by the user in the mfs shell
	char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

	// start the main loop
  while( true )
  {
		// Print out the mfs prompt
		printf ("mfs> ");

		// Read the command from the commandline.  The
		// maximum command that will be read is MAX_COMMAND_SIZE
		// This while command will wait here until the user
		// inputs something since fgets returns NULL when there
		// is no input */
		while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );
		
	
		// save the raw command, removing any \r or \n chars from the end, for later use
		char * rawCmd = strdup( cmd_str );
		rawCmd[strcspn(rawCmd, "\r\n")] = 0;

		// Parse input - use MAX...+1 because we need to accept 3 params PLUS the command
		char * tokens[MAX_NUM_ARGUMENTS+1];

		// ensure every element of the tokens array is NULL
		int i;
		for( i=0; i<MAX_NUM_ARGUMENTS+1; i++)
		{
			tokens[i] = NULL;
		}

		int token_count = 0;                                 
		
		// Pointer to point to the token
		// parsed by strsep
		char * arg_ptr;                                         
																														
		char * working_str  = strdup( cmd_str );            

		// we are going to move the working_str pointer so
		// keep track of its original value so we can deallocate
		// the correct amount at the end
		char * working_root = working_str;

		// Tokenize the input strings with whitespace used as the delimiter
		while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) && 
							(token_count<=MAX_NUM_ARGUMENTS))
		{
			tokens[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
			if( strlen( tokens[token_count] ) == 0 )
			{
				tokens[token_count] = NULL;
			}
				token_count++;
		}
		
		if(DBG)
		{
			int token_index = 0;
			for( token_index = 0; token_index < token_count; token_index ++ ) 
			{
				printf("DEBUG: main(): token[%d] = %s\n", token_index, tokens[token_index]);
			}
		}

		// if no command/text was submitted, restart the loop
		if(tokens[0] == NULL)
		{
			continue;
		}
		
		// store pointer to the first token (the command) for easy use
		char *command = tokens[0];

		// check for quit/exit commands and break out of main loop if received
		if( strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) 
		{
			// before exiting, ensure any open image is closed via cleanUp()
			//cleanUp();
			break;
		}

		// check the entered mfs command against known commands, and call the appropriate function
		// change this to switch/case? would break for the switch interfere with the loop?

		if( strcmp(command, "get") == 0)
		{
			//handleGet(tokens[1], tokens[2]);
			continue;
		}

    if( strcmp(command, "put") == 0)
		{
			//handlePut(tokens[1]);
			continue;
		}

    if( strcmp(command, "del") == 0)
		{
			//handleDel(tokens[1]);
			continue;
		}

		if( strcmp(command, "list") == 0)
		{
			handleList();
			continue;
		}

		if( strcmp(command, "df") == 0)
		{
			handleDf();
			continue;
		}

		if( strcmp(command, "dbg") == 0)
		{
			// enable or disable dbg output via the program itself
			DBG = !DBG;
			printf("Debug output ");
			if(DBG)
			{
				printf("enabled\n");
			}
			else
			{
				printf("disabled\n");
			}
			continue;
		}
	}// main loop

	if(DBG)
	{
		printf("DEBUG: main() exiting...\n");
	}

	exit(EXIT_SUCCESS);

} // main

void handleDf()
{
  if(DBG)
  {
    printf("DEBUG: handleDf() starting...\n");
  }


  if(DBG)
  {
    printf("DEBUG: handleDf() ending...\n");
  }
}

void handleList()
{
  if(DBG)
  {
    printf("DEBUG: handleList() starting...\n");
  }


  if(DBG)
  {
    printf("DEBUG: handleList() ending...\n");
  }
}
