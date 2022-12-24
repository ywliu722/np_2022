int rwg_command_decision(vector<string> args, string input, int sockfd, int client_id){
    if(args[0] == "who")
        rwg_who(args, sockfd, client_id);
    else if(args[0] == "tell")
        rwg_tell(args, input, sockfd, client_id);
    else if(args[0] == "yell")
        rwg_yell(args, input, sockfd, client_id);
    else if(args[0] == "name")
        rwg_name(args, sockfd, client_id);
    else if(args[0] == "exit")
        rwg_exit(args, sockfd, client_id);
    else
        return 1;
    return 0;
}

void rwg_who(vector<string> args, int sockfd, int client_id){
    string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    for(int i = 1; i < MAXUSR; i++){
        if(client_fd[i] > 0){
            msg = msg + to_string(i) + "\t" + username[i] + "\t" + ip_address[i] + ":" + to_string(port[i]);
            if(i == client_id)
                msg = msg + "\t<-me";
            msg = msg + "\n";
        }
    }
    write(sockfd, msg.c_str(), msg.length());
}

void rwg_tell(vector<string> args, string input, int sockfd, int client_id){
    int msg_position = args[0].length() + args[1].length() + 2;
    string tell_msg = input.substr(msg_position, input.length() - msg_position);
    
    for(int i = 0; i < args[1].length(); i++){
        if(args[1][i] < '0' || args[1][i] > '9'){
            string errmsg = "Usage: tell <user id> <message>\n";
            write(sockfd, errmsg.c_str(), errmsg.length());
            return;
        }
    }

    int target_id = stoi(args[1]);
    if(client_fd[target_id] == -1){
        string errmsg = "*** Error: user #" + to_string(target_id) + " does not exist yet. ***\n";
        write(sockfd, errmsg.c_str(), errmsg.length());
        return;
    }

    string msg = "*** " + username[client_id] + " told you ***: " + tell_msg + "\n";
    write(client_fd[target_id], msg.c_str(), msg.length());
}

void rwg_yell(vector<string> args, string input, int sockfd, int client_id){
    int msg_position = args[0].length() + 1;
    string yell_msg = input.substr(msg_position, input.length() - msg_position);
    string msg = "*** " + username[client_id] + " yelled ***: " + yell_msg + "\n";
    
    for(int i = 1; i < MAXUSR; i++){
        if(client_fd[i] >= 0){
            write(client_fd[i], msg.c_str(), msg.length());
        }
    }
}

void rwg_name(vector<string> args, int sockfd, int client_id){
    string newname = args[1];
    for(int i = 1; i < MAXUSR; i++){
        if(username[i] == newname){
            string errmsg = "*** User '" + newname + "' already exists. ***\n";
            write(sockfd, errmsg.c_str(), errmsg.length());
            return;
        }
    }

    username[client_id] = newname;
    string msg = "*** User from " + ip_address[client_id] + ":" + to_string(port[client_id]) + " is named '" + newname + "'. ***\n";
    for(int i = 1; i < MAXUSR; i++){
        if(client_fd[i] >= 0){
            write(client_fd[i], msg.c_str(), msg.length());
        }
    }
}

void rwg_exit(vector<string> args, int sockfd, int client_id){
    string msg = "*** User '" + username[client_id] + "' left. ***\n";
    for(int i = 1; i < MAXUSR; i++){
        if(client_fd[i] >= 0 && i != client_id){
            write(client_fd[i], msg.c_str(), msg.length());
        }
    }

    close(sockfd);
    FD_CLR(sockfd, &all_fd);
    username[client_id] = "(no name)";
    ip_address[client_id] = "";
    port[client_id] = 0;
    client_fd[client_id] = -1;

    user_env[client_id].clear();
    user_env[client_id]["PATH"] = "bin:.";
    
    // Clean user pipe
    for(int i = 1; i < MAXUSR; i++){
        if(client_fd[i] >= 0 && i != client_id){
            stringstream ss;
            ss << setw(2) << setfill('0') << i;
            string user_id = ss.str();

            ss.str("");
            ss << setw(2) << setfill('0') << client_id;
            string client = ss.str();

            string key1 = client + user_id;
            string key2 = user_id + client;
            cout << key1 << " " << key2 << endl;
            // the FIFO os created by this client
            if(current_user_pipe.find(key1) != current_user_pipe.end()){
                current_user_pipe.erase(key1);
            }
            // the FIFO is created by others
            if(current_user_pipe.find(key2) != current_user_pipe.end()){
                current_user_pipe.erase(key2);
            }
        }
    }

    line_cnt[client_id] = 0;
    for(int i = 0; i < MAXPIPE * 2; i++){
        numbered_pipe[client_id][i] = 0;
    }
    for(int i = 0; i < MAXPIPE; i++){
        pid_wait[client_id][i].clear();
        numpiped_to[client_id][i] = false;
    }

}

