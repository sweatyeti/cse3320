#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// directory entries are 32 bytes wide
#define DIR_ENTRY_SIZE 32

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

// declare the directory struct and ensure it's also packed
struct DirectoryEntry
{
	char DIR_name[11];
	uint8_t DIR_attr;
	uint8_t unused1[8];
	uint16_t DIR_firstClusterHigh;
	uint8_t unused[4];
	uint16_t DIR_firstClusterLow;
	uint32_t DIR_fileSize;
} __attribute__((__packed__));

// declare the global directory array
struct DirectoryEntry dir[256];

// declare & init a global short to keep track of the # of dir entries currently read
uint16_t numDirEntries = 0;

// declare & init a global bool to keep track of whether or not the current dir's entries have been read
bool currDirEntriesRead = false;

// declare the global BPB struct
struct imageBPB bpb;

// the prompt will display the current folder location, so have a global for it
char * currentDir = NULL;

// global to keep track of the current working directory via sector num
uint64_t currentSector = 0;

// declare the global file pointer and ensure it's instantiated to the null ptr
FILE * fp = NULL;

// function declarations
bool readImageMetadata( void );
int32_t LBAToOffset( uint64_t );
int16_t nextLB( uint64_t );
bool validateOpenCmd( char * );
void tryOpenImage( char * );
void printImageInfo( void );
bool imgAlreadyOpened( void );
void tryCloseImage( void );
void printVolumeName( void );
void cleanUp( void );
char * getCurrentDir( void );
void setCurrentDir( char * );
void handleLS( void );
bool readCurrDirEntries( void );
void handleStat( char * );
bool generateShortName( char *, char *, bool *, bool *, bool *);
void handleCd( char * );
void handleRead( char *, char *, char * );
bool findDirEntry( char *, int * );
//uint32_t getFullCluster( struct DirectoryEntry );
void resetToRoot( void );
void addSubDirToPrompt( char * );
void removeSubDirFromPrompt( void );
void handleGet( char * );
bool tryMultistepDirChg( bool, char * );
bool tryMoveOneDir( char * );
bool tryCopyFileFromImageToCwd( int, char * );

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
		// Print out the mfs prompt depending on the context
		if( currentDir == NULL )
		{
			printf ("mfs> ");
		}
		else if( strcmp(currentDir,"root") == 0 )
		{
			printf("mfs:\\> ");
		}
		else
		{
			printf("mfs:\\%s\\>", currentDir);
		}

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
			cleanUp();
			break;
		}

		// check the entered mfs command against known commands, and call the appropriate function
		// change this to switch/case? would break for the switch interfere with the loop?

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
			handleStat(tokens[1]);
			continue;
		}

		if( strcmp(command, "get") == 0)
		{
			handleGet(tokens[1]);
			continue;
		}

		if( strcmp(command, "cd") == 0)
		{
			handleCd(tokens[1]);
			continue;
		}

		if( strcmp(command, "ls") == 0)
		{
			handleLS();
			continue;
		}

		if( strcmp(command, "read") == 0)
		{
			handleRead(tokens[1], tokens[2], tokens[3]);
			continue;
		}

		if( strcmp(command, "volume") == 0)
		{
			printVolumeName();
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
	if(DBG)
	{
		printf("DEBUG: readImageMetadata() starting...\n");
	}
	// since we'll be reading from the file, make one final check to ensure the pointer is not NULL
	if( fp == NULL )
	{
		printf("There was an error. Please try again.\n");
		if(DBG)
		{
			printf("ERROR -> at start of function, the fp file pointer is NULL\n");
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
			printf("ERROR -> error from fread()\n");
		}
		return false;
	}
	// if we got here, then all is good
	return true;
} // readImageMetadata()

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
} // validateOpenCmd()

void tryOpenImage ( char * imageToOpen )
{
	if(DBG)
	{
		printf("DEBUG: tryOpenImage() starting...\n");
	}
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
				printf("ERROR -> fopen failed with error: %u: %s\n", errno, strerror(errno));
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

	// since the image was just opened, set the currentDir global to the root dir, along with the root sec #
	resetToRoot();

	// populate the global directory entry array with the contents of the root dir
	if( !readCurrDirEntries() && DBG )
	{
		printf("ERROR -> readCurrDirEntries() had a problem...\n");
		return;
	}

	return;
} // tryOpenImage()

void tryCloseImage()
{
	// check if an image has been opened, warn and bail if so
	if( !imgAlreadyOpened() )
	{
		printf("Error: File system not open.\n");
		return;
	}

	if(DBG)
	{
		printf("DEBUG: tryCloseImage(): closing the image...\n");
	}

	// close the file
	if( fclose(fp) != 0 && DBG)
	{
		printf("ERROR-> tryCloseImage(): error during fclose..\n");
	}
	
	// ensure the file ptr gets reset to NULL
	fp = NULL;

	// ensure the currentDir global gets reset to NULL
	currentDir = NULL;

	if(DBG)
	{
		printf("DEBUG: tryCloseImage(): image closed\n");
	}
	
	return;
}

void printImageInfo()
{
	if(DBG)
	{
		printf("DEBUG: printImageInfo() starting...\n");
	}
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

	if(DBG)
	{
		printf("    -: BPB_RootClus: 0n%u, 0x%X\n", bpb.BPB_RootClus, bpb.BPB_RootClus);
		uint32_t rootAddr = LBAToOffset(bpb.BPB_RootClus);
		printf("    -: root dir address = 0x%X\n", rootAddr);
	}

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
	// of memory of 11+1 bytes to include the null terminator
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

	return;
}

