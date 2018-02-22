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

#define WHITESPACE " \t\n"          // We want to split our command line up into tokens
                                    // so we need to define what delimits our tokens.
                                    // In this case  white space
                                    // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255        // The maximum command-line size

#define MAX_NUM_ARGUMENTS 10        // Mav shell only supports ten arguments (req 9)

#define DEBUGMODE 1                 // Output debug/verbose logging if == 1

#define MAX_PID_HISTORY 10          // The number of child PIDs to keep in the history

#define MAX_CMD_HISTORY 15          // The number of commands to keep in the history

int pidHistory[MAX_PID_HISTORY];    // Global storage for the PID history
int pidHistoryCount = 0;            // Global count of PID history depth

char * cmdHistory[MAX_CMD_HISTORY]; // Global storage for the command history
int cmdHistoryCount = 0;            // Global count of command history depth

typedef enum { false, true } bool;  // create a bool type, just in case

// function declarations
void addCmdToHistory( char * );
void outputCmdHistory( void );
void addPidToHistory( int );
void outputPidHistory( void );

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
    char * tokens[MAX_NUM_ARGUMENTS+1];

    int   token_count = 0;                                 
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char * arg_ptr;                                         
                                                           
    char * working_str  = strdup( cmd_str );
    
    // save the raw command, removing any \r or \n chars from the end, for preservation later
    char * rawCmd = strdup( cmd_str );
    rawCmd[strcspn(rawCmd, "\r\n")] = 0;
    
    if(DEBUGMODE)
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
    if( strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) 
    {
      free( working_root );
      break;
    }
    
    // keep track of the command history, using the fully-raw vegan rawCmd from earlier
    addCmdToHistory(rawCmd);
    
    // check if the user wanted to list the command history
    if( strcmp(command, "history") == 0 )
    {
      outputCmdHistory();
      continue;
    }
    
    // check if the user wanted to list the PID history
    if( strcmp(command, "showpids") == 0 )
    {
      outputPidHistory();
      continue;
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
        printf("DEBUG: in child process after fork()\n");
      }
      
      char * cwdBuf = (char *)malloc( (size_t)255 );
      char * ptr;
      ptr = getcwd(cwdBuf, 255);
      
      // allocate enough memory to store a string representation of the cwd+'/'+command+\0
      char * cwdPlusCommand = (char *)malloc( strlen(cwdBuf) + strlen(command) + 2 );
      
      // concatenate all the pieces noted above into cwdPlusCommand
      strcat(cwdPlusCommand, cwdBuf);
      char * fwdSlash = "/";
      strcat(cwdPlusCommand, fwdSlash);
      strcat(cwdPlusCommand, command);
      
      // prep the errno variable for the exec call
      errno = 0;
      
      if(DEBUGMODE)
      {
        printf("DEBUG: current working directory: %s\n", cwdBuf);
        printf("DEBUG: entered command: %s\n", command);
        printf("DEBUG: full command to exec: %s\n", cwdPlusCommand);
        
        //execl(cwdPlusCommand, command, NULL);
        execv(cwdPlusCommand, tokens);
        
        if(errno != 0)
        {
          printf("DEBUG: errno after exec: %d\n", errno);
          printf("DEBUG: error msg: %s\n", strerror(errno));
        }
      }
      else
      {
        //execl(cwdPlusCommand, command, NULL);
        execv(cwdPlusCommand, tokens);
      }
      
      // if errno==2 then the command was not found in the CWD
      // go through the other required directories to see if the command is found
      // will reset errno back to zero each time to ensure it's fresh
      if(errno == 2)
      {
        char * usrLocalBinStr = "/usr/local/bin/";
        char * usrLocalBinCmd = (char*) malloc( strlen(usrLocalBinStr) + strlen(command) + 1 );
        strcat(usrLocalBinCmd, usrLocalBinStr);
        strcat(usrLocalBinCmd, command);
        
        if(DEBUGMODE)
        {
          printf("DEBUG: attempting \"%s\" ... \n", usrLocalBinCmd);
        }
                
        errno = 0;
        execv(usrLocalBinCmd, tokens);
        
        if(errno == 2)
        {
          if(DEBUGMODE)
          {
            printf("DEBUG: errno after exec: %d\n", errno);
            printf("DEBUG: error msg: %s\n", strerror(errno));
          }
          
          char * usrBinStr = "/usr/bin/";
          char * usrBinCmd = (char*) malloc( strlen(usrBinStr) + strlen(command) + 1 );
          strcat(usrBinCmd, usrBinStr);
          strcat(usrBinCmd, command);
          
          if(DEBUGMODE)
          {
            printf("DEBUG: attempting \"%s\" ... \n", usrBinCmd);
          }
                  
          errno = 0;
          execv(usrBinCmd, tokens);
          
          if(errno == 2)
          {
            if(DEBUGMODE)
            {
              printf("DEBUG: errno after exec: %d\n", errno);
              printf("DEBUG: error msg: %s\n", strerror(errno));
            }
            
            char * binStr = "/bin/";
            char * binCmd = (char*) malloc( strlen(binStr) + strlen(command) + 1 );
            strcat(binCmd, binStr);
            strcat(binCmd, command);
            
            if(DEBUGMODE)
            {
              printf("DEBUG: attempting \"%s\" ... \n", binCmd);
            }
                    
            errno = 0;
            execv(binCmd, tokens);
            
            if(errno==2)
            {
              // inform the user that the command wasn't found (req. 2)
              printf("%s: command not found\n", command);
            }
            free(binCmd);
          }
          free(usrBinCmd);
        }
        free(usrLocalBinCmd);
      }
      
      fflush(NULL);
      free(ptr);
      free(cwdPlusCommand);
      exit(EXIT_SUCCESS);
    }
    else
    {
      // we're in the parent process
      
      // variable to hold status of child process to wait on
      int childStatus;
      
      if(DEBUGMODE)
      {
        printf("DEBUG: child PID=%d\n", pid);
      }
      
      // keep track of the created child PIDs
      addPidToHistory(pid);
      
      // wait for the child process to exit or suspend
      (void)waitpid(pid, &childStatus, 0);
      
      if(DEBUGMODE)
      {
        // output status depending on how the child process exited (signal vs. normal)
        if(WIFSIGNALED(childStatus))
        {
          printf("DEBUG: child process %d exited with sig status %d\n", pid, WTERMSIG(childStatus));
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

void addCmdToHistory(char * cmd)
{
  // increment our counter from the start to ensure it matches the point at which this function is called
  cmdHistoryCount++;
  
  // if the max # commands size has been reached, then get rid of the oldest one in the history (cmdHistory[0])
  // so, if MAX_CMD_HISTORY is n, check if this is the n+1 command
  if( cmdHistoryCount == MAX_CMD_HISTORY + 1 )
  {
    // Shift the char pointers one down in the array, so the 2nd oldest command now becomes the oldest
    int i;
    for( i = 0; i < MAX_CMD_HISTORY ; i++ )
    {
      cmdHistory[i] = cmdHistory[i+1];
    }
    
    if(DEBUGMODE)
    {
      printf("DEBUG: %d commands have been entered, shifting cmdHistory array...\n", cmdHistoryCount);
    }
    
    cmdHistoryCount--;
  }
  
  if(DEBUGMODE)
  {
    printf("DEBUG: Adding command #%d: '%s', to command history...\n", cmdHistoryCount, cmd);
  }
  
  cmdHistory[cmdHistoryCount-1] = cmd;
}

void outputCmdHistory()
{
  int i;
  for( i = 0 ; i < cmdHistoryCount ; i++ )
  {
    printf("%d: %s\n", i, cmdHistory[i]);
  }
}

void addPidToHistory(int pid)
{
  // increment our counter from the start to ensure it matches the point at which this function is called
  pidHistoryCount++;
  
  // if the max # PIDs size has been reached, then get rid of the oldest one in the history (pidHistory[0])
  // so, if MAX_PID_HISTORY is n, check if this is the n+1 PID
  if( pidHistoryCount == MAX_PID_HISTORY + 1 )
  {
    // Shift the ints one down in the array, so the 2nd oldest PID now becomes the oldest
    int i;
    for( i = 0; i < MAX_PID_HISTORY ; i++ )
    {
      pidHistory[i] = pidHistory[i+1];
    }
    
    if(DEBUGMODE)
    {
      printf("DEBUG: %d PIDs have been created, shifting pidHistory array...\n", pidHistoryCount);
    }
    
    pidHistoryCount--;
  }
  
  if(DEBUGMODE)
  {
    printf("DEBUG: Adding PID #%d: '%d', to PID history...\n", pidHistoryCount, pid);
  }
  
  pidHistory[pidHistoryCount-1] = pid;
}

void outputPidHistory()
{
  int i;
  for( i = 0; i < pidHistoryCount; i++ )
  {
    printf("%d: %d\n", i, pidHistory[i]);
  }
  
}
