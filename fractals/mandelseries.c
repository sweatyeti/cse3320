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
const int maxMandelRuns = 50;          

// create the mandel program parameters
// the 's' param changes for each instance of mandel, 
// so create a var for its initial and final values
char * mandelParamX = "-0.163013";
char * mandelParamY = "-1.03265";
float initialMandelParamS = 2;
float finalMandelParamS = 0.000025;
char * mandelParamM = "7000";
char * mandelParamW = "600";
char * mandelParamH = "600";

// create the debug constant to enable/disable debug output
const bool DBG = false;

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

    if(DBG)
    {
      printf("DEBUG: main() exiting...\n");
    }
    
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
  if(DBG)
  {
    printf("DEBUG: in startSeries()\n");
  }
  // calculate the S amount to subtract for each subsequent mandel run
  // using maxMandelRuns-1 because our first S value is set, so we have max-1 available iterations
  // to get to our final value
  float mandelParamSFactor = (initialMandelParamS - finalMandelParamS) / (maxMandelRuns - 1);

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
      if(DBG)
      {
        printf("DEBUG->parent: all output files created & and child procs have exited..\n");
        printf("DEBUG->parent: exiting main loop in startSeries()..\n");
      }
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
        // do the fork thing
        errno = 0;
        pid_t pid = fork();

        if( pid == -1 )
        {
          // the call to fork() failed if pid == -1
          if(DBG)
          {
            printf("ERROR -> after fork(): %d: %s.. exiting...\n", errno, strerror(errno) );
          }
          printf("An error occurred. Please try again\n");
          
          exit(EXIT_FAILURE);
        }
        else if( pid == 0 )
        {
          // we're in the child process

          // calculate the new S value each time the loop is run
          float currentMandelParamS = initialMandelParamS - ( bmpCount * mandelParamSFactor );

          // build the filename to be created and sent to the mandel program
          // TODO: can be refactored to a separate function that populates a provided buffer
          strcpy( bmpFilename, bmpName );
          char bmpNum[2];
          sprintf( bmpNum, "%d", bmpCount+1 );
          strcat( bmpFilename, bmpNum );
          strcat( bmpFilename, bmpExtension );

          // command for reference:
          // mandel -s .000025 -y -1.03265 -m 7000 -x -.163013 -W 600 -H 600 mandel#.bmp

          // construct the mandel argument list, starting with the less complicated ones
          char * mandelArgList[16];
          mandelArgList[0] = "mandel";
          mandelArgList[1] = "-y";
          mandelArgList[2] = mandelParamY;
          mandelArgList[3] = "-m";
          mandelArgList[4] = mandelParamM;
          mandelArgList[5] = "-x";
          mandelArgList[6] = mandelParamX;
          mandelArgList[7] = "-W";
          mandelArgList[8] = mandelParamW;
          mandelArgList[9] = "-H";
          mandelArgList[10] = mandelParamH;

          // since the -s argument value is a calculated float, we need to convert it to char *,
          // then it can be added to the arg list
          mandelArgList[11] = "-s";
          char argSBuffer[20];
          snprintf( argSBuffer, 20, "%f", currentMandelParamS );
          mandelArgList[12] = argSBuffer;

          // finally, add the filename to be output as the 14th and last argument
          mandelArgList[13] = "-o";
          mandelArgList[14] = bmpFilename;
          mandelArgList[15] = NULL;

          if(DBG)
          {
            printf("DEBUG->child: command to be run: %s %s %s ",mandelArgList[0],mandelArgList[1],mandelArgList[2]);
            printf("%s %s %s %s %s ",mandelArgList[3],mandelArgList[4],mandelArgList[5],mandelArgList[6],mandelArgList[7]);
            printf("%s %s %s %s %s %s\n",mandelArgList[8],mandelArgList[9],mandelArgList[10],mandelArgList[11],mandelArgList[12],mandelArgList[13]);
          }

          if(DBG)
          {
            printf("DEBUG->child: calling execv()..\n");
          }
          errno = 0;
          execvp("mandel", mandelArgList);

          if(DBG || errno != 0)
          {
            printf( "ERROR -> after execv: %d: %s\n", errno, strerror(errno) );
            exit(EXIT_FAILURE);
          }

          /*sleep(runningProcs);

          if(DBG)
          {
            printf("DEBUG->child: process for bmp %s exiting..\n", bmpFilename);
          }

          exit(EXIT_SUCCESS);*/

        }
        else
        {
          // we're in the parent process

          // increment the running proc count and the bmp count
          runningProcs++;
          bmpCount++;

          if(DBG)
          {
            printf("DEBUG->parent: child %d spawned to create bmp #%d..\n", pid, bmpCount);
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