bool readCurrDirEntries()
{
	if(DBG)
	{
		printf("DEBUG: readCurrDirEntries() starting...\n");
		printf("    -: current sector: %lu\n", currentSector);
		printf("    -: sector starting addr: 0x%X\n", LBAToOffset(currentSector));
	}

	// back up the currentSector global, since it shouldn't technically change on 'ls' cmd
	uint64_t currentSectorBackup = currentSector;

	// clear the file error indicator
	clearerr(fp);

	// navigate to teh appropriate image location offset, and check if successful
	if( fseek(fp, LBAToOffset(currentSector), SEEK_SET) != 0 )
	{
		printf("There was a problem performing this operation. Please try again.\n");
		if(DBG)
		{
			if( ferror(fp) )
			{
				printf("ERROR -> fseek() failed at above address.. ");
			}
			else if ( feof(fp) )
			{
				printf("ERROR -> fseek() reached EOF from above address.. ");
			}
		}
		return false;
	}

	// clear the file error indicator
	clearerr(fp);

	// initialize a counter to keep track of how many entries are present in the current dir
	uint16_t numDirEntriesRead = 0;

	// loop that reads all teh directory entries
	while(true)
	{
		if(DBG)
		{
			printf("    -: reading directory entry index #%d: ", numDirEntriesRead);
		}

		// read one entry and increment the counter
		// there will always be at least one entry since even an empty directory must have the '.' entry
		fread( &dir[numDirEntriesRead], DIR_ENTRY_SIZE, 1, fp );

		if(DBG)
		{
			char rawLabel[12];
			strncpy( rawLabel, dir[numDirEntriesRead].DIR_name, 11 );
			rawLabel[11] = '\0';
			printf("raw label: %s, ", rawLabel);
			printf("1st label byte: 0x%hhX, ", rawLabel[0]);
			printf("attr: 0x%hhX\n", dir[numDirEntriesRead].DIR_attr);
		}

		numDirEntriesRead++;

		// check if we've reached the end of the sector
		//if( numDirEntriesRead * DIR_ENTRY_SIZE == bpb.BPB_BytesPerSec )
		if( (( numDirEntriesRead*DIR_ENTRY_SIZE ) % bpb.BPB_BytesPerSec) == 0 )
		{
			// end of sector reached, check if there's a next one
			int16_t nextSector = nextLB(currentSector);

			if(DBG)
			{
				printf("    -: end of sector reached...\n");
			}

			if(nextSector != -1 )
			{
				// there's more data, update the global sector var
				currentSector = nextSector;

				int32_t nextSectorAddr = LBAToOffset(nextSector);

				if(DBG)
				{
					printf("    -: next sec: %hd, next sec addr: %X, going there..\n", nextSector, nextSectorAddr);
				}

				// navigate to teh appropriate image location offset, and check if successful
				if( fseek(fp, nextSectorAddr, SEEK_SET) != 0 )
				{
					printf("There was a problem performing this operation. Please try again.\n");
					if(DBG)
					{
						if( ferror(fp) )
						{
							printf("ERROR -> fseek() failed getting to the next sector address.. ");
						}
						else if ( feof(fp) )
						{
							printf("ERROR -> fseek() reached EOF from next sector address.. ");
						}
					}
					// restore the currentSector global
					currentSector = currentSectorBackup;
					return false;
				}

				// we were able to fseek successfully, so continue the loop
				continue;
			}
			else
			{
				// no more blocks, break out of the loop
				if(DBG)
				{
					printf("    -: no more sectors to read...\n");
				}
				break;
			}
		}
		else
		{
			// store the current position of the stream so it can be restored
			fpos_t streamPosition;
			fgetpos(fp, &streamPosition);

			// read one byte into testDir, which could be the start of a new directory entry
			uint8_t testDir = 0;
			fread( &testDir, 1, 1, fp );

			// restore the file stream position from before the fread
			fsetpos(fp, &streamPosition);

			// if testDir is still zero (0), then break out of the loop, otherwise continue and read another entry
			if( testDir == 0 )
			{
				if(DBG)
				{
					printf("     : no more entries, exiting loop..\n");
				}
				break;
			}
			else
			{
				continue;
			}
		}
	}

	// check for any file errors
	if( ferror(fp) )
	{
		printf("There was a problem reading the image. Please try again.\n");
		// restore the currentSector global
		currentSector = currentSectorBackup;
		return false;
	}

	// populate the global with the number of entries read
	numDirEntries = numDirEntriesRead;

	// restore the currentSector global
	currentSector = currentSectorBackup;

	if(DBG)
	{
		printf("    -: %hu entries read\n", numDirEntriesRead);
		printf("DEBUG: readCurrDirEntries() ending...\n");
	}

	// set the global to let the program know the entries have been read
	currDirEntriesRead = true;

	return true;
} // readCurrDirEntries()