int target_user_decision(string argument, int client_id){

    int target_user = stoi(&argument[1]);
    for(int i = 1; i < argument.length(); i++){
        if(argument[i] < '0' || argument[i] > '9'){
            target_user = 0;
            return 0;
        }
    }
    if(target_user >= MAXUSR) return target_user * -1;
    if(target_user){
        // check if target user is exist
        if(client_fd[target_user] < 0){
            cout << "User does not exist " << target_user << endl;
            return -1 * target_user;
        }

        // check if the pipe is exist or not
        // form the user pipe key
        stringstream ss;
        ss << setw(2) << setfill('0') << target_user;
        string target = ss.str();

        ss.str("");
        ss << setw(2) << setfill('0') << client_id;
        string client = ss.str();

        string user_pipe_key;
        if(argument[0] == '<')  user_pipe_key = target + client;
        else user_pipe_key = client + target;
        
        // user pipe in
        if(argument[0] == '<'){
            // the pipe is exist
            if(current_user_pipe.find(user_pipe_key) != current_user_pipe.end())
                return target_user;
            // the pipe is not exist yet
            else
                return target_user * -1;
        }
        // user pipe out
        else{
            // the pipe is not exist yet
            if(current_user_pipe.find(user_pipe_key) == current_user_pipe.end()){
                user_pipe new_pipe;
                pipe(new_pipe.user_pipe);
                current_user_pipe[user_pipe_key] = new_pipe;
                return target_user;
            }
            // the pipe is exist
            else
                return target_user * -1;
        }
    }
    return 0;
}

void user_pipe_in_broadcast(int target_user_in, int sockfd, int client_id, string input){
    if(target_user_in > 0){
        string msg = "*** " + username[client_id] + " (#" + to_string(client_id) + ") just received from " + username[target_user_in] + " (#" + to_string(target_user_in) + ") by \'" + input + "' ***\n";
        for(int i = 1; i < MAXUSR; i++){
            if(client_fd[i] > 0)
                write(client_fd[i], msg.c_str(), msg.length());
        }
    }
    else if(target_user_in < 0){
        int src = -1 * target_user_in;
        string errmsg;
        // the client is not exist
        if(client_fd[src] < 0 || src >= MAXUSR)
            errmsg = "*** Error: user #" + to_string(src) + " does not exist yet. ***\n";
        // the pipe is not exist
        else
            errmsg = "*** Error: the pipe #" + to_string(src) + "->#" + to_string(client_id) + " does not exist yet. ***\n";
        write(sockfd, errmsg.c_str(), errmsg.length());
    }
}

void user_pipe_out_broadcast(int target_user_out, int sockfd, int client_id, string input){
    if(target_user_out){
        if(target_user_out > 0){
            // return msg
            string msg = "*** " + username[client_id] + " (#" + to_string(client_id) + ") just piped '" + input + "' to " + username[target_user_out] + " (#" + to_string(target_user_out) + ") ***\n";
            for(int i = 1; i < MAXUSR; i++){
                if(client_fd[i] > 0)
                    write(client_fd[i], msg.c_str(), msg.length());
            }
        }
        else if(target_user_out < 0){
            int dst = -1 * target_user_out;
            string errmsg;
            if(client_fd[dst] < 0 || dst >= MAXUSR)
                errmsg = "*** Error: user #" + to_string(dst) + " does not exist yet. ***\n";
            else
                errmsg = "*** Error: the pipe #" + to_string(client_id) + "->#" + to_string(dst) + " already exists. ***\n";
            write(sockfd, errmsg.c_str(), errmsg.length());
        }
    }
}

