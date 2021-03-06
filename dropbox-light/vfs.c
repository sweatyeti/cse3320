/*
 * Name: Matt Hamrick
 * ID: 1000433109
 * Description: create a usable, an in-memory virtual file system which files can be saved to, removed from, retrieved from, and listed 
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

// the inodes start at block 1
#define INODE_BLOCKS_START 1

// enable/disable debug output
bool DBG = false;

// create the virtual file system structure  
uint8_t vfs[NUM_BLOCKS][BLOCK_SIZE];

// this array keeps track of the free blocks
int freeBlocks[NUM_BLOCKS];

// define the struct that describes a directory entry in the vfs
struct DirectoryEntry
{
  char name[MAX_FILENAME_LENGTH+1];
  uint32_t size;
  bool isValid;
  uint8_t inodeBlockIndex;
	time_t offsetTimeAdded;
} __attribute__((__packed__));

// define the struct that holds the index of data blocks for a file
struct inode 
{
  int dataBlocks[MAX_BLOCKS_PER_FILE];
  bool isValid;

}__attribute__((__packed__));

// declare an array of pointers to DirectoryEntry structs to easily access any of the entries
// the array itself starts at vfs[0], so the same memory essentially has two accessible names:
// first name is vfs[0], second name is rootDirEntries
struct DirectoryEntry (* rootDirEntries)[MAX_NUM_FILES] = (struct DirectoryEntry (*)[MAX_NUM_FILES]) vfs[0];

// function declarations
bool initVirtFS( void );
void handleDf( void );
void handleList( void );
void handlePut( char * );
void handleDel( char * );
void handleGet( char *, char * );
bool tryPutFile( char *, char *, int );
bool tryDelFile( struct DirectoryEntry * );
bool tryGetFile( struct DirectoryEntry *, char * );
void createDirectoryEntry( char *, int, int * );
uint32_t getAmountOfFreeSpace( void );
int getIndexOfNextFreeBlock( void );
int getIndexOfNextFreeDirEntry( void );
struct inode * getInode( uint8_t );

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
			//cleanUp();
			break;
		}

		// check the entered mfs command against known commands, and call the appropriate function
		// change this to switch/case?

		if( strcmp(command, "get") == 0)
		{
			handleGet(tokens[1], tokens[2]);
			continue;
		}

    if( strcmp(command, "put") == 0)
		{
			handlePut(tokens[1]);
			continue;
		}

    if( strcmp(command, "del") == 0)
		{
			handleDel(tokens[1]);
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

		// custom, undocumented command to enable/disable debug output on the fly
		if( strcmp(command, "dbg") == 0)
		{
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

/*
 * function: 
 *  initVirtFS
 * 
 * description: 
 *  initializes the freeBlocks index and all the root directory entries and inodes as invalid
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  bool: true if there were no issues during initialization; false otherwise
 */
bool initVirtFS()
{
	if(DBG)
	{
		printf("DEBUG: initVirtFS() starting...\n");
	}

	// when the program is just starting out, every data block is 
	// free, so mark the associated index array with all 1's to indicate this
	if(DBG)
	{
		printf("     : initVirtFS(): marking all data blocks as free...");
	}
	int i;
	for( i=DATA_BLOCKS_START; i<NUM_BLOCKS; i++)
	{
		freeBlocks[i] = 1;
	}
	if(DBG)
	{
		printf("finished\n");
	}

	// initialize all root dir entries and inodes
	// they are marked as invalid to start, and marked valid later as files are PUT in
	if(DBG)
	{
		printf("     : initVirtFS(): initializing root dir entries...\n");
	}
	for( i=0; i<MAX_NUM_FILES; i++)
	{
		rootDirEntries[i]->isValid = false;
		rootDirEntries[i]->inodeBlockIndex = i + INODE_BLOCKS_START;
		//struct inode * inodePtr = getInode(rootDirEntries[i]->inodeBlockIndex);
		//inodePtr->isValid = false;
	}
	if(DBG)
	{
		printf("DEBUG: initVirtFS(): initializing root dir entries...finished\n");
	}


	if(DBG)
	{
		printf("DEBUG: initVirtFS() exiting...\n");
	}

	// if we get here then all is well, return successful
	return true;

} // initVirtFS()

