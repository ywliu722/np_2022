#include "np_simple.hpp"

void shell_operation(vector<string> arguments, int sockfd){
    
    signal(SIGCHLD, SIG_IGN);

    bool pipe_exist = !IS_PIPE_IN;  // used to identify if the current command's input is piped by the previous command
    bool last_cmd_numpipe = false;  // used to identify if the numbered pipe added the line_cnt
    int last_command_pos = 0, pipe_index = 0;   // identify the starting index of the current command and the index of pipe_pair for the current command
    int pipe_fd[MAXPIPE*2]; // [read_fd, write_fd]
    
    for(int i = 0; i < arguments.size(); i++){
        last_cmd_numpipe = false;
        // handling ordinary pipe
        if(arguments[i] == "|"){
            vector<string> command;
            for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
            pipe(pipe_fd + pipe_index * 2);
            command_decision(command, pipe_fd, {pipe_exist, IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index}, sockfd);
                
            pipe_index = (pipe_index + 1) % MAXPIPE;
            last_command_pos = i + 1;
            pipe_exist = IS_PIPE_IN;
        }

        // handling numbered pipe
        else if( (arguments[i][0] == '|' || arguments[i][0] == '!') && arguments[i].size() > 1){
            int target = 0;

            // determine the type of numbered pipe
            if(arguments[i][0] == '|') target = IS_NUMPIPE;
            else target = IS_NUMPIPE_ERR;

            // check the "N" is valid or not, and convert it to integer form
            for(int j = 1; j < arguments[i].size(); j++){
                if(arguments[i][j] < '1' || arguments[i][j] > '9'){
                    target = 0;
                    break;
                }
            }

            // building target numbered pipe
            if(target){
                target = target * stoi(arguments[i].substr(1, arguments[i].size() - 1));
                int target_line = (line_cnt + abs(target)) % MAXPIPE;
                numpiped_to[target_line] = true;
                // check if there is any command piped to the same target
                if(pid_wait[target_line].size() == 0){
                    pipe(numbered_pipe + target_line * 2);
                }
            }

            vector<string> command;
            for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes

            command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, target, !IS_REDIRECT, pipe_index}, sockfd);   // pipe_index is not used in this situation

            // modify the status of the commands
            last_cmd_numpipe = true;
            pipe_exist = !IS_PIPE_IN;
            last_command_pos = i + 1;
            line_cnt = (line_cnt + 1) % MAXPIPE;
        }

        // handling redirection
        else if(arguments[i] == ">"){
            // open target file and check if it is opened successfully
            if( i + 1 >= arguments.size()){
                cout << "Usage: [command] > [filename]" <<endl;
                continue;
            }
            int redirect_fd = open(arguments[i+1].c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);

            vector<string> command;
            for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
                
            command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, redirect_fd, pipe_index}, sockfd);   // pipe_index is not used in this situation

            // close the opened file and modify the status of the commands
            close(redirect_fd);
            pipe_exist = !IS_PIPE_IN;
            last_command_pos = i + 2;

        }
            
    }

    if(last_command_pos != arguments.size()){
        vector<string> command;
        for(int j = last_command_pos; j < arguments.size(); j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
        command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index}, sockfd);   // pipe_index is not used in this situation
    }

    if(!last_cmd_numpipe) line_cnt = (line_cnt + 1) % MAXPIPE;
}

vector<string> string_parsing(string input){
    // parsing the input with stringstream
    string arg;
    vector<string> ret;
    stringstream ss(input);
    while(ss >> arg) ret.push_back(arg);
    return ret;
}

void buildin_setenv(vector<string> args, int sockfd){
    if(args.size() != 3){
        string errmsg = "Usage: setenv [var] [value]\n";
        write(sockfd, errmsg.c_str(), errmsg.length());
        return;
    }
    setenv(args[1].c_str(), args[2].c_str(), 1);
}

void buildin_printenv(vector<string> args, int sockfd){
    if(args.size() != 2){
        string errmsg = "Usage: printenv [var]\n";
        write(sockfd, errmsg.c_str(), errmsg.length());
        return;
    }
    char* ret = getenv(args[1].c_str());
    if(ret){
        string msg = string(ret) + "\n";
        write(sockfd, msg.c_str(), msg.length());
    }
}

