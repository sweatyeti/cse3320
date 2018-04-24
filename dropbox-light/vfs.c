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
#include <time.h>
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

// the data blocks start at block 129
#define DATA_BLOCKS_START 129

// enable/disable debug output
bool DBG = false;

// create the virtual file system structure  
uint8_t vfs[NUM_BLOCKS][BLOCK_SIZE];

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
	time_t offsetTimeAdded;
} __attribute__((__packed__));

// define and init the global counter to keep track of the number of entries in the vfs
uint8_t numValidDirEntries = 0;

// create a pointer to the root directory
uint8_t (* rootDir)[BLOCK_SIZE] = vfs; 

// function declarations
bool initVirtFS( void );
void handleDf( void );
void handleList( void );
void handlePut ( char * );
bool tryPutFile( char *, int );
uint32_t getAmountOfFreeSpace( void );
int getIndexOfNextFreeBlock( void );

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

	if(!initVirtFS())
	{
		printf("There was a problem, and the program must exit. Please try again.\n");
		if(DBG)
		{
			printf("ERROR -> main(): initVirtFS() returned FALSE\n");
		}
		exit(EXIT_FAILURE);
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
				printf("     : main(): token[%d] = %s\n", token_index, tokens[token_index]);
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
			handlePut(tokens[1]);
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

bool initVirtFS()
{
	if(DBG)
	{
		printf("DEBUG: initVirtFS() starting...\n");
	}

	int i;
	for( i=DATA_BLOCKS_START; i<NUM_BLOCKS; i++)
	{
		freeBlocks[i] = 1;
	}


	if(DBG)
	{
		printf("DEBUG: initVirtFS() exiting...\n");
	}
	return true;
} // initVirtFS()

void handlePut( char * fileToAdd )
{
	if(DBG)
	{
		printf("DEBUG: handlePut() starting...\n");
	}

	// ensure a file was specified, warn and bail if not
	if(fileToAdd == NULL || (strlen(fileToAdd) == 0) )
	{
		printf("put error: Please enter a file name to put - ex. 'put foobar.txt'\n");
		return;
	}

	// check if the specified file name is too long, warn and bail if so
	if( strlen(fileToAdd) > MAX_FILENAME_LENGTH )
	{
		printf("put error: File name too long.\n");
		return;
	}

	// ensure we have not reached the max # of files in the system yet, warn and bail if so
	if( numValidDirEntries >= MAX_NUM_FILES )
	{
		printf("put error: the max number of files (%d) has been reached. ", MAX_NUM_FILES);
		printf("Please remove a file before attempting to PUT another.\n");
		return;
	}

	// grab and store the current working directory
	char * cwdBuf = NULL;
	cwdBuf = getcwd(NULL, 0);

	// use the current working dir to build the entire file path to feed to stat and fopen later
	char physicalFileToGet[ strlen(cwdBuf) + strlen(fileToAdd) +2 ];
	strcpy(physicalFileToGet,cwdBuf);
	strcat(physicalFileToGet,"/");
	strcat(physicalFileToGet,fileToAdd);
	free(cwdBuf);

	if(DBG)
	{
		printf("     : file to get: '%s'\n", physicalFileToGet);
	}

	// run stat() against the file, and warn and bail if there was a problem (i.e. FNF, etc.)
	struct stat fileStats;
	errno = 0;
	if( stat(physicalFileToGet, &fileStats) != 0 )
	{
		printf("put error: %s\n", strerror(errno) );
		return;
	}

	// store the file size as it gets used several times hereafter
	int fileSize = fileStats.st_size;

	// check if the given file is larger than the allowed size, warn and bail if so
	if( fileSize > MAX_FILE_SIZE )
	{
		printf("put error: file size exceeds the max allowed size.\n");
		return;
	}

	// check to ensure there's enough free space on the fs for the file, warn and bail if not
	if( fileSize > getAmountOfFreeSpace() )
	{
		printf("put error: Not enough disk space.\n");
		return;
	}

	// call the tryPutFile() function, which does the actual file copying work
	if( !tryPutFile(physicalFileToGet, fileSize) )
	{
		// if the PUT failed, inform and bail
		printf("put error: there was a problem, please try again.\n");
		return;
	}

	if(DBG)
	{
		printf("DEBUG: handlePut() exiting...\n");
	}

	return;
} // handlePut()

void handleDf()
{
  if(DBG)
  {
    printf("DEBUG: handleDf() starting...\n");
  }


  if(DBG)
  {
    printf("DEBUG: handleDf() exiting...\n");
  }
} // handleDf()

void handleList()
{
  if(DBG)
  {
    printf("DEBUG: handleList() starting...\n");
  }

	struct DirectoryEntry * dirEntry = NULL;

	int i;
	for( i=0; i<(MAX_NUM_FILES*sizeof(struct DirectoryEntry)); i+=sizeof(struct DirectoryEntry) )
	{
		dirEntry = (struct DirectoryEntry *) &rootDir[i];
		if((*dirEntry).isValid)
		{
			//printf("%-6d ")
			printf("valid\n");
		}
	}


  if(DBG)
  {
    printf("DEBUG: handleList() exiting...\n");
  }
} // handleList()

bool tryPutFile( char * pathToFile, int fileSize )
{
	if(DBG)
	{
		printf("DEBUG: tryPutFile() starting...\n");
	}

	// validate params
	if(pathToFile == NULL || (strlen(pathToFile) == 0) || fileSize < 0 )
	{
		if(DBG)
		{
			printf("ERROR -> tryPutFile(): provided file path is NULL or empty\n");
		}
		return false;
	}

	// try to open the given file
	errno = 0;
	FILE * fp = NULL;
	fp = fopen(pathToFile, "r");

	// warn and bail if there was a problem with fopen
	if( fp == NULL )
	{
		if(DBG)
		{
			printf("ERROR -> tryPutFile(): fopen failed with errno %d: %s\n", errno, strerror(errno));
		}
		return false;
	}
	else if(DBG)
	{
		printf("     : file opened successfully, attempting to read file of size %d into the fs...\n", fileSize);
	}

	int bytesToBeRead = fileSize;
	int fileOffset = 0;
	int blocksUsed[MAX_BLOCKS_PER_FILE];
	int counter = 0;

	while( bytesToBeRead > 0 )
	{
		int freeBlockIndex = getIndexOfNextFreeBlock();

		if(DBG)
		{
			printf("     : writing to block %d...\n", freeBlockIndex);
		}

		fseek( fp, fileOffset, SEEK_SET );

		int bytesRead = fread( vfs[freeBlockIndex], 1, BLOCK_SIZE, fp );

		if(DBG)
		{
			printf("     : file bytes read: %d\n", bytesRead);
		}

		if( bytesRead == 0 && !feof(fp) )
		{
			printf("An error occurred reading from the file. Please try again.\n");
			if(DBG)
			{
				printf("     : fread() returned 0 bytes and it was not EOF\n");
			}
			fclose(fp);
			return false;
		}

		clearerr(fp);

		bytesToBeRead -= bytesRead;

		fileOffset += BLOCK_SIZE;

		blocksUsed[counter] = freeBlockIndex;

		freeBlocks[freeBlockIndex] = 0;

		counter++;
	}

	if(DBG)
	{
		printf("     : file read successfully, creating directory entry...\n");
	}

	// close the opened file to release associated resources
	fclose(fp);

	// create the directory entry and add it to the directory here


	if(DBG)
	{
		printf("DEBUG: tryPutFile() exiting...\n");
	}

	// if we get here, then everything seemingly went OK, return successful
	return true;
} // tryPutFile()

uint32_t getAmountOfFreeSpace()
{
	if(DBG)
	{
		printf("DEBUG: getAmountOfFreeSpace() starting...\n");
	}

	int i;
	uint32_t count=0;
	for( i=DATA_BLOCKS_START; i<NUM_BLOCKS; i++)
	{
		if( freeBlocks[i] == 1 )
		{
			count++;
		}
	}	

	if(DBG)
	{
		printf("     : current free bytes: %d\n", count*BLOCK_SIZE);
		printf("DEBUG: getAmountOfFreeSpace() exiting...\n");
	}

	return count*BLOCK_SIZE;
} // getAmountOfFreeSpace()

int getIndexOfNextFreeBlock()
{
	int i;
	int index = -1;
	for( i=DATA_BLOCKS_START; i<NUM_BLOCKS; i++ )
	{
		if( freeBlocks[i] == 1 )
		{
			index = i;
			break;
		}
	}

	return index;
} // getIndexOfNextFreeBlock()