/*
 * function: 
 *  handlePut
 * 
 * description: 
 *  takes the user input for the file to add to the FS and runs validations against it, such as 
 *  ensuring it exists. First checks to ensure there is enough space and such available for 
 *  the new file. If all is validated, then it calls tryPutFile() to perform the actual work. 
 * 
 * parameters:
 *  char * fileToAdd: the name of the file, specified by the user input, to add to the FS
 * 
 * returns: 
 *  void
 */
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
	if( getIndexOfNextFreeDirEntry() == -1 )
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

	// we're finished with the cwdBuf from getcwd() above, so release its resources
	free(cwdBuf);

	if(DBG)
	{
		printf("     : handlePut(): file to get: '%s'\n", physicalFileToGet);
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
	if( !tryPutFile( fileToAdd, physicalFileToGet, fileSize) )
	{
		// if the PUT failed, inform and bail
		printf("put error: There was a problem, please try again.\n");
		return;
	}
	else
	{
		printf("put: File added successfully.\n");
	}

	if(DBG)
	{
		printf("DEBUG: handlePut() exiting...\n");
	}

	return;
} // handlePut()

/*
 * function: 
 *  handleDf
 * 
 * description: 
 *  displays the amount of available space
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  void
 */
void handleDf()
{
  if(DBG)
  {
    printf("DEBUG: handleDf() starting...\n");
  }

	// this function doesn't really do any work, there's already a function that
	// calculates the amount of free space, so output that amount directly
	printf("%d bytes free.\n", getAmountOfFreeSpace());

  if(DBG)
  {
    printf("DEBUG: handleDf() exiting...\n");
  }
	return;
} // handleDf()

/*
 * function: 
 *  handleList
 * 
 * description: 
 *  iterates through each DirectoryEntry and outputs the necessary info if the entry is valid 
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  void
 */
void handleList()
{
  if(DBG)
  {
    printf("DEBUG: handleList() starting...\n");
  }

	// flag to see if any entries were displayed when navigating through them
	// starts as false, and is only set to true if at least one entry exists
	bool entriesExist = false;

	// to attempt to be safe with memory, specify the max length for how 
	// long the datetime string will be at most
	uint16_t dtMaxLength = 15;

	// alloc mem for the string that holds the date & time characters
	char * datetimeStr = (char*) malloc(dtMaxLength);

	int i;
	for( i=0; i<MAX_NUM_FILES; i++ )
	{
		if( rootDirEntries[i]->isValid )
		{
			if(DBG)
			{
				printf("     : current entry %d valid value: %d\n", i, rootDirEntries[i]->isValid);
			}	

			// ensure for this iteration of the loop that we have teh orig max length value
			dtMaxLength = 15;

			// set the flag
			entriesExist = true;

			// print the size with a width of 7 characters, left justified
			printf("%-7d ", rootDirEntries[i]->size);

			// convert the stored time offset to a localized time struct
			struct tm * locTime = localtime(&(rootDirEntries[i]->offsetTimeAdded));

			// var to hold the # chars stored in the string to be displayed from strftime() call
			uint16_t dtNumChars = 0;

			// this loop's purpose is to ensure enough memory is allocate for the strftime() call
			while(true)
			{
				// create the formatted date/time string that will get output
				dtNumChars = strftime(datetimeStr, dtMaxLength, "%b %d %R", locTime);

				// that call returns 0 if the # chars needed exceeds the dtMaxLength
				if(dtNumChars == 0)
				{
					if(DBG)
					{
						printf("     : handleList(): reallocating more memory for the datetime string..\n");
					}
					
					// if the max length was exceeded, double the allotted max size and reallocate memory
					dtNumChars+=dtNumChars;
					datetimeStr = (char*) realloc(datetimeStr, dtNumChars);
					continue;
				}
				else
				{
					// if we get there, then there was enough memory to save the datetime string, output it
					printf("%s ", datetimeStr);
					break;
				}
			}
			
			// output the file name now
			printf("%s\n", rootDirEntries[i]->name);

			if(DBG)
			{
				// for debug output, include the used data blocks on the next line
				struct inode * inodePtr = getInode(rootDirEntries[i]->inodeBlockIndex);
				printf("DBG: used data blocks for file above:\n[");
				int j;
				for( j=0; j<MAX_BLOCKS_PER_FILE; j++ )
				{
					if(inodePtr->dataBlocks[j] != -1)
					{
						printf(" %d", inodePtr->dataBlocks[j]);
					}
				}
				printf(" ]\n");
			}

		}
	}

	// since malloc was used for datetimeStr, free it since we're done with it
	free(datetimeStr);

	// if we've gone through the whole list of entries and the bool was never set to true,
	// then no entries were valid, thus none exist, so inform the user
	if(!entriesExist)
	{
		printf("list: No files found.\n");
	}

  if(DBG)
  {
    printf("DEBUG: handleList() exiting...\n");
  }
	return;
} // handleList()

