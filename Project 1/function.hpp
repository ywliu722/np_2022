vector<string> string_parsing(string input){
    // parsing the input with stringstream
    string arg;
    vector<string> ret;
    stringstream ss(input);
    while(ss >> arg) ret.push_back(arg);
    return ret;
}

void buildin_setenv(vector<string> args){
    if(args.size() != 3){
        cout<< "Usage: setenv [var] [value]" << endl;
        return;
    }
    setenv(args[1].c_str(), args[2].c_str(), 1);
}

void buildin_printenv(vector<string> args){
    if(args.size() != 2){
        cout<< "Usage: printenv [var]" << endl;
        return;
    }
    char* ret = getenv(args[1].c_str());
    if(ret) cout << ret <<endl;
}

void outside_command(vector<string> command, int* pipe_fd, cmd_status status){
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

void command_decision(vector<string> command, int* pipe_fd, cmd_status status){
    if(command[0] == "setenv") buildin_setenv(command);
    else if(command[0] == "printenv") buildin_printenv(command);
    else if(command[0] == "exit") exit(0);
    else outside_command(command, pipe_fd, status);
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
        close(numbered_pipe[2 * line_cnt] + 1);
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