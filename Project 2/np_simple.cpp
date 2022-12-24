#include "np_simple_function.hpp"

int main(int argc, char* argv[]){
    if(argc != 2){
        cerr << "Usage: ./" << argv[0] << " [port]" <<endl;
    }

    setenv("PATH", "bin:.", 1); // set the default environment variables

    int sockfd, newConnection;
    int flag;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

    if(bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0){
        cerr << "Cannot bind local address!" << endl;
    }

    listen(sockfd, MAXUSR);

    while(1){
        socklen_t clilen = sizeof(cli_addr);
        newConnection = accept(sockfd, (struct sockaddr*) &cli_addr, &clilen);
        
        string prompt = "% ";
        char addr[20];
        inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, addr, sizeof(cli_addr));
        write(newConnection, prompt.c_str(), prompt.length());

        cout << "*** User '" << newConnection  << "' entered from " << string(addr) << ":" << ntohs(cli_addr.sin_port) << ". ***" << endl;
        while(1){
            char input[MAXLEN] = "";
            read(newConnection, input, sizeof(input));
            string msg = string(input);
            vector<string> args = string_parsing(msg);

            if(args[0] == "exit"){
                // Initialize global variables
                line_cnt = 0;
                for(int i = 0; i < MAXPIPE; i++){
                    pid_wait[i].clear();
                    numpiped_to[i] = false;
                }
                for(int i = 0; i < 2*MAXPIPE; i++){
                    numbered_pipe[i] = 0;
                }

                setenv("PATH", "bin:.", 1);
                cout << "*** User '" << newConnection << "' logout. ***" << endl;
                close(newConnection);
                break;
            }
            else{
                shell_operation(args, newConnection);
            }
            write(newConnection, prompt.c_str(), prompt.length());
        }
    }
}