void shell_operation(vector<string> arguments, string input, int sockfd, int client_id){
    
    signal(SIGCHLD, SIG_IGN);

    bool pipe_exist = !IS_PIPE_IN;  // used to identify if the current command's input is piped by the previous command
    bool last_cmd_numpipe = false;  // used to identify if the numbered pipe added the line_cnt
    int last_command_pos = 0, pipe_index = 0;   // identify the starting index of the current command and the index of pipe_pair for the current command
    int pipe_fd[MAXPIPE*2]; // [read_fd, write_fd]
    int target_user_in = 0, target_user_out = 0;
    int arguments_size = arguments.size();

    // environment variables setup
    map<string, string>::iterator iter;
    for(iter = user_env[client_id].begin(); iter != user_env[client_id].end(); iter++){
        setenv(iter->first.c_str(), iter->second.c_str(), 1);
    }

    for(int i = 0; i < arguments_size; i++){
        last_cmd_numpipe = false;
        target_user_in = 0, target_user_out = 0;

        // handling user pipe
        if( (arguments[i][0] == '>' || arguments[i][0] == '<') && arguments[i].size() > 1){
            // handling user_pipe_in and user_pipe_out in same command
            if( (i + 1 < arguments_size) && (arguments[i+1][0] == '>' || arguments[i+1][0] == '<') && arguments[i].size() > 1){
                // handling input fist, then output
                if(arguments[i][0] == '<'){
                    target_user_in = target_user_decision(arguments[i], client_id);
                    target_user_out = target_user_decision(arguments[i+1], client_id);
                }
                // handling output first, then input
                else{
                    target_user_out = target_user_decision(arguments[i], client_id);
                    target_user_in = target_user_decision(arguments[i+1], client_id);
                }

                // call command decision directly
                vector<string> command;
                for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes

                user_pipe_in_broadcast(target_user_in, sockfd, client_id, input);
                user_pipe_out_broadcast(target_user_out, sockfd, client_id, input);
                command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index, target_user_in, target_user_out}, sockfd, client_id);

                last_command_pos = i + 2;
                i++;
            }
            // only user_pipe_in or user_pipe_out
            else{
                // decide the target user of the pipe
                if(arguments[i][0] == '<'){
                    target_user_in = target_user_decision(arguments[i], client_id);
                }
                else{
                    target_user_out = target_user_decision(arguments[i], client_id);
                }
                // remove the pipe from the arguments
                arguments.erase(arguments.begin() + i);
                arguments_size--;
            }
        }


        // handling ordinary pipe
        if(arguments[i] == "|"){
            vector<string> command;
            for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
            pipe(pipe_fd + pipe_index * 2);

            user_pipe_in_broadcast(target_user_in, sockfd, client_id, input);
            user_pipe_out_broadcast(target_user_out, sockfd, client_id, input);
            command_decision(command, pipe_fd, {pipe_exist, IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index, target_user_in, target_user_out}, sockfd, client_id);
            
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
                int target_line = (line_cnt[client_id] + abs(target)) % MAXPIPE;
                numpiped_to[client_id][target_line] = true;
                // check if there is any command piped to the same target
                if(pid_wait[client_id][target_line].size() == 0){
                    pipe(numbered_pipe[client_id] + target_line * 2);
                }
            }

            vector<string> command;
            for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes

            user_pipe_in_broadcast(target_user_in, sockfd, client_id, input);
            user_pipe_out_broadcast(target_user_out, sockfd, client_id, input);
            command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, target, !IS_REDIRECT, pipe_index, target_user_in, target_user_out}, sockfd, client_id);   // pipe_index is not used in this situation

            // modify the status of the commands
            last_cmd_numpipe = true;
            pipe_exist = !IS_PIPE_IN;
            last_command_pos = i + 1;
            line_cnt[client_id] = (line_cnt[client_id] + 1) % MAXPIPE;
        }

        // handling redirection
        else if(arguments[i] == ">"){
            // open target file and check if it is opened successfully
            if( i + 1 >= arguments_size){
                string errmsg = "Usage: [command] > [filename]\n";
                write(sockfd, errmsg.c_str(), errmsg.length());
                continue;
            }
            int redirect_fd = open(arguments[i+1].c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);

            vector<string> command;
            for(int j = last_command_pos; j < i; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
            
            user_pipe_in_broadcast(target_user_in, sockfd, client_id, input);
            user_pipe_out_broadcast(target_user_out, sockfd, client_id, input);
            command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, redirect_fd, pipe_index, target_user_in, target_user_out}, sockfd, client_id);   // pipe_index is not used in this situation

            // close the opened file and modify the status of the commands
            close(redirect_fd);
            pipe_exist = !IS_PIPE_IN;
            last_command_pos = i + 2;

        }
            
    }

    if(last_command_pos != arguments_size){
        vector<string> command;
        for(int j = last_command_pos; j < arguments_size; j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
        
        user_pipe_in_broadcast(target_user_in, sockfd, client_id, input);
        user_pipe_out_broadcast(target_user_out, sockfd, client_id, input);
        command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index, target_user_in, target_user_out}, sockfd, client_id);   // pipe_index is not used in this situation
    }

    if(!last_cmd_numpipe) line_cnt[client_id] = (line_cnt[client_id] + 1) % MAXPIPE;
    // change the environment variables back
    for(iter = user_env[client_id].begin(); iter != user_env[client_id].end(); iter ++){
        setenv(iter->first.c_str(), "", 1);
    }
}

