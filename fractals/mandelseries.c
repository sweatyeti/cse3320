/*
 * Name: Matt Hamrick
 * ID: 1000433109
 * 
 * Description:
 *  runs, in parallel, a user-provided number of child mandel processes to generate 
 *  mandel images, starting with a scale of 2, down to the desired scale amount.
 * 
 * Mandel command for the final image:
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
#include <stdbool.h>
#include <float.h>
#include <sys/time.h>

// how many total times to run the mandel program.
// this can be tweaked to change the number of output images.
// If tweaking this, no other changes are needed, and the program 
// logic will still work as expected.
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

// enable/disable debug output
bool DBG = false;

// enable/disable timing output
bool TIMING = true;

// function declarations (implementations after main())
bool validCommand( int, char * );
void runSeries( int );

int main ( int argc, char * argv[] )
{
    // check the validity of the command, bail-out if it's bad
    if( !validCommand( argc, argv[1] ) )
    {
        printf("error: please enter a valid number argument, for example: \n");
        printf("'mandelseries 10' will run 10 processes\n");
        exit(EXIT_FAILURE);
    }

    // declare the vars that hold time values, just in case timing has been enabled
    struct timeval seriesStart;
    struct timeval seriesEnd;

    // if this is being timed, get the time value before the series and store it
    if(TIMING)
    {
      gettimeofday( &seriesStart, NULL );
    }

    // user command has been validated, so start the series
    runSeries( atoi( argv[1] ) );

    // if this is being timed, get the time value after series and store it
    if(TIMING)
    {
      gettimeofday( &seriesEnd, NULL );
      // if this is being timed, calculate & output the time taken in microseconds to run the computation
      int computationTime = ( ( seriesEnd.tv_sec - seriesStart.tv_sec ) * 1000000 + ( seriesEnd.tv_usec - seriesStart.tv_usec ) );
      printf( "mandelseries: Computed time taken (in usec): %d\n", computationTime );
    }

    if(DBG)
    {
      printf("DEBUG: main() exiting...\n");
    }
    
    exit(EXIT_SUCCESS);
}

/*
 * function: 
 *  validCommand
 * 
 * description: 
 *  short function that does a couple simple checks on the user input to ensure 
 *  it's what is expected (only one actual argument that can be converted to a number).
 * 
 * parameters:
 *  - int argCount: the count of arguments, including the program name, used to run this program
 *  - char * firstParam, the first parameter provided after the program name on the command line
 * 
 * returns: 
 *  bool: whether the command used to run the program is valid (true) or not (false)
 */
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

/*
 * function: 
 *  runSeries
 * 
 * description: 
 *  primary function that contains all the child process and mandel program logic
 * 
 * parameters:
 *  - int maxRunningProcs: the number of child processes to run, passed-in via command-line param
 * 
 * returns: 
 *  void
 */