void handleRead( char * fileToBeRead, char * filePosStr, char * numBytesStr)
{
	if(DBG)
	{
		printf("DEBUG: handleRead() starting...\n");
	}

	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// check if any of the req'd params are NULL, warn and bail if so
	if(fileToBeRead == NULL || filePosStr == NULL || numBytesStr == NULL)
	{
		printf("Please enter a valid read command, such as 'read foo.txt 0 20'\n");
		if(DBG)
		{
			printf("    -: a param is NULL\n");
		}
		return;
	}

	int64_t filePos;

	// check the position to start with
	// if nonzero, convert the chars to int64_t
	if(strcmp(filePosStr,"0") == 0)
	{
		filePos = 0;
	}
	else
	{
		// ensure the param can be converted without issue
		filePos = strtol(filePosStr, NULL, 10);
		if( filePos == 0 || filePos == LONG_MAX || filePos == LONG_MIN )
		{
			printf("Please enter a valid read command, such as 'read foo.txt 0 20'\n");
			if(DBG)
			{
				printf("    -: filePos couldn't be converted\n");
			}
			return;
		}
	}

	int64_t numBytes;

	// check how many bytes to read
	// if nonzero, convert the chars to int64_t
	if(strcmp(numBytesStr, "0") == 0)
	{
		numBytes = 0;
	}
	else
	{
		// ensure the param can be converted without issue
		numBytes = strtol(numBytesStr, NULL, 10);
		if( numBytes == 0 || numBytes == LONG_MAX || numBytes == LONG_MIN )
		{
			printf("Please enter a valid read command, such as 'read foo.txt 0 20'\n");
			if(DBG)
			{
				printf("    -: numBytes couldn't be converted\n");
			}
			return;
		}
	}	

	// first we need to convert the entered entry name into the short name stored in the image
	char enteredShortName[12];
	
	// generateShortName returns true if it was able to generate a legit short name
	// if it failed, then warn and bail
	bool isDirectory = false;
	bool isDot = false;
	bool isDotDot = false;
	if(!generateShortName(fileToBeRead, enteredShortName, &isDirectory, &isDot, & isDotDot))
	{
		printf("Error: File not found\n");
		return;
	}
	enteredShortName[11] = '\0';

	// can't read from a directory, warn and bail if that's what the user tried
	if(isDirectory)
	{
		printf("Please enter a valid read command, such as 'read foo.txt 0 20'\n");
		if(DBG)
		{
			printf("    -: can't read from a directory\n");
		}
		return;
	}

	// ensure the current dir entries have been read
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return;
		}
	}

	int index = 0;
	if(findDirEntry(enteredShortName, &index))
	{
		// make doubly sure we're not trying to read from a directory
		if((dir[index].DIR_attr | 0x10) == 0x10)
		{
			printf("Please enter a valid read command, such as 'read foo.txt 0 20'\n");
			if(DBG)
			{
				printf("    -: can't read from a directory\n");
			}
			return;
		}

		// ensure the desired position is within the file
		if(filePos > dir[index].DIR_fileSize)
		{
			printf("Please enter a valid position within the requested file.\n");
			return;
		}

		// backup the current sector so it can be restored after the read
		uint64_t currentSectorBackup = currentSector;

		currentSector = dir[index].DIR_firstClusterLow;

		// figure out if the desired position is in another sector
		uint64_t filePosSector = filePos / bpb.BPB_BytesPerSec;

		// adjust the current sector if needed
		int i;
		for( i=0; i<filePosSector; i++)
		{
			if(DBG)
			{
				printf("    -: adjusting sector from 0x%lX to ", currentSector);
			}
			currentSector = nextLB(currentSector);
			if(DBG)
			{
				printf("0x%lX\n", currentSector);
			}
		}

		// adjust the position indicator based on the sector
		filePos -= (bpb.BPB_BytesPerSec * filePosSector);

		if(DBG)
		{
			printf("     : new filePos: 0x%lX (0n%ld)\n", filePos, filePos);
			printf("     : seeking to and reading the file...\n");
		}

		// goto the location in the file
		fseek(fp, LBAToOffset(currentSector)+filePos, SEEK_SET);

		// create the char array that will hold all the output, and init every element to \0
		char chars[numBytes+1];
		int ctr;
		for( ctr=0; ctr<=numBytes; ctr++)
		{
			chars[ctr] = '\0';
		}

		// do the reading
		//int byteCount;
		//for( byteCount=0; byteCount<numBytes; byteCount++)
		//{
			fread( chars, 1, numBytes, fp );
		//}
		if(DBG)
		{
			printf("     : file read finished\n");
		}

		printf("%s\n", chars);

		currentSector = currentSectorBackup;

	}
	else
	{
		printf("Error: File not found\n");
	}

	if(DBG)
	{
		printf("DEBUG: handleRead() ending...\n");
	}

} // handleRead()

void handleLS()
{
	if(DBG)
	{
		printf("DEBUG: handleLS() starting...\n");
	}
	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// ensure the current dir entries have been read
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return;
		}
	}

	// loop through each directory entry and display the necessary info
	uint16_t i;
	for( i=0; i<numDirEntries; i++)
	{
		uint8_t attr = dir[i].DIR_attr;

		// per the assignment, the only entries to show by attribute are:
		// 0x01: read-only file
		// 0x10: subdirectory
		// 0x20: archive flag
		// so create a bitmask of those values to compare against
		uint8_t attrBitmaskShow = 0x01 | 0x10 | 0x20;
		//also create a bitmask of the other values that don't get shown
		uint8_t attrBitmaskDontShow = 0x02 | 0x04 | 0x08;

		// do the attribute bitwise comparison
		// if any of the desired bits match, then the bitwise AND value will be >=1
		// if none of the undesired bits match, then the bitwise AND value will be 0
		// the attribute for long-name entries is 0x0F - don't deal with these
		if( ((attr & attrBitmaskShow) >= 1) && ((attr & attrBitmaskDontShow) == 0) && attr != 0x0F )
		{
			// for the attributes that match, do the check on the first filename char
			uint8_t firstCharOfName = dir[i].DIR_name[0];
			if( firstCharOfName != 0x00 && firstCharOfName != 0xE5)
			{
				// if we get here, then an entry's label will be displayed

				// store a null-term'd copy of the full label
				char rawLabel[12];
				strncpy( rawLabel, dir[i].DIR_name, 11 );
				rawLabel[11] = '\0';

				// convert every char to lowercase
				int k;
				for( k=0; k<strlen(rawLabel); k++)
				{
					rawLabel[k] = tolower(rawLabel[k]);
				}

				// check if we're dealing with the . and .. entries
				if( i == 0 && firstCharOfName == '.' && dir[i].DIR_name[1] == 0x20 )
				{
					// this is the dot entry
					printf(".\n");
				}
				else if( i== 1 && firstCharOfName == '.' && dir[i].DIR_name[1] == '.' && dir[i].DIR_name[2] == 0x20 )
				{
					// this is the dotdot entry
					printf("..\n");
				}
				// check if the entry is a subdirectory
				else if( attr == 0x10)
				{
					int j;
					for( j=10; j>=0; j--)
					{
						if(rawLabel[j] == 0x20)
						{
							rawLabel[j] = '\0';
						}
					}
					printf("%s\n", rawLabel);
				}
				else
				{
					// isolate the extension
					char extension[4];
					strncpy( extension, &rawLabel[8], 3 );
					extension[3] = '\0';
					if(extension[2] == 0x20)
					{
						extension[2] = '\0';
					}
					if(extension[1] == 0x20)
					{
						extension[1] = '\0';
					}

					// isolate the file name
					char fileName[9];
					strncpy (fileName, rawLabel, 8);
					fileName[8] = '\0';
					int j;
					for( j=7; j>=0; j-- )
					{
						if(fileName[j] == 0x20)
						{
							fileName[j] = '\0';
						}
					}

					printf("%s.%s\n", fileName, extension);
				}// if..else

			}// if..else

		}// if..else
		
	}// for

	if(DBG)
	{
		printf("DEBUG: handleLS() ending...\n");
	}
	return;
} // handleLS()

