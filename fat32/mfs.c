#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

// enable/disable debug output
bool DBG = false;

// We want to split our command line up into tokens
// so we need to define what delimits our tokens.
// In this case  white space will separate the tokens on our command line
#define WHITESPACE " \t\n"

// The maximum command-line size
#define MAX_COMMAND_SIZE 255

// mfs has commands that accept 3 arguments at most
#define MAX_NUM_ARGUMENTS 3        

// declare the BPB struct, and tell GCC it's packed so we can read the entire BPB structure
// in one fell swoop
struct imageBPB
{
	uint8_t BS_jmpBoot[3];		// offset byte 0, size 3 (bytes)
	char BS_OEMName[8]; 			// offset byte 3, size 8
	uint16_t BPB_BytesPerSec;	// offset byte 11, size 2
	uint8_t BPB_SecPerClus;		// offset byte 13, size 1
	uint16_t BPB_RsvdSecCnt;	// offset byte 14, size 2
	uint8_t BPB_NumFATs;			// offset byte 16, size 1
	uint16_t BPB_RootEndCnt;	// offset byte 17, size 2
	uint16_t BPB_TotSec16;		// offset byte 19, size 2
	uint8_t BPB_Media;				// offset byte 21, size 1
	uint16_t BPB_FATSz16;			// offset byte 22, size 2
	uint16_t BPB_SecPerTrk;		// offset byte 24, size 2
	uint16_t BPB_NumHeads;		// offset byte 26, size 2
	uint32_t BPB_HiddSec;			// offset byte 28, size 4
	uint32_t BPB_TotSec32;		// offset byte 32, size 4
	uint32_t BPB_FATSz32;			// offset byte 36, size 4
	uint16_t BPB_ExtFlags;		// offset byte 40, size 2
	uint8_t BPB_FSVer[2];			// offset byte 42, size 2
	uint32_t BPB_RootClus;		// offset byte 44, size 4
	uint16_t BPB_FSInfo;			// offset byte 48, size 2
	uint16_t BPB_BkBootSec;		// offset byte 50, size 2
	uint8_t BPB_Reserved[12];	// offset byte 52, size 12
	uint8_t BS_DrvNum;				// offset byte 64, size 1
	uint8_t BS_Reserved1;			// offset byte 65, size 1
	uint8_t BS_BootSig;				// offset byte 66, size 1
	uint32_t BS_VolID;				// offset byte 67, size 4
	char BS_VolLabel[11];			// offset byte 71, size 11
	char BS_FileSysType[8];		// offset byte 82, size 8
} __attribute__((__packed__));

// declare the global BPB struct
struct imageBPB bpb;

// declare the global file pointer and ensure it's instantiated to the null ptr
FILE * fp = NULL;

// function declarations
bool readImageMetadata( void );
uint LBAToOffset( unsigned long );
short nextLB( unsigned long );
bool validateOpenCmd( char * );
void tryOpenImage( char * );
void printImageInfo( void );
bool imgAlreadyOpened( void );
void tryCloseImage( void );
void printVolumeName( void );
void cleanUp( void );