/*
 * function: 
 *  handleDel
 * 
 * description: 
 *  takes the user input for the file to remove from the FS and runs validations against it, such as 
 *  ensuring it exists. If all is validated, then it calls tryDelFile() to perform the actual work. 
 * 
 * parameters:
 *  char * fileToDel: the name of the file, specified by the user input, to remove from the FS
 * 
 * returns: 
 *  void
 */
void handleDel( char * fileToDel )
{
	if(DBG)
  {
    printf("DEBUG: handleDel() starting...\n");
  }

	// ensure a file was specified, warn and bail if not
	if(fileToDel == NULL || (strlen(fileToDel) == 0) )
	{
		printf("del error: Please enter a file name to delete - ex. 'del foobar.txt'\n");
		return;
	}

	// flag to set if a matching file name was found
	bool fileFound = false;

	// this flag will be set if a file was actually deleted
	bool fileDeleted = false;

	// loop through each dir entry
	int i;
	for( i=0; i<MAX_NUM_FILES; i++)
	{
		// check if the dir entry is valid
		if( rootDirEntries[i]->isValid == true )
		{
			// check the name of the valid entry to see if it matches the user input
			if( strcmp(fileToDel, rootDirEntries[i]->name) == 0 )
			{
				// set the flag
				fileFound = true;

				// try to delete the file
				fileDeleted = tryDelFile( rootDirEntries[i] );
				
				// break the loop since we found a file that matched
				break;
			}
		}
	}

	if(fileFound && fileDeleted)
	{
		if(DBG)
		{
			printf("     : handleDel(): file deleted\n");
		}
	}
	else if(fileFound && !fileDeleted)
	{
		// the file was found, but it wasn't deleted for some reason, alert the user
		printf("del error: There was a problem deleting the file. Please try again.\n");
	}
	else
	{
		// the file was not found, inform the user
		printf("del error: File not found.\n");
	}

	if(DBG)
  {
    printf("DEBUG: handleDel() exiting...\n");
  }
	return;
} // handleDel()

/*
 * function: 
 *  handleGet
 * 
 * description: 
 *  takes the user input(s) for the file to retrieve from the FS and runs validations against it,
 *  such as ensuring it exists, and if a file can be created in the current working directory. 
 *  If all is validated, then it calls tryPutFile() to perform the actual work. 
 * 
 * parameters:
 *  char * fileToGet: the name of the file, specified by the user input, to download from the FS
 *  char * newFilename: if specified, indicates the name of the output file
 * 
 * returns: 
 *  void
 */