void handleCd( char * enteredDirName )
{
	if(DBG)
	{
		printf("DEBUG: handleCd() starting...\n");
	}
	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// ensure the user entered a dir name
	if( enteredDirName == NULL )
	{
		printf("Please enter a directory name.\n");
		return;
	}

	// ensure the directories have been read before traversing through them
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return;
		}
	}

	// create a flag to indicate if the directory has been changed
	bool cdSuccessful = false;

	int enteredDirNameLength = strlen(enteredDirName);

	// check if the user wants to move to an absolute or relative location
	if( enteredDirName[0] == '/' || enteredDirName[0] == '\\')
	{
		if(enteredDirNameLength == 1)
		{
			// user is asking to move to root, do so
			if(DBG)
			{
				printf("    -: setting cwd to root...\n");
			}
			resetToRoot();
			cdSuccessful = true;
		}
		else
		{
			//handle other root-relative (absolute) moves
			cdSuccessful = tryMultistepDirChg(true, enteredDirName);

		}
	}
	else if( (strchr(enteredDirName,'\\') != NULL) || (strchr(enteredDirName,'/') != NULL) )
	{
		// user wants to move to a relative location that would take more than one step (ie cd ../name)
		cdSuccessful = tryMultistepDirChg(false, enteredDirName);
	}
	else
	{
		// user wants to move to a relative location in one step (ie cd . or cd foldera)
		cdSuccessful = tryMoveOneDir(enteredDirName);
	}

	if(DBG)
	{
		printf("DEBUG: handleCd() ending...\n");
	}

	// if we changed directories, then update the global flag to indicate to the next function that
	// the current directory for them has not been read yet
	if(cdSuccessful)
	{
		currDirEntriesRead = false;
	}
	
	return;
} // handleCd()

void handleStat( char * enteredEntryName )
{
	if(DBG)
	{
		printf("DEBUG: handleStat() starting...\n");
	}
	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// ensure the user entered an entry name
	if( enteredEntryName == NULL )
	{
		printf("Please enter a file or directory name.\n");
		return;
	}

	// first we need to convert the entered entry name into the short name stored in the image
	char enteredShortName[12];
	
	// generateShortName returns true if it was able to generate a legit short name
	// if it failed, then warn and bail
	bool isDirectory = false;
	bool isDot = false;
	bool isDotDot = false;
	if(!generateShortName(enteredEntryName, enteredShortName, &isDirectory, &isDot, & isDotDot))
	{
		printf("Error: File not found\n");
		return;
	}
	enteredShortName[11] = '\0';

	// ensure the directories have been read before traversing through them
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return;
		}
	}

	int index = 0;
	if(findDirEntry(enteredShortName, &index))
	{
		// print friendly name and short name
		printf("Entered value: %s\n", enteredEntryName);
		printf("Directory entry raw label: %s\n", enteredShortName);

		// print attributes
		printf("Directory entry attributes:\n");

		if( (dir[index].DIR_attr & 0x01) == 0x01 )
		{
			printf(" - 0x01: ATTR_READ_ONLY\n");
		}
		if( (dir[index].DIR_attr & 0x02) == 0x02 )
		{
			printf(" - 0x02: ATTR_HIDDEN\n");
		}
		if( (dir[index].DIR_attr & 0x04) == 0x04 )
		{
			printf(" - 0x04: ATTR_SYSTEM\n");
		}
		if( (dir[index].DIR_attr & 0x08) == 0x08 )
		{
			printf(" - 0x08: ATTR_VOLUME_ID\n");
		}
		if( (dir[index].DIR_attr & 0x10) == 0x010 )
		{
			printf(" - 0x10: ATTR_DIRECTORY\n");
		}
		if( (dir[index].DIR_attr & 0x20) == 0x20 )
		{
			printf(" - 0x20: ATTR_ARCHIVE\n");
		}

		uint8_t attrLongName = 0x01|0x02|0x04|0x08;
		if( (dir[index].DIR_attr & attrLongName) == attrLongName )
		{
			printf(" - 0x%hhX: ATTR_LONG_NAME\n");
		}

		// print starting cluster
		printf("Starting cluster: %hX\n", dir[index].DIR_firstClusterLow);

		// print size
		if(isDirectory)
		{
			printf("File size: 0 bytes\n");
		}
		else
		{
			printf("File size: %d (0x%X) bytes\n", dir[index].DIR_fileSize, dir[index].DIR_fileSize);
		}
	}
	else
	{
		printf("Error: File not found\n");
	}

	return;

} // handleStat()

