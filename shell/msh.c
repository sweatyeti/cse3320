/*
 * Name: Matt Hamrick
 * ID: 1000433109
 * 
 * TODO:
 *  - [opt] refactor/encapsulate main areas of code into functions
 *     - move all checking for handled commands to a single function
 *  - [opt] implement better debug logging
 * 
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

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

#define WHITESPACE " \t\n"          // We want to split our command line up into tokens
                                    // so we need to define what delimits our tokens.
                                    // In this case  white space
                                    // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255        // The maximum command-line size

#define MAX_NUM_ARGUMENTS 10        // Mav shell only supports ten arguments (req 9)

#define DEBUGMODE false              // Output debug/verbose logging if true

#define MAX_PID_HISTORY 10          // The number of child PIDs to keep in the history

#define MAX_CMD_HISTORY 15          // The number of commands to keep in the history

int pidHistory[MAX_PID_HISTORY];    // Global storage for the PID history
int pidHistoryCount = 0;            // Global count of PID history depth

char * cmdHistory[MAX_CMD_HISTORY]; // Global storage for the command history
int cmdHistoryCount = 0;            // Global count of command history depth

int historyLoopCounter = 0;         // Global counter to keep track of cmdHistory looping
                                    // this is to assist with infinite loop detection/prevention

struct sigaction sigAct;            // Global sigaction for signal handling

// function declarations (implementations after main())
void addCmdToHistory( char * );
void outputCmdHistory( void );
void addPidToHistory( int );
void outputPidHistory( void );
bool fetchPreviousCmd( int, char * );
void setupSigHandling( void );
void backgroundLastProcess( void );
void parentProcess( int, bool );
void handleCd( char * );

int main()
{

  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );
  bool cmdFromHistory = false;
  
  setupSigHandling();

  while( true )
  {
    // first check if we're re-running a previous cmd
    // if we are, then don't display the prompt and don't ask 
    // for input since we already have the cmd
    if(!cmdFromHistory)
    {
      // since we're asking for input, reset the loop counter
      historyLoopCounter = 0;
      
      // Print out the msh prompt
      printf ("msh> ");

      // Read the command from the commandline.  The
      // maximum command that will be read is MAX_COMMAND_SIZE
      // This while command will wait here until the user
      // inputs something since fgets returns NULL when there
      // is no input
      while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );
      
    }
    
    // save the raw command, removing any \r or \n chars from the end, for later use
    char * rawCmd = strdup( cmd_str );
    rawCmd[strcspn(rawCmd, "\r\n")] = 0;
    
    // check the first character entered to see if the user is looking to execute a command
    // from teh command history (format is !n, where n is the place in the cmdHistory)
    if(rawCmd[0] == '!')
    {
      // create a flag and fetch the previous command
      bool cmdGood = false;
      cmdGood = fetchPreviousCmd( atoi(rawCmd+1), cmd_str );
      
      // if the cmd entered is not good, inform the user and reset
      if( !cmdGood )
      {
        printf("Command not in history.\n");
        cmdFromHistory = false;
        continue;
      }
      else
      {
        // the request to re-run a previous command was good
        // the user didn't explicitly enter the cmd to be re-run, so don't add it to the history
        if(!cmdFromHistory)
        {
          addCmdToHistory(rawCmd);
        }
        
        // set the flag since we are re-running a previous cmd, and continue the main loop
        cmdFromHistory = true;
        continue;
      }
    }

    // Parse input - use MAX...+1 because we need to accept 10 params PLUS the command (req 9)
    char * tokens[MAX_NUM_ARGUMENTS+1];

    int   token_count = 0;                                 
    
    // Pointer to point to the token
    // parsed by strsep
    char * arg_ptr;                                         
                                                           
    char * working_str  = strdup( cmd_str );
    
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
    
    // if this command was actually entered by the user (not from the history),
    // then add it to the history (req15)
    if(!cmdFromHistory)
    {
      addCmdToHistory(rawCmd);
    }
    
    // we're finished with the cmdFromHistory flag, so reset it
    cmdFromHistory = false;
    
    // check if the user wanted to list the command history (req15)
    if( strcmp(command, "history") == 0 )
    {
      outputCmdHistory();
      continue;
    }
    
    // check if the user wanted to list the PID history (req14)
    if( strcmp(command, "showpids") == 0 )
    {
      outputPidHistory();
      continue;
    }
    
    // check if the user wanted to background the last process (req8)
    if( strcmp(command, "bg") == 0 )
    {
      backgroundLastProcess();
      continue;
    }
    
    // check if the user is trying to change directories (req13)
    if( strcmp(command, "cd") == 0 )
    {
      handleCd(tokens[1]);
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
    
      // grab and store the current working directory
      char * cwdBuf = NULL;
      cwdBuf = getcwd(NULL, 0);
      
      if(DEBUGMODE)
      {
        printf("DEBUG: current working directory: %s\n", cwdBuf);
        //printf("strlen(cwdBuf) = %d\n", strlen(cwdBuf));
        //printf("strlen(command) = %d\n", strlen(command));
      }
      
      // allocate enough memory to store a string representation of the cwd+'/'+command+\0
      char * cwdPlusCommand = (char *)malloc( strlen(cwdBuf) + strlen(command) + 2 );
      
      // concatenate all the pieces noted above into cwdPlusCommand
      strcat(cwdPlusCommand, cwdBuf);
      char * fwdSlash = "/";
      strcat(cwdPlusCommand, fwdSlash);
      strcat(cwdPlusCommand, command);
      
      free(cwdBuf);
      
      // prep the errno variable for the exec call
      errno = 0;
      execv(cwdPlusCommand, tokens);
        
      if(DEBUGMODE && errno != 0)
      {
        printf( "ERROR -> after execv -> %d: %s\n", errno, strerror(errno) );
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
        
        if(DEBUGMODE && errno != 0)
        {
          printf( "ERROR -> after execv: %d: %s\n", errno, strerror(errno) );
        }
        
        if(errno == 2)
        {          
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
          
          if(DEBUGMODE && errno != 0)
          {
            printf( "ERROR -> after execv:%d: %s\n", errno, strerror(errno) );
          }
          
          if(errno == 2)
          {
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
            
            if(DEBUGMODE && errno != 0)
            {
              printf( "DEBUG: after execv: %d: %s\n", errno, strerror(errno) );
            }
            
            if(errno==2)
            {
              // inform the user that the command wasn't found (req. 2)
              printf("%s: command not found\n", command);
            }
          }
        }
      }
      
      //fflush(NULL);
      //free(cwdPlusCommand);
      
      if(DEBUGMODE)
      {
        printf("DEBUG: child process exiting...\n");
      }
      
      exit(EXIT_SUCCESS);
    }
    else
    {
      // we're in the parent process
      parentProcess(pid, true);
      
    }

    free( working_root );

  }
  return 0;
}

/*
 * function: 
 *  addCmdToHistory
 * 
 * description: 
 *  adds the provided command into the global command history array
 *  encapsulates all necessary logic to keep the command history up-to-date and correct
 * 
 * parameters:
 *  char * cmd: pointer to the command string
 * 
 * returns: 
 *  void
 */
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
    printf("DEBUG: Adding command #%d: '%s' to command history...\n", cmdHistoryCount, cmd);
  }
  cmdHistory[cmdHistoryCount-1] = cmd;
}