void handleGet( char * fileToGet, char * newFilename )
{
	if(DBG)
  {
    printf("DEBUG: handleGet() starting...\n");
  }

	// ensure a file was specified, warn and bail if not
	if(fileToGet == NULL || (strlen(fileToGet) == 0) )
	{
		printf("get error: Please enter a file name to get - ex. 'get foobar.txt'\n");
		return;
	}

	// flag to set if a matching file name was found
	bool fileFound = false;

	// flag to set if the file was retrieved successfully
	bool fileRetrieved = false;

	int i;
	for( i=0; i<MAX_NUM_FILES; i++ )
	{
		if( rootDirEntries[i]->isValid )
		{
			// check the name of the valid entry to see if it matches the user input
			if( strcmp(fileToGet, rootDirEntries[i]->name) == 0 )
			{
				// set the flag
				fileFound = true;

				// try to get the file
				fileRetrieved = tryGetFile(rootDirEntries[i], newFilename );

				// break the loop since we found a file that matched
				break;
			}
		}
	}
	
	if(fileFound && fileRetrieved && DBG)
	{
		printf("     : handleGet(): file retrieved\n");
	}
	else if(fileFound && !fileRetrieved)
	{
		// the file was found, but it wasn't retrieved for some reason, alert the user
		printf("get error: There was a problem getting the file. Please try again.\n");
	}
	else if(!fileFound)
	{
		// the file was not found, inform the user
		printf("get error: File not found.\n");
	}

	if(DBG)
  {
    printf("DEBUG: handleGet() exiting...\n");
  }
	return;

} // handleGet()

/*
 * function: 
 *  tryPutFile
 * 
 * description: 
 *  tries to read the specified file from the host FS, and place it into the virtual
 *  FS. Informs the caller whether or not it was successful via return bool. 
 * 
 * parameters:
 *  char * fileName: the name of the file, specified by the user input, to add to the FS
 *  char * pathToFile: the full path of the file (filename included)
 *  int fileSize: the size of the file to put
 * 
 * returns: 
 *  bool: true if the file was able to be put into the FS; false otherwise
 */