bool tryMoveOneDir( char * enteredDirName )
{
	if(DBG)
	{
		printf("DEBUG: tryMoveOneDir() starting...\n");
	}

	bool dirChanged = false;

	char genShortName[12];
	genShortName[11] = '\0';

	// generateShortName returns true if it was able to generate a legit short name
	// if it failed, then warn and bail
	bool isDirectory = false;
	bool isDot = false;
	bool isDotDot = false;
	if(!generateShortName(enteredDirName, genShortName, &isDirectory, &isDot, &isDotDot))
	{
		if(DBG)
		{
			printf("    -: generateShortName() returned false\n");
		}
		printf("Error: Please enter a valid directory name.\n");
		return false;
	}
	// if a short name could be generated, check if the entered entry is a directory name, 
	// warn and bail if not
	else if(!isDirectory)
	{
		if(DBG)
		{
			printf("    -: generateShortName() indicated the chosen entry is not a directory\n");
		}
		printf("Error: Please enter a valid directory name.\n");
		return false;
	}

	// check to ensure the user hasn't selected . or .. if in the root dir, warn and bail if so
	if(currentSector == bpb.BPB_RootClus && ( isDot || isDotDot ) )
	{
		if(DBG)
		{
			printf("    -: cannot do dot or dotdot in root\n");
		}
		printf("Error: Please enter a valid directory name.\n");
		return false;
	}

	// ensure the directories have been read before traversing through them
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return false;
		}
	}

	// check for . and .. entries first, since they will be the first 2 records of a non-root directory
	if(isDot)
	{
		// dot is the first entry of a non-root directory
		if(DBG)
		{
			printf("    -: handling dot..\n");
		}

		// doing this is not optimal, but is needed for the sake of time to avoid adding way more work in other areas
		dirChanged = true;

	}
	else if(isDotDot)
	{
		// dotdot is the second entry of a non-root directory
		if(DBG)
		{
			printf("    -: handling dotdot..\n");
		}

		// var for the parent directory cluster
		uint16_t parentDirCluster = dir[1].DIR_firstClusterLow;

		// check if the parent dir is root first
		if(parentDirCluster == 0)
		{
			if(DBG)
			{
				printf("    -: dotdot leads to root, going there...\n");
			}
			resetToRoot();
			dirChanged = true;
		}
		else
		{
			// at this point, we're not in a subdirectory of the root, so move upwards one level
			if(DBG)
			{
				printf("    -: dotdot does not lead to root, calculating the parent dir..\n");
			}

			// update the global tracker
			currentSector = parentDirCluster;

			// since we're moving directories, update the breadcrumb prompt
			removeSubDirFromPrompt();
			dirChanged = true;
		}

	}
	else
	{
		// moving to a subdirectory only
		if(DBG)
		{
			printf("    -: checking if subdir '%s' exists..\n", enteredDirName);
		}
		int dirIndex = 0;
		if(!findDirEntry(genShortName, &dirIndex))
		{
			printf("Error: Path not found.\n");
		}
		else
		{
			currentSector = dir[dirIndex].DIR_firstClusterLow;
			addSubDirToPrompt(enteredDirName);
			dirChanged = true;
		}

		if(DBG)
		{
			printf("    -: current sec = 0x%lX\n",currentSector);
			printf("    -: current dir = '%s'\n", currentDir);				
		}
	}

	if(DBG)
	{
		printf("DEBUG: tryMoveOneDir() ending...\n");
	}

	return dirChanged;
}

bool tryMultistepDirChg( bool relativeToRoot, char * requestedDir )
{
	if(DBG)
	{
		printf("DEBUG: tryMultistepDirChg() starting...\n");
	}

	// backup current sector and dir in case any of the steps fail
	char * currentDirBackup;
	int currentSectorBackup = currentSector;
	if(currentSector != bpb.BPB_RootClus)
	{
		currentDirBackup = (char*) calloc( strlen(currentDir)+1, sizeof(char));
		strcpy(currentDirBackup, currentDir);
	}

	bool dirChanged = false;
	bool allStepsSuccessful = true;

	// the tokenizing logic further down strips-out any leading slashes
	// since that is lost, the relativeToRoot param is set by the caller, which has the logic to determine this
	if(relativeToRoot)
	{
		resetToRoot();
		currDirEntriesRead = false;
	}
	
	// split the string into tokens, checking if the supplied dir entry is valid along the way
	char * entryToken;
	entryToken = strtok(requestedDir,"/\\");
	
	if(DBG)
	{
		printf("     : tokenizing...\n");
	}
	while( entryToken != NULL )
	{
		if(DBG)
		{
			printf("    -: testing token '%s' \n", entryToken);
		}

		// check if we could move one directory in the appropriate context
		if( tryMoveOneDir(entryToken) )
		{
			// the directory exists and we could move into it
			if(DBG)
			{
				printf("    -: single move successful\n");
			}
			// since we moved currentDir somewhere else, update the global 
			currDirEntriesRead = false;

			// update the flag since we changed dirs at least once
			dirChanged = true;

			// grab the next token and continue
			entryToken = strtok(NULL,"/\\");
			continue;
		}
		else
		{
			// the directory move could not be performed
			if(DBG)
			{
				printf("    -: single move failed\n");
			}
			// since at least one step failed, set the flag and break out
			allStepsSuccessful = false;
			break;
		}		
	}

	if(DBG && allStepsSuccessful)
	{
		printf("    -: all steps successful\n");
	}

	// if we changed directories at least once, but overall the move failed, then restore the original location context
	if(dirChanged && !allStepsSuccessful)
	{
		if(DBG)
		{
			printf("    -: restoring original sector and dir...\n");
		}

		if(currentSectorBackup == bpb.BPB_RootClus)
		{
			resetToRoot();
		}
		else
		{
			currentSector = currentSectorBackup;
			currentDir = NULL;
			currentDir = (char*) calloc( strlen(currentDirBackup)+1, sizeof(char) );
			strcpy(currentDir, currentDirBackup);
		}
		
		currDirEntriesRead = false;
	}

	if(DBG)
	{
		printf("DEBUG: tryMultistepDirChg() ending...\n");
	}

	return allStepsSuccessful;
}

void handleGet( char * fileToGet )
{
	if(DBG)
	{
		printf("DEBUG: handleGet() starting...\n");
	}
	// make the de facto check to ensure an image has been opened, warn and bail if not
	if( !imgAlreadyOpened() ) 
	{
		printf("Error: File system image must be opened first.\n");
		return;
	}

	// ensure the user entered a file name
	if( fileToGet == NULL )
	{
		printf("Please enter a file name.\n");
		return;
	}

	// first we need to convert the entered entry name into the short name stored in the image
	char enteredShortName[12];
	
	// generateShortName returns true if it was able to generate a legit short name
	// if it failed, then warn and bail
	bool isDirectory = false;
	bool isDot = false;
	bool isDotDot = false;
	if(!generateShortName(fileToGet, enteredShortName, &isDirectory, &isDot, & isDotDot))
	{
		printf("Error: File not found\n");
		return;
	}
	enteredShortName[11] = '\0';

	// can't get a directory, warn and bail if that's what the user tried
	if(isDirectory)
	{
		printf("Please enter a valid get command, such as 'get foo.txt'\n");
		if(DBG)
		{
			printf("     : can't get a directory\n");
		}
		return;
	}

	// ensure the current dir entries have been read
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return;
		}
	}

	int index = 0;
	if(findDirEntry(enteredShortName, &index))
	{
		// make doubly sure we're not trying to read from a directory
		if((dir[index].DIR_attr | 0x10) == 0x10)
		{
			printf("Please enter a valid get command, such as 'get foo.txt'\n");
			if(DBG)
			{
				printf("     : can't get a directory\n");
			}
			return;
		}

		if( !tryCopyFileFromImageToCwd(index,fileToGet) )
		{
			printf("There was a problem getting the file. Please try again.\n");
			if(DBG)
			{
				printf("ERROR -> tryCopyFileFromImageToCwd() returned false\n");
			}
		}
		else
		{
			printf("File '%s' retrieved and placed in current working directory.\n", fileToGet);
		}
		
	}
	else
	{
		printf("Error: File not found.\n");
	}


	if(DBG)
	{
		printf("DEBUG: handleGet() ending...\n");
	}

} // handleGet()

