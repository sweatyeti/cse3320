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

#define NUM_MANDEL_RUNS 50          // how many times to run the mandel program

// define the constants to use for the mandel program
#define MANDEL_PARAM_X -0.163013    
#define MANDEL_PARAM_Y -1.03265
#define MANDEL_PARAM_S 0.000025
#define MANDEL_PARAM_M 7000
#define MANDEL_PARAM_W 600
#define MANDEL_PARAM_H 600

// function declarations (implementations after main())
bool validCommand( int, char * ); 

int main ( int argc, char * argv[] )
{
    // check the validity of the command, bail-out if it's bad
    if( !validCommand( argc, argv[1] ) )
    {
        printf("error: please enter a valid number argument, for example: \n");
        printf("'mandelseries 10' will run 10 processes\n");
        exit(EXIT_FAILURE);
    }

    // define and init the var to hold how many processes the user wants to run
    int numProcs = 0;
    numProcs = atoi( argv[1] );
    

    
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