bool tryPutFile( char * fileName, char * pathToFile, int fileSize )
{
	if(DBG)
	{
		printf("DEBUG: tryPutFile() starting...\n");
	}

	// validate params
	if(fileName == NULL || pathToFile == NULL || (strlen(pathToFile) == 0) || fileSize < 0 )
	{
		if(DBG)
		{
			printf("ERROR -> tryPutFile(): invalid parameter, bailing..\n");
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
		printf("     : tryPutFile(): file opened successfully, attempting to read file of size %d into the fs...\n", fileSize);
	}

	// initialize the counter that keeps track of how many bytes need to be read.
	// this gets decremented in the loop below 
	int bytesToBeRead = fileSize;

	// intialize the counter that keeps track of where we are in the file to be read
	// this gets incremented in the loop below as we get further into the file
	int fileOffset = 0;

	// declare and init the int array that stores which blocks a file is using
	// initialize with -1 since that block is nonexistent, so it's a good tail indicator
	int blocksUsed[MAX_BLOCKS_PER_FILE];
	int i;
	for( i=0; i<MAX_BLOCKS_PER_FILE; i++ )
	{
		blocksUsed[i] = -1;
	}

	// init a a counter that keeps track of how many times the loop iterates
	// this counter is used specifically for the blocksUsed array to help keep track of those
	int counter = 0;

	// this is the loop that attempts to read all bytes from the input file
	while( bytesToBeRead > 0 )
	{	

		// get and store the index location of the next free block; it will get the data from the file
		int freeBlockIndex = getIndexOfNextFreeBlock();

		if(DBG)
		{
			printf("     : tryPutFile(): writing to block %d...", freeBlockIndex);
		}

		// navigate to the offset location in the file 
		fseek( fp, fileOffset, SEEK_SET );

		// clear any errors in the file stream before attempting to read
		clearerr(fp);

		// attempt to read the file in chunks of BLOCK_SIZE, store how many bytes were actually read
		int bytesRead = fread( vfs[freeBlockIndex], 1, BLOCK_SIZE, fp );

		if(DBG)
		{
			printf("done\n");
			printf("     : tryPutFile(): file bytes read: %d\n", bytesRead);
		}

		// if 0 bytes were read, yet it's not EOF, then there was a problem, warn and bail
		if( bytesRead == 0 && !feof(fp) )
		{
			printf("An error occurred reading from the file. Please try again.\n");
			// BUGBUG: need to undo any work that was done here if a problem happens
			if(DBG)
			{
				printf("     : tryPutFile(): fread() returned 0 bytes and it was not EOF\n");
			}
			// since we're bailing, close the file to release associated resources
			fclose(fp);
			return false;
		}

		// we've read x # bytes, so decrement the bytesToBeRead counter
		// this will keep decrementing until it reaches 0, which breaks the loop
		bytesToBeRead -= bytesRead;

		// since we've read a BLOCK_SIZE amount of bytes potentially, increase our offset by that much
		fileOffset += BLOCK_SIZE;

		// add the index of the block containing the data to the index array for the inode
		blocksUsed[counter] = freeBlockIndex;

		// mark the free block we used as no longer free
		freeBlocks[freeBlockIndex] = 0;

		// increase the loop counter, and continue with the loop
		counter++;
	}

	if(DBG)
	{
		printf("     : tryPutFile(): file read successfully, creating directory entry...\n");
	}

	// close the opened file to release associated resources
	fclose(fp);

	// create the dir entry and associated inode, and insert the entry into the directory
	createDirectoryEntry( fileName, fileSize, blocksUsed );
	
	if(DBG)
	{
		printf("DEBUG: tryPutFile() exiting...\n");
	}

	// if we get here, then everything seemingly went OK, return successful
	return true;

} // tryPutFile()

/*
 * function: 
 *  tryDelFile
 * 
 * description: 
 *  tries to remove the file from the virtual FS, and free-up/invalidate the associated 
 *  data blocks and directory entry. Informs the caller whether or not it was successful
 *  via return bool. 
 * 
 * parameters:
 *  struct DirectoryEntry * entryPtr: the entry representing the file to be deleted
 * 
 * returns: 
 *  bool: true if the delete was successful; false otherwise
 */
bool tryDelFile( struct DirectoryEntry * entryPtr )
{
	if(DBG)
	{
		printf("DEBUG: tryDelFile starting...\n");
	}

	struct inode * inodePtr = getInode(entryPtr->inodeBlockIndex);

	// copy the used data blocks for the temp blocks array into the memory inside the fs
	int i;
	for( i=0; i<MAX_BLOCKS_PER_FILE; i++ )
	{
		// if the current dataBlocks[i] value is != -1, then mark the i block as free
		if( inodePtr->dataBlocks[i] != -1 )
		{
			if(DBG)
			{
				printf("     : tryDelFile(): marking data block %d as free...", inodePtr->dataBlocks[i]);
			}
			freeBlocks[inodePtr->dataBlocks[i]] = 1;
			if(DBG)
			{
				printf("done\n");
			}
		}
		else
		{
			// break if the current block is -1, since that indicates the end of the used blocks
			if(DBG)
			{
				printf("     : tryDelFile(): all used blocks freed...\n");
			}
			break;
		}		
	}

	// free the inode
	inodePtr->isValid = false;

	// free the DirectoryEntry
	entryPtr->isValid = false;

	if(DBG)
	{
		printf("     : tryDelFile(): inode and DirectoryEntry (entry ID %d) marked invalid/free\n", entryPtr->inodeBlockIndex);
		printf("DEBUG: tryDelFile exiting...\n");
	}

	// if we got here, then everything above was successful, return true
	return true;

} // tryDelFile()

/*
 * function: 
 *  tryGetFile
 * 
 * description: 
 *  tries to retrieve the file from the virtual FS and save it to the host FS. 
 *  Informs the caller whether or not it was successful via return bool. 
 * 
 * parameters:
 *  struct DirectoryEntry * entryPtr: the entry representing the file to be retrieved
 *  char * newFilename: if specified, represents the name to save the file as
 * 
 * returns: 
 *  bool: true if the retrieval was successful; false otherwise
 */
bool tryGetFile( struct DirectoryEntry * entryPtr, char * newFilename )
{
	if(DBG)
  {
    printf("DEBUG: tryGetFile() starting...\n");
  }

	// set flag if user wants to rename the outgoing file
	bool setNewFilename = false;
	if(newFilename != NULL && strlen(newFilename) > 0 )
	{
		// set the flag to true if something at all was provided in the 2nd param
		setNewFilename = true;
	}

	// create var to hold the actual name of the output file
	char * filename;

	// the filename gets initialized to either the original file name, or the user's desired new name
	if(setNewFilename)
	{
		filename = newFilename;
	}
	else
	{
		filename = entryPtr->name;
	}

	// grab and store the current working directory
	char * cwdBuf = NULL;
	cwdBuf = getcwd(NULL, 0);

	// build the entire file path, which gets sent to fopen later
	char outFilePathAndName[ strlen(cwdBuf) + strlen(filename) +2 ];
	strcpy(outFilePathAndName,cwdBuf);
	strcat(outFilePathAndName,"/");
	strcat(outFilePathAndName,filename);

	// done with provided cwdBuf, free it
	free(cwdBuf);

	if(DBG)
	{
		printf("     : tryGetFile(): file to write: %s\n", outFilePathAndName );
	}

	// get the entry's associated inode
	struct inode * inodePtr = getInode(entryPtr->inodeBlockIndex);

	// attempt to open the file for writing
	errno = 0;
	FILE * fp;
	fp = fopen( outFilePathAndName, "w" );

	// check to ensure the file opened, return false if it didn't since we cannot continue
	if(fp == NULL)
	{
		if(DBG)
		{
			printf("ERROR -> tryGetFile(): fopen failed with error %d: %s\n", errno, strerror(errno));
		}
		return false;
	}

	// init the vars that help keep track of file writing
	int fileSize = entryPtr->size;
	int numBytesLeft = fileSize;
	int numBytesSaved = 0;

	// this flag gets set to true if the write is fully successful
	bool writeSuccessful = false;

	if(DBG)
	{
		printf("     : tryGetFile(): attempting to retrieve the file...\n");
	}

	// start the loop that writes the file
	int i;
	for( i=0; i<MAX_BLOCKS_PER_FILE; i++)
	{
		// retrieve the current block index
		int dataBlockIdx = inodePtr->dataBlocks[i];

		// check for bad scenario (shouldn't happen) when there are no more data blocks left,
		// but we have not written the right amount of data
		if( dataBlockIdx == -1 && numBytesSaved != fileSize )
		{
			// if we get here, we need to bail out, set the flag and do so
			writeSuccessful = false;
			break;
		}

		int count = 0;
		int numBytesToWrite = 0;

		if( numBytesLeft <= BLOCK_SIZE )
		{
			numBytesToWrite = numBytesLeft;
		}
		else
		{
			numBytesToWrite = BLOCK_SIZE;
		}

		if(DBG)
		{
			printf("     : reading %d bytes from data block %d...\n", numBytesToWrite, dataBlockIdx);
		}

		clearerr(fp);
		count = fwrite( vfs[dataBlockIdx], 1, numBytesToWrite, fp );

		// check to see if the numbers don't match up or if there was an error in the stream
		if( count != numBytesToWrite || ferror(fp) )
		{
			if(DBG)
			{
				printf("ERROR -> tryGetFile(): error while writing file, aborting\n");
			}
			// if there was an error, then set the flag and bail out
			writeSuccessful = false;
			break;
		}

		// update the vars used to track byte counts
		numBytesSaved += count;
		numBytesLeft -= count;

		// check if we're finishing writing the file
		if( numBytesSaved == fileSize && numBytesLeft == 0 )
		{
			// everything is good, set the flag and break out of the loop
			writeSuccessful = true;
			break;
		}

	}

	// close the file since we're done with it
	fclose(fp);

	if(writeSuccessful)
	{
		if(DBG)
		{
			printf("     : tryGetFile(): file write successful\n");
		}
	}
	else
	{
		// if something bad happened, then attempt to delete the file that was created
		if(DBG)
		{
			printf("ERROR -> tryGetFile(): file write unsuccessful, deleting created file..\n");
		}

		// reset the errno, then attempt to delete the file
		errno = 0;
		remove(outFilePathAndName);

		// not a big deal if the actual file wasn't deleted, only warn if debug output is enabled, 
		// otherwise just keep going
		if( errno != 0 && DBG )
		{
			printf("ERROR -> tryGetFile(): failed to delete corrupted file, error: %d: %s\n", errno, strerror(errno));
		}
	}
	
	if(DBG)
  {
    printf("DEBUG: tryGetFile() exiting...\n");
  }

	return writeSuccessful;

} // tryGetFile()

/*
 * function: 
 *  createDirectoryEntry
 * 
 * description: 
 *  populates the next free DirectoryEntry's info with and the associated inode when a 
 *  file is placed into the FS
 * 
 * parameters:
 *  char * name: the name of the entry in teh FS
 *  int size: the size of the file
 *  int blocks[]: array containing the data blocks used by the file
 * 
 * returns: 
 *  void
 */
void createDirectoryEntry( char * name, int size, int blocks[] )
{
	if(DBG)
	{
		printf("DEBUG: createDirectoryEntry() starting...\n");
	}

	// get the index of the next free DirectoryEntry in the FS
	int idx = getIndexOfNextFreeDirEntry();

	// if we got -1 back, which shouldn't happen, then die
	if(idx == -1)
	{
		printf("There was a problem and the program needs to exit.\n");
		if(DBG)
		{
			printf("     : createDirectoryEntry(): getIndexOfNextFreeDirEntry() returned -1, ");
			printf("this should not happen here, and indicates an unrecoverable problem.\n");
		}
		exit(EXIT_FAILURE);
	}

	if(DBG)
	{
		printf("     : createDirectoryEntry(): assigning entry values...");
	}

	// set the appropriate entry's data, starting with the provided file name
	strcpy( rootDirEntries[idx]->name, name );

	// store the file's size, retrieved from the file's stats
	rootDirEntries[idx]->size = size;

	// mark the entry as valid
	rootDirEntries[idx]->isValid = true;

	// retrieve the time offset of this moment
	rootDirEntries[idx]->offsetTimeAdded = time(NULL);
	
	if(DBG)
	{
		printf("done\n");
	}

	// retrieve the associated inode for this particular DirectoryEntry
	struct inode * inodePtr = getInode(rootDirEntries[idx]->inodeBlockIndex);

	if(DBG)
	{
		printf("     : createDirectoryEntry(): assigning inode values...");
	}

	// mark the inode as valid
	inodePtr->isValid = true;

	// copy the used data blocks for the temp blocks array into the memory inside the fs
	int i;
	for( i=0; i<MAX_BLOCKS_PER_FILE; i++ )
	{
		// copy the value to fs memory
		inodePtr->dataBlocks[i] = blocks[i];
	}
	if(DBG)
	{
		printf("done\n");
	}

	if(DBG)
	{
		printf("DEBUG: createDirectoryEntry() exiting...\n");
	}
	return;
} // createDirectoryEntry()

/*
 * function: 
 *  getAmountOfFreeSpace
 * 
 * description: 
 *  iterates through teh freeBlocks index array and increments a counter if the block is free;
 *  once the loop is finished, it returns the product of the counter and BLOCK_SIZE 
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  uint32_t: the # of free blocks * the BLOCK_SIZE, in bytes - aka the amount of free space
 */
uint32_t getAmountOfFreeSpace()
{
	if(DBG)
	{
		printf("DEBUG: getAmountOfFreeSpace() starting...\n");
	}

	// starting at the first data block index, check each block index (via the index array) 
	// to see if the associated data block is free. If it is, increment the count variable
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
		printf("     : getAmountOfFreeSpace(): current free bytes: %d\n", count*BLOCK_SIZE);
		printf("DEBUG: getAmountOfFreeSpace() exiting...\n");
	}

	// return the product of how many blocks are free and the BLOCK_SIZE, which is the 
	// amount of overall free space in bytes
	return count*BLOCK_SIZE;

} // getAmountOfFreeSpace()

