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

// how many times to run the mandel program
#define NUM_MANDEL_RUNS 50          

// define the constants to use for the mandel program
#define MANDEL_PARAM_X -0.163013    
#define MANDEL_PARAM_Y -1.03265
#define MANDEL_PARAM_S 0.000025
#define MANDEL_PARAM_M 7000
#define MANDEL_PARAM_W 600
#define MANDEL_PARAM_H 600

// define the debug constant to enable/disable debug output
//#define DBG true

bool DBG = true;

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
    //startSeries( atoi( argv[1] ) );
    
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

void startSeries( int numProcs )
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




    }
    else
    {
      // we're in the parent process


    }
}
