/*
 * Name: Matt Hamrick
 * ID: 1000433109
 * 
 */

// The MIT License (MIT)
// 
// Copyright (c) 2016, 2017 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 10    // Mav shell only supports ten arguments (req 9)

#define DEBUGMODE 1             // Output debug/verbose logging if == 1

int main()
{

  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

  while( 1 )
  {
    // Print out the msh prompt
    printf ("msh> ");

    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

    // Parse input - use MAX...+1 because we need to accept 10 params PLUS the command (req 9)
    char *tokens[MAX_NUM_ARGUMENTS+1];

    int   token_count = 0;                                 
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;                                         
                                                           
    char *working_str  = strdup( cmd_str );                

    // we are going to move the working_str pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *working_root = working_str;

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

    // DO STUFF
    
    
    if(DEBUGMODE)
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
    
    // store pointer to the first token (the command) for easy retrieval later
    char *command = tokens[0];

    // check for quit/exit commands and break out of main loop if received (req 5)
    if(strcmp(command,"quit") == 0 || strcmp(command,"exit") == 0)
    {
      free( working_root );
      break;
    }
    
    pid_t pid = fork();
    
    if(pid == -1)
    {
      // the call to fork() failed if pid == -1
      if(DEBUGMODE)
      {
        printf("DEBUG: call to fork() returned -1 - exiting...\n");
        fflush(NULL);
        break;
      }
      printf("An error occurred. Please try again\n");
    }
    else if(pid==0)
    {
      // we're in the child process
      if(DEBUGMODE)
      {
        printf("DEBUG: in child '%s' process after fork()\n", command);
      }
      
      char * cwdBuf = (char *)malloc((size_t)255);
      char * ptr;
      ptr = getcwd(cwdBuf, 255);
      //printf(cwdBuf);
      
      //char *result = malloc(strlen(cwdBuf)+strlen(s2)+1);
      
      execl(cwdBuf,"ls", NULL); // still not working
      //execl("/bin/ls","ls",NULL);
      fflush(NULL);
      
      free(ptr); // is this needed?
      exit(EXIT_SUCCESS);
    }
    else
    {
      // we're in the parent process
      
      // variable to hold status of waited-on child process
      int childStatus;
      
      // wait for the child process to exit or suspend
      (void)waitpid(pid, &childStatus, 0);
      
      if(DEBUGMODE)
      {
        // output status depending on how the child process exited (signal vs. normal)
        if(WIFSIGNALED(childStatus))
        {
          printf("DEBUG: child process %d exited with signal status %d\n", pid, WTERMSIG(childStatus));
        }
        else
        {
          printf("DEBUG: child process %d exited with status %d\n", pid, childStatus);
        }
        
      }
      fflush(NULL);
    }


    free( working_root );

  }
  return 0;
}