/*
 * function: 
 *  getIndexOfNextFreeBlock
 * 
 * description: 
 *  iterates through the freeBlocks index array from the beginning and returns the next 
 *  free block index
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  int: the index of the next free block
 */
int getIndexOfNextFreeBlock()
{
	if(DBG)
	{
		printf("DEBUG: getIndexOfNextFreeBlock() starting...\n");
	}
	// starting at the first data block index, check each block index (via the index array) 
	// to see if the associated data block is free. If it is, return the index of that free block
	// to the caller
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

	if(DBG)
	{
		printf("     : getIndexOfNextFreeBlock(): next free block idx: %d\n", index);
		printf("DEBUG: getIndexOfNextFreeBlock() exiting...\n");
	}
	return index;

} // getIndexOfNextFreeBlock()

/*
 * function: 
 *  getIndexOfNextFreeDirEntry
 * 
 * description: 
 *  iterates through each DirectoryEntry from the beginning and checks if it's valid (used)
 *  or not valid (unused), and returns the index of the first unused/free one
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  int: the index of the first free DirectoryEntry
 */
int getIndexOfNextFreeDirEntry()
{
	if(DBG)
	{
		printf("DEBUG: getIndexOfNextFreeDirEntry() starting...\n");
	}

	// set a default value of -1 so it gets returned if there are no free entries
	// this indicates every entry up to the max is valid, and thus we cannot accept more
	int index = -1;

	// iterate through the dir entries array to check which is the next free (aka invalid) one
	int i;
	for( i=0; i<MAX_NUM_FILES; i++)
	{
		if( rootDirEntries[i]->isValid == false )
		{
			// if we found an entry that is not valid, set the index var and break out of the loop
			// (this means the entry is free for use)
			index = i;
			break;
		}
	}

	if(DBG)
	{
		printf("     : getIndexOfNextFreeDirEntry(): next free dir entry idx: %d\n", index);
		printf("DEBUG: getIndexOfNextFreeDirEntry() exiting...\n");
	}
	return index;

} // getIndexOfNextFreeDirEntry()

/*
 * function: 
 *  getInode
 * 
 * description: 
 *  given a DirectoryEntry index, returns a pointer to the associated inode
 * 
 * parameters:
 *  uint8_t entry: the index of the entry to retrieve the inode for
 * 
 * returns: 
 *  struct inode *: a pointer to the appropriate inode
 */
struct inode * getInode( uint8_t entry )
{
	if(DBG)
	{
		printf("DEBUG: getInode() starting...\n");
	}

	// instantiate the pointer and initialize to NULL
	struct inode * ptr = NULL;

	// get the address  of the appropriate inode block, cast it to the inode *, and set the var
	ptr = (struct inode *) &vfs[INODE_BLOCKS_START + entry];

	if(DBG)
	{
		printf("DEBUG: getInode() exiting...\n");
	}

	// return the ptr, it points directly to the right spot in the virtual FS
	return ptr;

} //getInode()