/*
 * function: 
 *  outputCmdHistory
 * 
 * description: 
 *  iterates through teh command history global array and outputs the commands
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  void
 */
void outputCmdHistory()
{
  int i;
  for( i = 0 ; i < cmdHistoryCount ; i++ )
  {
    printf("%d: %s\n", i, cmdHistory[i]);
  }
}

/*
 * function: 
 *  addPidToHistory
 * 
 * description: 
 *  adds the provided forked child PID into the global PID history array
 *  encapsulates all necessary logic to keep the PID history up-to-date and correct
 * 
 * parameters:
 *  int pid: the PID to be saved
 * 
 * returns: 
 *  void
 */
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

/*
 * function: 
 *  outputPidHistory
 * 
 * description: 
 *  iterates through teh PID history global array and outputs the PIDs
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  void
 */
void outputPidHistory()
{
  int i;
  for( i = 0; i < pidHistoryCount; i++ )
  {
    printf("%d: %d\n", i, pidHistory[i]);
  }
  
}

/*
 * function: 
 *  fetchPreviousCmd
 * 
 * description: 
 *  if the user input !n, replace the original raw command with the command from the history.
 *  checks to make sure n is valid.
 *  also implements loop detection and prevention, and takes action if a loop is detected
 * 
 * parameters:
 *  int whichCmd: the integer representation of n
 *  char * rawCmd: the raw command entered, so it can be replaced, thus allowing it 
 *    to be re-tokenized
 * 
 * returns: 
 *  bool: true if the user input is accepted; false otherwise
 */