bool tryCopyFileFromImageToCwd( int entryIndex, char * fileName )
{
	if(DBG)
	{
		printf("DEBUG: tryCopyFileFromImageToCwd() starting...\n");
	}

	// create a flag that gets returned so the caller can determine success
	bool fileSaveSuccessful = false;

	// grab and store the current working directory, will need to free(cwdBuf) later
	char * cwdBuf = NULL;
	cwdBuf = getcwd(NULL, 0);

	// build the entire file path, which gets sent to fopen later
	char outFilePathAndName[ strlen(cwdBuf) + strlen(fileName) +2 ];
	strcpy(outFilePathAndName,cwdBuf);
	strcat(outFilePathAndName,"/");
	strcat(outFilePathAndName,fileName);

	if(DBG)
	{
		printf("     : cwd = %s\n", cwdBuf);
		printf("     : full file path will be: %s\n", outFilePathAndName);
	}

	// backup the current sector so it can be restored after the read
	uint64_t currentSectorBackup = currentSector;

	// move the sector indicator to the first block of the file, and calculate that address
	currentSector = dir[entryIndex].DIR_firstClusterLow;
	uint32_t fileOffset = LBAToOffset(currentSector);

	// store the size of the file
	uint32_t fileSize = dir[entryIndex].DIR_fileSize;

	if(DBG)
	{
		printf("     : file starts at sector %hd, address 0x%X, going there..\n", currentSector, fileOffset);
	}

	// goto the file location
	fseek(fp, fileOffset, SEEK_SET);

	// initialize the output file stream
	FILE * outFile;
	outFile = fopen(outFilePathAndName, "w+");

	uint8_t * outBytes;

	// if the fileSize < num bytes per sector, then we don't need to do anything special wrt moving to diff clusters
	// so, read the file in one fell swoop
	if(fileSize <= bpb.BPB_BytesPerSec)
	{
		outBytes = (uint8_t*) calloc( fileSize, sizeof(uint8_t) );
		if( fread(outBytes, sizeof(uint8_t), fileSize, fp ) != fileSize )
		{
			// fread had a problem
			if(DBG)
			{
				printf("ERROR -> tryCopyFileFromImageToCwd(): single cluster fread failed\n");
			}
		}
		else if( fwrite(outBytes, sizeof(uint8_t), fileSize, outFile) != fileSize )
		{
			// fwrite had a problem
			if(DBG)
			{
				printf("ERROR -> tryCopyFileFromImageToCwd(): single cluster fwrite failed\n");
			}
		}
		else
		{
			// we only get here if both fread and fwrite were successful
			fileSaveSuccessful = true;
			if(DBG)
			{
				printf("     : single cluster read and write successful\n");
			}
		}
	}
	else
	{
		// the file will span multiple clusters, so read in chunks of BPB_BytesPerSec bytes at a time, 
		// until the last block, then just read what's left
		uint16_t amountToWrite = bpb.BPB_BytesPerSec;

		uint32_t numBytesToBeRead = fileSize;

		if(DBG)
		{
			printf("     : the file to get is %d bytes\n", fileSize);
		}

		int loopCount = 0;
		uint32_t amountWritten = 0;
		while(true)
		{
			// clear and allocate memory for the array that holds the bytes to be xferred
			outBytes = NULL;
			outBytes = (uint8_t*) calloc(amountToWrite, sizeof(uint8_t));

			if( fread( outBytes, sizeof(uint8_t), amountToWrite, fp) != amountToWrite )
			{
				// fread had a problem
				if(DBG)
				{
					printf("ERROR -> tryCopyFileFromImageToCwd(): multi-cluster fread failed in loop %d\n", loopCount);
				}
			}
			else if( fwrite( outBytes, sizeof(uint8_t), amountToWrite, outFile) != amountToWrite )
			{
				// fwrite had a problem
				if(DBG)
				{
					printf("ERROR -> tryCopyFileFromImageToCwd(): multi-cluster fwrite failed in loop %d\n", loopCount);
				}
			}
			else
			{
				// we only get here if both fread and fwrite were successful
				if(DBG)
				{
					printf("     : multi-cluster read/write for loop #%d successful\n", loopCount);
				}

				amountWritten += amountToWrite;
				numBytesToBeRead -= amountToWrite;

				if(DBG)
				{
					printf("     : num bytes written: %d\n", amountWritten);
				}

				// calculate next loop iteration details
				currentSector = nextLB(currentSector);

				// if the last block has already been read, then we're finished, break the loop
				if(currentSector == -1)
				{
					if(DBG)
					{
						printf("     : multi-cluster read/write finished, loop exiting...\n");
					}

					if(amountWritten != fileSize)
					{
						if(DBG)
						{
							printf("ERROR -> tryCopyFileFromImageToCwd(): fileSize (%d) & amountWritten (%d) don't match\n",fileSize, amountWritten);
							printf("      -> the difference (amountWritten-fileSize): %d\n", amountWritten-fileSize);
						}
					}
					else
					{
						fileSaveSuccessful = true;
					}
					break;
				}

				// get the new block's offset and go there
				fileOffset = LBAToOffset(currentSector);
				fseek(fp, fileOffset, SEEK_SET);
				
				if(DBG)
				{
					printf("     : next sector: %lX @ address 0x%X\n", currentSector, fileOffset);
				}

				// figure out if we need to read the entire block, or just part of it
				if( numBytesToBeRead <= bpb.BPB_BytesPerSec )
				{
					// if we don't need to read the whole block, then set the amountToWrite to what's left
					amountToWrite = numBytesToBeRead;
				}
				if(DBG)
				{
					printf("     : numBytesToBeRead=%d, next amountToWrite=%hd\n", numBytesToBeRead, amountToWrite);
				}
			}
			loopCount++;
		}

	}

	// close the output file
	fclose(outFile);

	// restore the original current sector
	currentSector = currentSectorBackup;
	free(cwdBuf);

	if(!fileSaveSuccessful)
	{
		// a file always gets created, so delete it if any failure occurred during the read/write process
		if( remove(outFilePathAndName) != 0 )
		{
			if(DBG)
			{
				printf("ERROR -> tryCopyFileFromImageToCwd(): failure to delete created file after failed read/write attempt\n");
			}
		}
	}

	if(DBG)
	{
		printf("DEBUG: tryCopyFileFromImageToCwd() ending...\n");
	}

	return fileSaveSuccessful;
} // tryCopyFileFromImageToCwd()