int main( int argc, char *argv[] )
{
	// check for any configured cmdline options
	char c;
  while((c = getopt(argc,argv,"d"))!=-1) {
    switch(c) {
      case 'd':
        DBG = true;
        break;
			default:
				break;
    }
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

		int token_count = 0;                                 
		
		// Pointer to point to the token
		// parsed by strsep
		char * arg_ptr;                                         
																														
		char * working_str  = strdup( cmd_str );
		
		if(DBG)
		{
			printf("DEBUG: raw command entered: %s\n", rawCmd);
		}                

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
			int token_index  = 0;
			for( token_index = 0; token_index < token_count; token_index ++ ) 
			{
				printf("DEBUG: ");
				printf("token[%d] = %s\n", token_index, tokens[token_index] );  
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
		// before exiting, ensure any open image is closed via cleanUp()
		if( strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) 
		{
			cleanUp();
			break;
		}

		// check the entered mfs command against known commands, and call the appropriate function

		if( strcmp(command, "open") == 0)
		{
			if( validateOpenCmd( tokens[1] ) )
			{
				// if the simple checks pass, try to open the requested image
				tryOpenImage( tokens[1] );
			}
			else
			{
				continue;
			}

			continue;
		}

		if( strcmp(command, "info") == 0)
		{
			printImageInfo();
			continue;
		}

		if( strcmp(command, "close") == 0)
		{
			tryCloseImage();
			continue;
		}

		if( strcmp(command, "stat") == 0)
		{

			continue;
		}

		if( strcmp(command, "get") == 0)
		{

			continue;
		}

		if( strcmp(command, "cd") == 0)
		{

			continue;
		}

		if( strcmp(command, "ls") == 0)
		{

			continue;
		}

		if( strcmp(command, "read") == 0)
		{

			continue;
		}

		if( strcmp(command, "volume") == 0)
		{
			printVolumeName();
			continue;
		}

	}// main loop

	exit(EXIT_SUCCESS);

} // main

/*
 * function: 
 *  LBAToOffeset
 * 
 * description: 
 *  Finds the starting address of a block of data given the sector number corresponding
 * 	 to that data block
 * 
 * parameters:
 *  unsigned long: the current sector number that points to a block of data
 * 
 * returns: 
 *  uint: the value of the address for that block of data
 */

bool readImageMetadata()
{
	// since we'll be reading from the file, make one final check to ensure the pointer is not NULL
	if( fp == NULL )
	{
		printf("There was an error. Please try again.\n");
		if(DBG)
		{
			printf("ERROR -> readImageMetadata(): at start of function, the fp file pointer is NULL\n");
		}
		return false;
	}

	// make sure we're at byte zero of the image file
	fseek( fp, 0, SEEK_SET) ;
	
	// clear the file stream error indicator to prep for the fread() call
	clearerr(fp);

	// read all 90 bytes of the BPB into the struct
	fread( &bpb, 1, 90, fp );

	// check if fread() had an issue, return false if so
	if( ferror(fp) )
	{
		if(DBG)
		{
			printf("ERROR -> readImageMetadata(): error from fread()\n");
		}
		return false;
	}

	// if we got here, then all is good
	return true;
}

bool validateOpenCmd( char * requestedFilename )
{
	// check if an image is already opened, warn and bail if so
	if( imgAlreadyOpened() )
	{
		printf("Error: File system image already open.\n");
		return false;
	}

	// warn and bail if the user didn't specify anything to open
	if( requestedFilename == NULL )
	{
		printf("Error: Please enter a filename to open. Ex: 'open fat32.img'.\n");
		return false;
	}

	// basic checks pass, we're good
	return true;
}

void tryOpenImage ( char * imageToOpen )
{
	// reset errno for fopen call
	errno = 0;

	// attempt to open the file
	fp = fopen(imageToOpen, "r");

	// if fp is still NULL, something went wrong
	if( fp == NULL )
	{
		if( errno == 2 )
		{
			printf("Error: File system image not found.\n");
		}
		else
		{
			printf("There was an error opening the '%s' FAT32 image file. Please try again.\n", imageToOpen);
			if(DBG)
			{
				printf("ERROR -> main(): fopen failed with error: %u: %s\n", errno, strerror(errno));
			}
		}
		return;
	}

	// attempt to read the BPB and other metadata from the image
	if( !readImageMetadata() )
	{
		printf("There was a problem reading the opened FAT32 image file. Please try again.\n");
		fclose(fp);
		fp = NULL;
		return;
	}

	return;
}

void tryCloseImage()
{
	// check if an image has been opened, warn and bail if so
	if( !imgAlreadyOpened() )
	{
		printf("Error: File system not open.\n");
		return;
	}
	// close the file
	if( fclose(fp) != 0 && DBG)
	{
		printf("ERROR-> tryCloseImage(): error during fclose..\n");
	}
	
	// ensure the file ptr gets reset to NULL
	fp = NULL;
}

void printImageInfo()
{
	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// output all the required values in both base-10 (0n) and hexadecimal (0x)
	// fortunately, no checks for string terminators or sizes are needed with these fields, as
	// the printf specifiers and modifiers are able to pick exactly the bytes that are needed
	printf("BPB_BytesPerSec: 0n%hu, 0x%hX\n", bpb.BPB_BytesPerSec, bpb.BPB_BytesPerSec);
	printf("BPB_SecPerClus: 0n%hhu, 0x%hhX\n", bpb.BPB_SecPerClus, bpb.BPB_SecPerClus);
	printf("BPB_RsvcSecCnt: 0n%hu, 0x%hX\n", bpb.BPB_RsvdSecCnt, bpb.BPB_RsvdSecCnt);
	printf("BPB_NumFATS: 0n%hhu, 0x%hhX\n", bpb.BPB_NumFATs, bpb.BPB_NumFATs);
	printf("BPB_FATSz32: 0n%u, 0x%X\n", bpb.BPB_FATSz32, bpb.BPB_FATSz32);

	return;
}

bool imgAlreadyOpened()
{
	if( fp == NULL )
	{
		return false;
	}
	
	return true;
}

void printVolumeName()
{
	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// per the spec, the FAT32 volume label is 11 bytes wide, so create a block
	// of memory 11+1 to include the null terminator
	char volLabel[12]; 

	// copy the string of 11 chars from the structure
	strncpy( volLabel, bpb.BS_VolLabel, 11 );

	// add the null terminator so printf is happy
	volLabel[11] = '\0';

	// per the spec, the string "NO NAME    " is present in the BS_VolLab field when there is no 
	// label, so output the appropriate string
	if( strcmp(volLabel, "NO NAME    ") == 0 )
	{
		printf("Error: volume name not found.\n");
	}
	else
	{
		printf("Volume name: '%s'\n", volLabel);
	}

}

/*
 * function: 
 *  LBAToOffset
 * 
 * description: 
 *  Finds the starting address of a block of data given the sector number corresponding
 * 	 to that data block
 * 
 * parameters:
 *  unsigned long: the current sector number that points to a block of data
 * 
 * returns: 
 *  uint: the value of the address for that block of data
 */
uint LBAToOffset( unsigned long sector )
{
	return ( (sector - 2) * bpb.BPB_BytesPerSec ) + ( bpb.BPB_BytesPerSec * bpb.BPB_RsvdSecCnt ) + ( bpb.BPB_NumFATs * bpb.BPB_FATSz32 * bpb.BPB_BytesPerSec );
}

/*
 * function: 
 *  nextLB
 * 
 * description: 
 *  Given a logical block address, look up into the first FAT and return the logical block address of 
 *   the block in the file. If there are no further blocks, return -1.
 * 
 * parameters:
 *  unsigned long sector: the block address which to calculate the next address for
 * 
 * returns: 
 *  short: return the next logical block, or -1 if the end
 */
short nextLB( unsigned long sector )
{
	unsigned long FATAddress = ( bpb.BPB_BytesPerSec * bpb.BPB_RsvdSecCnt ) + ( sector*4 );
	short val;
	fseek( fp, FATAddress, SEEK_SET );
	fread( &val, 2, 1, fp );
	return val;
}

void cleanUp()
{
	if( imgAlreadyOpened() )
	{
		tryCloseImage();
	}
	return;
}