bool fetchPreviousCmd(int cmdIndex, char * rawCmd)
{  
  // validate user input by checking to make sure the requested previous cmd index
  // is within the bounds of the existing cmdHistory array
  if( cmdIndex >= cmdHistoryCount || cmdIndex < 0 )
  {
    return false;
  }
  
  // keep track of how many loop iterations have occurred
  historyLoopCounter++;
  
  if(DEBUGMODE)
  {
    printf("DEBUG: fetching previous command #%d: '%s'\n", cmdIndex, cmdHistory[cmdIndex] );
  }
  
  // if the cmdIndex is valid, copy the cmd from the history to the rawCmd, 
  // which is used in the main loop for the actual cmd to run, then return
  strcpy( rawCmd, strcat( cmdHistory[cmdIndex], "\0" ) );
  
  // if we've looped more times than there are commands in the history, we're in an infinite loop
  // check for this and intervene
  if(historyLoopCounter > MAX_CMD_HISTORY)
  {
    printf("Infinite loop detected; invalidating command and returning to Mav shell..\n");
    rawCmd = '\0';
    historyLoopCounter = 0;
    return false;
  }
  
  return true;
}

/*
 * function: 
 *  setupSigHandling
 * 
 * description: 
 *  configures the process to ignore SIGINT and SIGTSTP
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  void
 */
void setupSigHandling()
{
  // zero-out the sigaction struct
  memset (&sigAct, '\0', sizeof(sigAct) );
  
  // set the sigaction handler to ignore SIGTSTP and SIGINT
  sigAct.sa_handler = SIG_IGN;
  
  // reset errno just in case there are errors with the sigaction
  errno = 0;
  
  // install the handler for SIGINT (req12)
  // output error text if debugmode is enabled if there's an issue
  if( sigaction(SIGINT, &sigAct, NULL) != 0 && DEBUGMODE)
  {
    printf("ERROR -> %d: %s\n", errno, strerror(errno));
  }
  
  // install the handler for SIGTSTP (req12)
  // output error text if debugmode is enabled if there's an issue
  if( sigaction(SIGTSTP, &sigAct, NULL) != 0 && DEBUGMODE)
  {
    printf("ERROR -> %d: %s\n", errno, strerror(errno));
  }
  
  // install the handler for SIGCHLD
  // output error text if debugmode is enabled if there's an issue
  /*if( sigaction(SIGCHLD, &sigAct, NULL) != 0 && DEBUGMODE)
  {
    printf("ERROR -> %d: %s\n", errno, strerror(errno));
  }*/
  
}

/*
 * function: 
 *  backgroundLastProcess
 * 
 * description: 
 *  will send the SIGCONT signal to the last PID to be run, if it exists
 * 
 * parameters:
 *  none
 * 
 * returns: 
 *  void
 */
void backgroundLastProcess()
{
  // check to make sure there is at least one PID in the history, return otherwise
  if( pidHistoryCount < 1 ) return;
  
  // retrieve the last created PID
  int pid = pidHistory[pidHistoryCount-1];
  
  // simple check to make sure we have a PID > 0 - return otherwise
  if( pid <= 0 ) return; 
  
  // reset errno for debugging purposes
  errno = 0;
  
  // build the sigval for sigqueue, providing the answer to the Ultimate Question of Life, etc.
  union sigval leSigval;
  leSigval.sival_int = 42; 
  
  // check if the desired process is still alive by sending the null signal (0) to it
  // if it is, go ahead and send the continue (?) signal
  if ( sigqueue(pid, 0, leSigval) == 0 )
  {
    if(DEBUGMODE)
    {
      printf( "DEBUG: backgroundLastProcess: bg'ing last PID %d", pid );
    }
    
    // PID exists, reset errno again and send the signal
    errno = 0;
    sigqueue(pid, SIGCONT, leSigval);
    
    parentProcess(pid, false);
    
    if(errno != 0 && DEBUGMODE)
    {
      printf( "DEBUG: backgroundLastProcess: errno after sigqueue = %d: %s\n", errno, strerror(errno) );
    }
  }
  else
  {
    if(DEBUGMODE)
    {
      printf( "DEBUG: backgroundLastProcess: errno after null sigqueue = %d: %s\n", errno, strerror(errno) );
    }
  }
}