bool generateShortName( char * enteredName, char * outShortName, bool * outIsDirectory, bool * outIsDot, bool * outIsDotDot )
{
	if(DBG)
	{
		printf("DEBUG: generateShortName() starting...\n");
	}

	// create array of chars and fill it with spaces 
	//char outShortName[11];
	int k;
	for( k=0; k<11; k++)
	{
		outShortName[k] = 0x20;
	}

	int enteredNameLength = strlen(enteredName);

	// since we're only doing short name checking, the file/dir name cannot be longer than
	// 11 characters + 1 dot. If the user entered more than 12 characters, return false
	// later-on we check to ensure the name is valid or not, since some 12char labels can be wrong
	if(enteredNameLength > 12)
	{
		if(DBG)
		{
			printf("     : invalid entry name entered (too long)..\n");
		}
		return false;
	}

	if(enteredNameLength == 1 && enteredName[0] == '.')
	{
		outShortName[0] = '.';
		*outIsDirectory = true;
		*outIsDot = true;
	}
	else if( enteredNameLength == 2 && enteredName[0] == '.' && enteredName[1] == '.')
	{
		outShortName[0] = '.';
		outShortName[1] = '.';
		*outIsDirectory = true;
		*outIsDotDot = true;
	}
	else if( enteredNameLength >= 2 && enteredName[0] == '.' )
	{
		// this is an invalid name per the FAT spec (DIR_Name[0] would be 0x20), return false
		if(DBG)
		{
			printf("     : invalid entry name entered (DIR_Name[0] would be 0x20)\n");
		}
		return false;
	}
	else
	{
		// convert the entered characters to uppercase
		int i;
		for( i=0; i<enteredNameLength; i++)
		{
			enteredName[i] = toupper(enteredName[i]);
		}

		// prep for strcpsn() call
		int dotPosition;
		char * dot = ".";

		// make the call. the '.' is used to see if one was entered. This determines 
		// if the user is asking for a directory or a file
		dotPosition = strcspn( enteredName, dot );

		// if no dot was found, then they're looking for a directory
		if( dotPosition == enteredNameLength )
		{
			if(enteredNameLength > 11)
			{
				// there's no dot/extension, so the max allowed name length is 11
				if(DBG)
				{
					printf("     : invalid entry name entered (too long)\n");
				}
				return false;
			}
			// copy just the entered (and capitalized) name letters to be returned
			strncpy( outShortName, enteredName, enteredNameLength );
			*outIsDirectory = true;
		}
		else if( dotPosition > 8 )
		{
			// per the FAT spec, the entry name is max 8-char label + max 3-char extension
			// so the dot's furthest position can be index 8
			// if the dot is past this (index>8), then it's an invalid request, return false
			if(DBG)
			{
				printf("     : invalid entry name entered (filename > 8 chars)\n");
			}
			return false;
		}
		else
		{
			// if the dot was at position X, then there are X characters before it, copy those
			// chars into the out string
			strncpy( outShortName, enteredName, dotPosition );

			int enteredExtnLength = strlen( &enteredName[dotPosition+1] );

			if(enteredExtnLength > 3)
			{
				// invalid file name entered (extension can only be max 3 chars), return false
				if(DBG)
				{
					printf("     : invald entry extension entered (extn > 3 chars)\n");
				}
				return false;
			}

			// according to the FAT spec, "foo." resolves to the "FOO" short name
			// so, only copy additional characters is there is something after the dot
			if(enteredExtnLength > 0)
			{
				strncpy( &outShortName[8], &enteredName[dotPosition+1], enteredExtnLength );
			}
			else
			{
				// if there is nothing after the dot, then the entry is a directory
				*outIsDirectory = true;
			}
		}
	}

	if(DBG)
	{
		printf("     : generated short name: '");
		int j;
		for( j=0; j<11; j++)
		{
			printf("%c", outShortName[j]);
		}
		printf("'\n");
		printf("DEBUG: generateShortName() ending...\n");
	}

	return true;

} // generateShortName()

bool findDirEntry(char * shortName, int * outIndex)
{
	if(DBG)
	{
		printf("DEBUG: findDirEntry() starting...\n");
	}

	// ensure the directories have been read before traversing through them
	if(!currDirEntriesRead)
	{
		if( !readCurrDirEntries() && DBG )
		{
			printf("ERROR -> readCurrDirEntries() had a problem...\n");
			return false;
		}
	}

	// create a bool to check whether a matching entry was found
	bool matchFound = false;

	// loop through the global dir array to check for a match
	int i;
	for( i=0; i<numDirEntries; i++)
	{
		char rawLabel[12];
		strncpy( rawLabel, dir[i].DIR_name, 11 );
		rawLabel[11] = '\0';
		if( strcmp(shortName, rawLabel) == 0 )
		{
			matchFound = true;
			*outIndex = i;
			break;
		}
	}
	if(DBG)
	{
		if(matchFound)
		{
			printf("    -: match found\n");
		}
		else
		{
			printf("    -: match not found\n");
		}
		printf("DEBUG: findDirEntry() ending...\n");
	}
	return matchFound;
}

