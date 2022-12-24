#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <iomanip>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAXLEN 15001
#define MAXPIPE 1001
#define MAXPROCESS 512
#define MAXUSR 35
#define MAXMSGLEN 1100

#define SIG_USERPIPE SIGUSR1
#define SIG_BROADCAST SIGUSR2

// define the pipe, numbered pipe, redirect status
#define IS_PIPE_IN 1
#define IS_PIPE_OUT 1
#define IS_NUMPIPE 1
#define IS_NUMPIPE_ERR -1
#define NOT_NUMPIPE 0
#define IS_REDIRECT 1
using namespace std;

// multi user variables
int sockfd_for_signal = 0;
int client_id_for_signal = 0;

typedef struct user_info{
    char username[25];
    char ip_address[20];
    int port;
    pid_t pid;
} usr_info;

// shared memory pointer
usr_info* user_info_shm;
char* broadcast_msg_shm;
char* user_pipe_shm;

// rwg variables
string welcome_msg = "****************************************\n** Welcome to the information server. **\n****************************************\n";
string user_pipe_path = "./user_pipe/";
map<string, int> current_user_pipe;
int dev_null_input = open("/dev/null", O_RDONLY);
int dev_null_output = open("/dev/null", O_WRONLY);

// npshell variables
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
    int user_pipe_in;   // store the target user of user pipe input
    int user_pipe_out;  // store the target user of user pipe output
}cmd_status;

int rwg_command_decision(vector<string> args, string input, int sockfd, int client_id);
void rwg_who(vector<string> args, int sockfd, int client_id);
void rwg_tell(vector<string> args, string input, int sockfd, int client_id);
void rwg_yell(vector<string> args, string input, int sockfd, int client_id);
void rwg_name(vector<string> args, int sockfd, int client_id);
void rwg_exit(vector<string> args, int sockfd, int client_id);

int target_user_decision(string argument, int client_id);
void user_pipe_in_broadcast(int target_user_in, int sockfd, int client_id, string input);
void user_pipe_out_broadcast(int target_user_out, int sockfd, int client_id, string input);
void shell_operation(vector<string> arguments, string input, int sockfd, int client_id);

vector<string> string_parsing(string input);
void buildin_setenv(vector<string> args, int sockfd);
void buildin_printenv(vector<string> args, int sockfd);
void outside_command(vector<string> command, int* pipe_fd, cmd_status status, int sockfd, int client_id);
void command_decision(vector<string> command, int* pipe_fd, cmd_status status, int sockfd, int client_id);

// pipe process functions
void child_pipe_process(int* pipe_fd, cmd_status status, int client_id);
void parent_pipe_process(int* pipe_fd, cmd_status status, pid_t pid, int client_id);