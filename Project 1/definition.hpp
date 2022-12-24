#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

#define MAXLEN 15001
#define MAXPIPE 1001
#define MAXPROCESS 512

// define the pipe, numbered pipe, redirect status
#define IS_PIPE_IN 1
#define IS_PIPE_OUT 1
#define IS_NUMPIPE 1
#define IS_NUMPIPE_ERR -1
#define NOT_NUMPIPE 0
#define IS_REDIRECT 1
using namespace std;

int line_cnt = 0;
int numbered_pipe[2*MAXPIPE];
vector<vector<pid_t>> pid_wait(MAXPIPE);
vector<bool> numpiped_to(MAXPIPE, false);   // store if the current numbered pipe is piped by some command or not (default set to false)

typedef struct command_statue{
    bool pipe_in;       // store if the input of the command is from the previous command (false for input from stdin)
    bool pipe_out;      // store if the command is piped to next command (false for output to stdout, numpipe or file redirect)
    int is_numpipe;     // store the target line to pipe (how many lines ahead, >0 for stdout, <0 for stderr, 0 for not numpipe)
    int is_redirect;    // store the fd of the redirect file (0 for not redirect)
    int pipe_idx;       // store the pipe_pair number of current command
}cmd_status;

vector<string> string_parsing(string input);
void buildin_setenv(vector<string> args);
void buildin_printenv(vector<string> args);
void outside_command(vector<string> command, int* pipe_fd, cmd_status status);
void command_decision(vector<string> command, int* pipe_fd, cmd_status status);

// pipe process functions
void child_pipe_process(int* pipe_fd, cmd_status status);
void parent_pipe_process(int* pipe_fd, cmd_status status, pid_t pid);