void outside_command(vector<string> command, int* pipe_fd, cmd_status status, int sockfd){
    // allocate char* array and copy command to argument
    char **arguments = new char* [command.size() + 1];
    for(int i = 0; i < command.size(); i++){
        arguments[i] = new char [command[i].size() + 1];
        strcpy(arguments[i], command[i].c_str());
    }
    arguments[command.size()] = NULL;

    // fork if possible, or just wait for it
    pid_t pid = fork();
    int waiting;
    if(pid < 0){
        wait(&waiting);
        pid = fork();
    }

    // child process
    if(pid == 0){
        dup2(sockfd, STDOUT_FILENO);
        dup2(sockfd, STDERR_FILENO);
        child_pipe_process(pipe_fd, status);
        if(execvp(arguments[0], arguments) == -1) cerr << "Unknown command: [" << arguments[0] << "]." <<endl;
        exit(0);
    }

    // parent process
    else{
        parent_pipe_process(pipe_fd, status, pid);
    }

    // deallocate the char* array
    for(int i = 0; i < command.size() - 1; i++){
        delete [] arguments[i];
    }
    delete [] arguments;
}

void command_decision(vector<string> command, int* pipe_fd, cmd_status status, int sockfd){
    if(command[0] == "setenv") buildin_setenv(command, sockfd);
    else if(command[0] == "printenv") buildin_printenv(command, sockfd);
    else outside_command(command, pipe_fd, status, sockfd);
}

void child_pipe_process(int* pipe_fd, cmd_status status){
    // input pipe process
    // check if the child process get the input from the previous one via pipe
    if(status.pipe_in){
        int previous_pipe;
        if(status.pipe_idx - 1 < 0) previous_pipe = (status.pipe_idx - 1) + MAXPIPE;
        else previous_pipe = status.pipe_idx - 1;

        close(pipe_fd[2 * previous_pipe + 1]); // write fd
        dup2(pipe_fd[2 * previous_pipe], STDIN_FILENO); // dup the stdin to the pipe
        close(pipe_fd[2 * previous_pipe]); // read fd
    }
    // check if the child process get the input from the numbered pipe
    // check > 1 because we just put the current child into the pid waiting queue
    else if(numpiped_to[line_cnt]){
        close(numbered_pipe[2 * line_cnt + 1]); // write fd
        dup2(numbered_pipe[2 * line_cnt], STDIN_FILENO); // dup the stdin to the pipe
        close(numbered_pipe[2 * line_cnt]); // read fd

    }

    // ouput pipe process
    // check if the child process is output to an ordinary pipe
    if(status.pipe_out){
        close(pipe_fd[2 * status.pipe_idx]); // read fd
        dup2(pipe_fd[2 * status.pipe_idx + 1], STDOUT_FILENO); // dup the stdout to the pipe
        close(pipe_fd[2 * status.pipe_idx + 1]); // write fd
    }
    // check if the child process is output to a numbered pipe
    else if(status.is_numpipe != 0){
        int target_line = (line_cnt + abs(status.is_numpipe)) % MAXPIPE;
        close(numbered_pipe[2 * target_line]); // read fd
        dup2(numbered_pipe[2 * target_line + 1], STDOUT_FILENO); // dup the stdout to the pipe
        if(status.is_numpipe < 0) dup2(numbered_pipe[2 * target_line + 1], STDERR_FILENO); // dup the stderr to the pipe
        close(numbered_pipe[2 * target_line + 1]); // write fd
        numpiped_to[line_cnt] = false;
    }
    // check if the child process is output to a file
    else if(status.is_redirect != 0){
        dup2(status.is_redirect, STDOUT_FILENO);
    }
}

void parent_pipe_process(int* pipe_fd, cmd_status status, pid_t pid){
    // put the child process pid into the waiting queue
    pid_wait[line_cnt].push_back(pid);

    // pipe process
    // check if the child process get the input from the previous one via pipe
    if(status.pipe_in){
        int previous_pipe;
        if(status.pipe_idx - 1 < 0) previous_pipe = (status.pipe_idx - 1) + MAXPIPE;
        else previous_pipe = status.pipe_idx - 1;

        close(pipe_fd[2 * previous_pipe]);
        close(pipe_fd[2 * previous_pipe + 1]);
    }
    // check if the child process get the input from the numbered pipe
    else if(numpiped_to[line_cnt]){
        close(numbered_pipe[2 * line_cnt]);
        close(numbered_pipe[2 * line_cnt + 1]);
    }

    // check if the child process if piped to a numbered pipe
    if(status.is_numpipe != 0){
        int target_line = (line_cnt + abs(status.is_numpipe)) % MAXPIPE;
        for(int i = 0; i < pid_wait[line_cnt].size(); i++){
            pid_wait[target_line].push_back(pid_wait[line_cnt][i]);
        }
        pid_wait[line_cnt].clear();
    }
    // check if reach the end of the line, which needs to wait for the child processes to finish
    else if(!status.pipe_out){
        int waiting;
        for(int i = 0; i < pid_wait[line_cnt].size(); i++){
            waitpid(pid_wait[line_cnt][i], &waiting, 0);
        }
        pid_wait[line_cnt].clear();
    }
}