vector<string> string_parsing(string input){
    // parsing the input with stringstream
    string arg;
    vector<string> ret;
    stringstream ss(input);
    while(ss >> arg) ret.push_back(arg);
    return ret;
}

void buildin_setenv(vector<string> args, int sockfd, int client_id){
    if(args.size() != 3){
        string errmsg = "Usage: setenv [var] [value]\n";
        write(sockfd, errmsg.c_str(), errmsg.length());
        return;
    }
    user_env[client_id][args[1]] = args[2];
}

void buildin_printenv(vector<string> args, int sockfd, int client_id){
    if(args.size() != 2){
        string errmsg = "Usage: printenv [var]\n";
        write(sockfd, errmsg.c_str(), errmsg.length());
        return;
    }
    char* ret = getenv(args[1].c_str());
    if(ret){
        string ret_msg = string(ret);
        if(ret_msg != ""){
            string msg = user_env[client_id][args[1]] + "\n";
            write(sockfd, msg.c_str(), msg.length());
        }
    }
}

void outside_command(vector<string> command, int* pipe_fd, cmd_status status, int sockfd, int client_id){
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
        child_pipe_process(pipe_fd, status, client_id);
        if(execvp(arguments[0], arguments) == -1) cerr << "Unknown command: [" << arguments[0] << "]." <<endl;
        exit(0);
    }

    // parent process
    else{
        parent_pipe_process(pipe_fd, status, pid, client_id);
    }

    // deallocate the char* array
    for(int i = 0; i < command.size() - 1; i++){
        delete [] arguments[i];
    }
    delete [] arguments;
}

void command_decision(vector<string> command, int* pipe_fd, cmd_status status, int sockfd, int client_id){
    if(command[0] == "setenv") buildin_setenv(command, sockfd, client_id);
    else if(command[0] == "printenv") buildin_printenv(command, sockfd, client_id);
    else outside_command(command, pipe_fd, status, sockfd, client_id);
}