/*uint32_t getFullCluster(struct DirectoryEntry dirEntry)
{
	if(DBG)
	{
		printf("DEBUG: getFullCluster() starting...\n");
	}

	char clusterStr[4];
	uint32_t cluster;

	// get all the cluster bytes individually
	uint8_t firstByte = dirEntry.DIR_firstClusterHigh[1];
	uint8_t secondByte = dirEntry.DIR_firstClusterHigh[0];
	uint8_t thirdByte = dirEntry.DIR_firstClusterLow[1];
	uint8_t fourthByte = dirEntry.DIR_firstClusterLow[0];

	// store the 4 bytes in order from highest byte to lowest byte in a string
	sprintf(clusterStr, "%hhu%hhu%hhu%hhu", firstByte, secondByte, thirdByte, fourthByte);

	// convert the cluster string to a uint32_t
	cluster = (uint32_t) strtol(clusterStr, NULL, 10);

	if(DBG)
	{
		printf("sector: %X\n", cluster);
		printf("sector offset: %X\n",LBAToOffset(cluster));
		printf("DEBUG: getFullCluster() ending...\n");
	}

	return cluster;
}*/

void addSubDirToPrompt( char * textToAdd )
{
	if(DBG)
	{
		printf("DEBUG: addSubDirToPrompt() starting...\n");
	}

	// if we're in root, then it's simple to add a subdir to the prompt
	if(currentDir == "root")
	{
		// invalidate the last pointer
		currentDir = NULL;
		
		// allocate a new block of memory that's big enough to hold the subdir size
		currentDir = (char *) calloc( strlen(textToAdd)+1, sizeof(char) );

		// copy-in the subdir name
		strcpy(currentDir, textToAdd);
	}
	else
	{
		// adding a nested subdir is more tricky
		// we need to ensure memory is handled properly
		int currentDirLength = strlen(currentDir);

		// create a temp copy of the current directory, with a couple added bytes for trailing slash and null term
		char copy[currentDirLength+2];
		strncpy(copy, currentDir, currentDirLength);

		// add the slash that will go at the end, along with the null term
		copy[currentDirLength] = '\\';
		copy[currentDirLength+1] = '\0';

		// invalidate the last pointer, and allocate an appropriately-size new block of memory
		currentDir = NULL;
		currentDir = (char*) calloc( strlen(textToAdd)+currentDirLength+2, sizeof(char) );

		// copy the old string into the new copy, then concatenate the new directory to that
		strcpy(currentDir,copy);
		strcat(currentDir,textToAdd);
	}

	if(DBG)
	{
		printf("DEBUG: addSubDirToPrompt() ending...\n");
	}
	return;
}

void removeSubDirFromPrompt()
{
	if(DBG)
	{
		printf("DEBUG: removeSubDirFromPrompt() starting...\n");
	}

	// we don't get here if the user is going back to root, so we don't need to worry about that

	// get the snippet of the directory that will be removed
	// the result ptr will be a ptr to the slash before the start of the current dir
	char * lastSlashPtr;
	lastSlashPtr = strrchr(currentDir,'\\');
	
	// check if there was a problem, and reset back to the root dir if so
	if(lastSlashPtr == NULL)
	{
		if(DBG)
		{
			printf("    -: problem with strrchr(), NULL returned\n");
		}
		resetToRoot();
		return;
	}

	if(DBG)
	{
		printf("    -: text to remove: %s\n",lastSlashPtr);
	}
	
	// figure out how much needs to get removed
	int textToRemoveLength = strlen(lastSlashPtr);
	int currDirLength = strlen(currentDir);
	int newCrumbLength = currDirLength-textToRemoveLength;

	// create a temp copy of the current dir, +1 for the null term
	char copy[currDirLength+1];
	strcpy(copy,currentDir);

	// invalidate the last pointer, and allocate an appropriately-size new block of memory
	currentDir = NULL;
	currentDir = (char*) calloc( newCrumbLength+1, sizeof(char) );

	// copy-in chars from the copy, up to just the length needed
	strncpy(currentDir, copy, newCrumbLength);

	// add the null term
	currentDir[newCrumbLength] = '\0';

	if(DBG)
	{
		printf("DEBUG: removeSubDirFromPrompt() ending...\n");
	}
	return;
}

void resetToRoot()
{
	if(DBG)
	{
		printf("DEBUG: resetToRoot() called...\n");
	}
	currentDir = "root";
	currentSector = bpb.BPB_RootClus;
	return;
}

void cleanUp()
{
	if( imgAlreadyOpened() )
	{
		tryCloseImage();
	}
	return;
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
 *  uint64_t: the current sector number that points to a block of data
 * 
 * returns: 
 *  int32_t: the value of the address for that block of data
 */
int32_t LBAToOffset( uint64_t sector )
{
	return ( (sector - 2) * bpb.BPB_BytesPerSec ) + ( bpb.BPB_BytesPerSec * bpb.BPB_RsvdSecCnt ) + ( bpb.BPB_NumFATs * bpb.BPB_FATSz32 * bpb.BPB_BytesPerSec );
}

/*
 * function: 
 *  nextLB
 * 
 * description: 
 *  Given a logical block address, look up into the first FAT and return the logical block address of 
 *   the next block in the file. If there are no further blocks, return -1.
 * 
 * parameters:
 *  uint64_t sector: the block address which to calculate the next address for
 * 
 * returns: 
 *  int16_t: return the next logical block, or -1 if the end
 */
int16_t nextLB( uint64_t sector )
{
	uint64_t FATAddress = ( bpb.BPB_BytesPerSec * bpb.BPB_RsvdSecCnt ) + ( sector*4 );
	int16_t val;
	fseek( fp, FATAddress, SEEK_SET );
	fread( &val, 2, 1, fp );
	return val;
}