/*
 * function: 
 *  parentProcess
 * 
 * description: 
 *  after fork(), handles all the parent process duties:
 *   - add the child PID to the history, if appropriate
 *   - wait for the child PID
 * 
 * parameters:
 *  - int childPid: the child PID from fork()
 *  - bool addChildPidToHistory: if true, the child PID will be added to the pidHistory
 * 
 * returns: 
 *  void
 */
void parentProcess(int childPid, bool addChildPidToHistory)
{
  // variable to hold status of child process to wait on
  int childStatus;
  
  if(DEBUGMODE)
  {
    printf("DEBUG: parentProcess -> child PID=%d\n", childPid);
  }
  
  // if this is a new child, add it to the history
  if( addChildPidToHistory )
  {
    addPidToHistory(childPid);
  }
  
  // wait for the child process to exit or suspend
  (void)waitpid( childPid, &childStatus, 0|WUNTRACED );
  
  if(DEBUGMODE)
  {
    // output status depending on how the child process exited (signal vs. normal)
    if(WIFSIGNALED(childStatus))
    {
      printf("\nERROR -> child process %d exited with unhandled", childPid);
      printf(" sig status %d: %s\n", WTERMSIG(childStatus), strsignal(WTERMSIG(childStatus)));
    }
    else if(WIFSTOPPED(childStatus))
    {
      // ctrl-z (SIGTSTP) gets here
      printf( "\nDEBUG: child process %d exited with status %d ", childPid, childStatus);
      printf( "and signal %d: %s\n", WSTOPSIG(childStatus), strsignal(WSTOPSIG(childStatus)));
    }
    else
    {
      // ctrl-c (SIGINT) gets here
      printf( "\nDEBUG: child process %d exited with status %d\n", childPid, childStatus);
    }
    
  }
  //fflush(NULL);
}

/*
 * function: 
 *  handleCd
 * 
 * description: 
 *  handles the 'cd' command (change working directory)
 * 
 * parameters:
 *  char * cmdParam: cd only takes one parameter, so this param
 *                   will either be that param, or NULL
 * 
 * returns: 
 *  void
 */
void handleCd(char * cmdParam)
{
  // grab and store the current working directory
  char * cwd = NULL;
  cwd = getcwd(NULL, 0);
  
  if(DEBUGMODE)
  {
    printf("DEBUG: handleCd() -> cwd: %s\n", cwd);
  }
  
  if(cmdParam != NULL)
  {
    // allocate enough memory to hold the cwd plus the parameter 
    // entered by the user, plus the '/' and terminating null
    char * newCwd = (char*) malloc( strlen(cwd) + strlen(cmdParam) + 2 );
    
    // build the new cwd based on what the user entered
    strcpy(newCwd, cwd);
    //char * fwdSlash = "/";
    newCwd[strlen(newCwd)] = '/';
    newCwd[strlen(newCwd)] = '\0';
    //strcat(newCwd, fwdSlash);
    strcat(newCwd, cmdParam);
    
    if(DEBUGMODE)
    {
      printf("DEBUG: handleCd() -> proposed cwd = %s\n", newCwd);
    }
    
    // reset errno, as chdir can change it
    errno = 0;
    
    if( chdir(newCwd) != 0 && DEBUGMODE )
    {
      printf("DEBUG: handleCc() -> after chdir: %d: %s\n", errno, strerror(errno));
    }

    free(newCwd);
  }
  else
  {
    printf("%s\n", cwd);
  }
  
  //free(cwd);
}
