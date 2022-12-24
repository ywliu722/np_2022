#include "definition.hpp"
#include "function.hpp"

int main(){
    setenv("PATH", "bin:.", 1); // set the default environment variables
    signal(SIGCHLD, SIG_IGN);
    while(1){
        string input;
        vector<string> arguments;
        
        cout<<"% "; // prompt
        
        // read and parsing the input
        getline(cin, input);
        if(input.size() == 0) continue; // enter only
        arguments = string_parsing(input);

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
                command_decision(command, pipe_fd, {pipe_exist, IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index});
                
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

                command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, target, !IS_REDIRECT, pipe_index});   // pipe_index is not used in this situation

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
                
                command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, redirect_fd, pipe_index});   // pipe_index is not used in this situation

                // close the opened file and modify the status of the commands
                close(redirect_fd);
                pipe_exist = !IS_PIPE_IN;
                last_command_pos = i + 2;

            }
            
        }

        if(last_command_pos != arguments.size()){
            vector<string> command;
            for(int j = last_command_pos; j < arguments.size(); j++) command.push_back(arguments[j]);  // get the whole command from the input between two pipes
            command_decision(command, pipe_fd, {pipe_exist, !IS_PIPE_OUT, NOT_NUMPIPE, !IS_REDIRECT, pipe_index});   // pipe_index is not used in this situation
        }

        if(!last_cmd_numpipe) line_cnt = (line_cnt + 1) % MAXPIPE;
    }
    return 0;
}