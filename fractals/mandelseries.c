/*
 * Name: Matt Hamrick
 * ID: 1000433109
 * 
 * Description:
 * 
 * Mandel command to start with:
 * ./mandel -s .000025 -y -1.03265 -m 7000 -x -.163013 -W 600 -H 600
 * 
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <float.h>

// how many times to run the mandel program
const int maxMandelRuns = 20;          

// create the mandel program parameters
// the 's' param changes for each instance of mandel, 
// so create a var for its initial and final values
const char * mandelParamX = "-0.163013";
const char * mandelParamY = "-1.03265";
const float initialMandelParamS = 2;
const float finalMandelParamS = 0.000025;
const char * mandelParamM = "7000";
const char * mandelParamW = "600";
const char * mandelParamH = "600";

// create the debug constant to enable/disable debug output
const bool DBG = true;

// function declarations (implementations after main())
bool validCommand( int, char * );
void startSeries( int );

int main ( int argc, char * argv[] )
{
    // check the validity of the command, bail-out if it's bad
    if( !validCommand( argc, argv[1] ) )
    {
        printf("error: please enter a valid number argument, for example: \n");
        printf("'mandelseries 10' will run 10 processes\n");
        exit(EXIT_FAILURE);
    }

    // user command is valid, so start the series
    startSeries( atoi( argv[1] ) );
    
    exit(EXIT_SUCCESS);
}

bool validCommand( int argCount, char * firstParam )
{
    if( argCount != 2 )
    {
        return false;
    }
    
    if( atoi( firstParam ) < 1 )
    {
        return false;
    }

    return true;
}

void startSeries( int maxRunningProcs )
{
  // calculate the S amount to subtract for each subsequent mandel run
  // using 49 because our first S value is set, so we have 49 available iterations
  // to get to our final value
  float mandelParamSFactor = (initialMandelParamS - finalMandelParamS) / 49;

  // create vars to hold the beginning and end of the output bmp filename
  char * bmpName = "mandel";
  char * bmpExtension = ".bmp";

  // allocate enough bytes to hold the longest filename: mandel##.bmp\0
  char bmpFilename[13];

  // initialize counter for how many images have been produced thus far
  int bmpCount = 0;

  // initialize counter for how many child procs are running
  int runningProcs = 0;

  // begin the outer loop that encompasses all child process and output creation
  while(true)
  {
    // since this outer loop waits for any children, we only want to break out
    // if we've reached the max # of images AND there are no more children running
    if( bmpCount == maxMandelRuns && runningProcs == 0)
    {
      break;
    }

    // this inner loop contains the logic for managing the # of active children
    while(true)
    {
      // since the outer loop manages the waiting and iterates until all children 
      // have exited, this condition makes sure this inner loop doesn't create
      // any more children if the required amount of output files have already 
      // been created, or are being created
      if( bmpCount == maxMandelRuns )
      {
        break;
      }

      // check if the # of children is the amount the user requested
      // if not, create another one to do the bidding
      // if yes, then we break out of this inner while loop and hand 
      // continue with the outer while
      if( runningProcs != maxRunningProcs )
      {
        
        pid_t pid = fork();

        if( pid == -1 )
        {
          // the call to fork() failed if pid == -1
          if(DBG)
          {
            printf("DEBUG: call to fork() returned -1 - exiting...\n");
            fflush(NULL);
          }
          printf("An error occurred. Please try again\n");
          exit(EXIT_FAILURE);
        }
        else if( pid == 0 )
        {
          // we're in the child process
          if(DBG)
          {
            printf("DEBUG: in child process after fork()\n");
          }

          // calculate the new S value each time the loop is run
          /*float currentMandelParamS = initialMandelParamS - ( bmpCount * mandelParamSFactor );

          // build the filename to be created and sent to the mandel program
          // TODO: can be refactored to a separate function that populates a provided buffer
          strcpy( bmpFilename, bmpName );
          char bmpNum[2];
          sprintf( bmpNum, "%d", bmpCount+1 );
          strcat( bmpFilename, bmpNum );
          strcat( bmpFilename, bmpExtension );*/

          sleep(runningProcs);

          if(DBG)
          {
            printf("DEBUG: child for bmp %d exiting\n", bmpCount);
          }

          exit(EXIT_SUCCESS);

        }
        else
        {
          // we're in the parent process

          // increment the running proc count and the bmp count
          runningProcs++;
          bmpCount++;

          if(DBG)
          {
            printf("DEBUG: child %d spawned to create bmp #%d\n", pid, bmpCount);
          }
        }

      } 
      else
      {
        break;
      } // if( runningProcs != maxRunningProcs )..else

    } // inner while

    wait(NULL);
    runningProcs--;

  } // outer while

}