void child_pipe_process(int* pipe_fd, cmd_status status, int client_id){
    // input pipe process
    // check if the child process get the input from the user pipe
    if(status.user_pipe_in > 0){
        stringstream ss;
        ss << setw(2) << setfill('0') << status.user_pipe_in;
        ss << setw(2) << setfill('0') << client_id;
        string user_pipe_key = ss.str();

        close(current_user_pipe[user_pipe_key].user_pipe[1]);
        dup2(current_user_pipe[user_pipe_key].user_pipe[0], STDIN_FILENO);
        close(current_user_pipe[user_pipe_key].user_pipe[0]);
    }
    else if(status.user_pipe_in < 0){
        dup2(dev_null_input, STDIN_FILENO); // dup the stdin to /dev/null
    }
    // check if the child process get the input from the previous one via pipe
    else if(status.pipe_in){
        int previous_pipe;
        if(status.pipe_idx - 1 < 0) previous_pipe = (status.pipe_idx - 1) + MAXPIPE;
        else previous_pipe = status.pipe_idx - 1;

        close(pipe_fd[2 * previous_pipe + 1]); // write fd
        dup2(pipe_fd[2 * previous_pipe], STDIN_FILENO); // dup the stdin to the pipe
        close(pipe_fd[2 * previous_pipe]); // read fd
    }
    // check if the child process get the input from the numbered pipe
    // check > 1 because we just put the current child into the pid waiting queue
    else if(numpiped_to[client_id][line_cnt[client_id]]){
        close(numbered_pipe[client_id][2 * line_cnt[client_id] + 1]); // write fd
        dup2(numbered_pipe[client_id][2 * line_cnt[client_id]], STDIN_FILENO); // dup the stdin to the pipe
        close(numbered_pipe[client_id][2 * line_cnt[client_id]]); // read fd
    }

    // ouput pipe process
    // check if the child process is output to the user pipe
    if(status.user_pipe_out > 0){
        stringstream ss;
        ss << setw(2) << setfill('0') << client_id;
        ss << setw(2) << setfill('0') << status.user_pipe_out;
        string user_pipe_key = ss.str();

        close(current_user_pipe[user_pipe_key].user_pipe[0]);
        dup2(current_user_pipe[user_pipe_key].user_pipe[1], STDOUT_FILENO);
        close(current_user_pipe[user_pipe_key].user_pipe[1]);
    }
    else if(status.user_pipe_out < 0){
        dup2(dev_null_output, STDOUT_FILENO); // dup the stdout to /dev/null
    }
    // check if the child process is output to an ordinary pipe
    else if(status.pipe_out){
        close(pipe_fd[2 * status.pipe_idx]); // read fd
        dup2(pipe_fd[2 * status.pipe_idx + 1], STDOUT_FILENO); // dup the stdout to the pipe
        close(pipe_fd[2 * status.pipe_idx + 1]); // write fd
    }
    // check if the child process is output to a numbered pipe
    else if(status.is_numpipe != 0){
        int target_line = (line_cnt[client_id] + abs(status.is_numpipe)) % MAXPIPE;
        close(numbered_pipe[client_id][2 * target_line]); // read fd
        dup2(numbered_pipe[client_id][2 * target_line + 1], STDOUT_FILENO); // dup the stdout to the pipe
        if(status.is_numpipe < 0) dup2(numbered_pipe[client_id][2 * target_line + 1], STDERR_FILENO); // dup the stderr to the pipe
        close(numbered_pipe[client_id][2 * target_line + 1]); // write fd
        numpiped_to[client_id][line_cnt[client_id]] = false;
    }
    // check if the child process is output to a file
    else if(status.is_redirect != 0){
        dup2(status.is_redirect, STDOUT_FILENO);
    }
}

void parent_pipe_process(int* pipe_fd, cmd_status status, pid_t pid, int client_id){
    // put the child process pid into the waiting queue
    pid_wait[client_id][line_cnt[client_id]].push_back(pid);

    // pipe process
    // check if the child process get the input from the user pipe
    if(status.user_pipe_in > 0){
        stringstream ss;
        ss << setw(2) << setfill('0') << status.user_pipe_in;
        ss << setw(2) << setfill('0') << client_id;
        string user_pipe_key = ss.str();
        close(current_user_pipe[user_pipe_key].user_pipe[0]);
        close(current_user_pipe[user_pipe_key].user_pipe[1]);
        current_user_pipe.erase(user_pipe_key);
    }
    // check if the child process get the input from the previous one via pipe
    else if(status.pipe_in){
        int previous_pipe;
        if(status.pipe_idx - 1 < 0) previous_pipe = (status.pipe_idx - 1) + MAXPIPE;
        else previous_pipe = status.pipe_idx - 1;

        close(pipe_fd[2 * previous_pipe]);
        close(pipe_fd[2 * previous_pipe + 1]);
    }
    // check if the child process get the input from the numbered pipe
    else if(numpiped_to[client_id][line_cnt[client_id]]){
        close(numbered_pipe[client_id][2 * line_cnt[client_id]]);
        close(numbered_pipe[client_id][2 * line_cnt[client_id] + 1]);
    }

    // check if the child process if piped to a numbered pipe
    if(status.is_numpipe != 0){
        int target_line = (line_cnt[client_id] + abs(status.is_numpipe)) % MAXPIPE;
        for(int i = 0; i < pid_wait[client_id][line_cnt[client_id]].size(); i++){
            pid_wait[client_id][target_line].push_back(pid_wait[client_id][line_cnt[client_id]][i]);
        }
        pid_wait[client_id][line_cnt[client_id]].clear();
    }
    // check if reach the end of the line, which needs to wait for the child processes to finish
    else if(!status.pipe_out && !(status.user_pipe_out > 0)){
        int waiting;
        for(int i = 0; i < pid_wait[client_id][line_cnt[client_id]].size(); i++){
            waitpid(pid_wait[client_id][line_cnt[client_id]][i], &waiting, 0);
        }
        pid_wait[client_id][line_cnt[client_id]].clear();
    }
}