void runSeries( int maxRunningProcs )
{
  if(DBG)
  {
    printf("DEBUG: in runSeries()\n");
  }
  // calculate the S amount to subtract for each subsequent mandel run
  // using maxMandelRuns-1 because our first S value is set, so we have max-1 available iterations
  // to get to our final value
  float mandelParamSFactor = (initialMandelParamS - finalMandelParamS) / (maxMandelRuns - 1);

  // create vars to hold the beginning and end of the output bmp filename
  char * bmpName = "mandel";
  char * bmpExtension = ".bmp";

  // this string will hold the output image filename
  // allocate enough bytes to hold the longest filename: mandel##.bmp\0 = 12 chars + \0
  char bmpFilename[13];

  // initialize counter to track how many images have been created
  int bmpCount = 0;

  // initialize counter for how many child procs are running at a time
  int runningProcs = 0;

  // this flag controls if the final output string telling the user that we're just waiting
  // for all child procs to exit will be displayed. We only want to display it once, so this
  // flag is flipped once the string has been displayed on the console.
  bool waitingForAllToFinishOutputOnce = false;

  // begin the outer loop that encompasses all child process creation and mandel runs
  while(true)
  {
    // since this outer loop waits for any children, we only want to break out
    // if we've reached the max # of images AND there are no more children running
    if( bmpCount == maxMandelRuns && runningProcs == 0)
    {
      if(DBG)
      {
        printf("DEBUG->parent: all output files created & and child procs have exited..\n");
        printf("DEBUG->parent: exiting outer loop in runSeries()..\n");
      }
      break;
    }

    // this inner loop contains the logic for managing the # of active children
    while(true)
    {
      // since the outer loop manages the waiting and iterates until all children 
      // have exited, this condition makes sure this inner loop doesn't create
      // any more children if the required amount of output files have already 
      // been created, or are being created.
      // this logic also is what allows the user to enter an amount of child 
      // processes > how many images will be created, and still work properly.
      // In other words, even if the user requested 60 processes when only 50 are needed,
      // the logic will now allow any more children to be created once 50 have been reached.
      if( bmpCount == maxMandelRuns )
      {
        // at this point, we're just waiting for existing children to finish, 
        // so inform the user one time
        if ( !waitingForAllToFinishOutputOnce )
        {
          // sleep to give the 50th child a little extra time to get started
          // the sleep helps make sure the printf outputs the string at the very end
          sleep(1);
          printf("The last mandel child process has been started. Waiting for all to exit...\n\n");
          waitingForAllToFinishOutputOnce = true;
        }
        break;
      }

      // check if the # of children is the amount the user requested
      // if not, create another one to do the bidding
      // if yes, then we break out of this inner while loop and 
      // return control to the outer loop
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
          
          // since fork failed, the logic to exit these loops may never be satisfied, so hard exit
          exit(EXIT_FAILURE);
        }
        else if( pid == 0 )
        {
          // we're in the child process

          // calculate the new S value each time the loop is run
          float currentMandelParamS = initialMandelParamS - ( bmpCount * mandelParamSFactor );

          // build the filename to be created and sent to the mandel program
          strcpy( bmpFilename, bmpName );
          char bmpNum[2];
          sprintf( bmpNum, "%d", bmpCount+1 );
          strcat( bmpFilename, bmpNum );
          strcat( bmpFilename, bmpExtension );

          // command for reference:
          // mandel -s .000025 -y -1.03265 -m 7000 -x -.163013 -W 600 -H 600 mandel##.bmp

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

          // the execvp command expects the argument array to be terminated by a NULL pointer
          mandelArgList[15] = NULL;

          if(DBG)
          {
            printf("DEBUG->child: command to be run: %s %s %s ",mandelArgList[0],mandelArgList[1],mandelArgList[2]);
            printf("%s %s %s %s %s ",mandelArgList[3],mandelArgList[4],mandelArgList[5],mandelArgList[6],mandelArgList[7]);
            printf("%s %s %s %s %s %s\n",mandelArgList[8],mandelArgList[9],mandelArgList[10],mandelArgList[11],mandelArgList[12],mandelArgList[13]);
            printf("DEBUG->child: calling execv()..\n");
          }

          // reset errno in case of any issues, then run the execvp command, 
          // passing-in the name of the mandel file and the argument list
          // we just built
          errno = 0;
          execvp("./mandel", mandelArgList);

          if(DBG || errno != 0)
          {
            printf( "ERROR -> after execv: %d: %s\n", errno, strerror(errno) );
            exit(EXIT_FAILURE);
          }


          // the below code was used during testing of the loop logic to force the child processes
          // to sleep for a varying amount of time, to simulate wait time
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
          // these are what keep track of how many children are currently running,
          // and how many output images have been created
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
        // if we've reached the maximum amount of children procs per the user input, 
        // then break out of the inner loop and give control back to the outer loop
        break;
      } // if( runningProcs != maxRunningProcs )..else

    } // inner while

    // the outer loop waits for any children processes to exit
    // once one has exited, we decrement the counter of running children and the 
    // loop continues, at which point the inner loop will be entered and the check 
    // for how many children are running and how many images have been created.
    wait(NULL);
    runningProcs--;

  } // outer while

}
