#include "np_single_proc.hpp"
#include "np_single_proc_function.hpp"

int main(int argc, char* argv[]){
    if(argc != 2){
        cerr << "Usage: ./" << argv[0] << " [port]" <<endl;
    }

    int sockfd, newConnection;
    int flag;
    int ready_sockfd; // how many sockfd is ready to be read decided by select()
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

    int max_fd = sockfd, max_index = -1;
    // Initialize user_env
    for(int i = 0; i < MAXUSR; i++){
        user_env[i]["PATH"] = "bin:.";
    }

    // Initialize fd_sets
    FD_ZERO(&all_fd);
    FD_SET(sockfd, &all_fd);

    while(1){
        selected_fd = all_fd;
        ready_sockfd = select(max_fd + 1, &selected_fd, NULL, NULL, NULL);
        if(ready_sockfd < 0){
            cout << "Select error" << endl;
            continue;
        }
        socklen_t clilen = sizeof(cli_addr);

        // if there are new connections (check if listened socket is in the selected set)
        if(FD_ISSET(sockfd, &selected_fd)){
            newConnection = accept(sockfd, (struct sockaddr*) &cli_addr, &clilen);
            if(newConnection < 0){
                cout << "Connected Failed" << endl;
            }
            else{
                int client_id;
                for(client_id = 1; client_id < MAXUSR; client_id++){
                    if(client_fd[client_id] < 0) break;
                }
                if(client_id == MAXUSR) cout << "Too many users" <<endl;
                else{
                    char addr[20];

                    inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, addr, sizeof(cli_addr));
                    client_fd[client_id] = newConnection;
                    port[client_id] = ntohs(cli_addr.sin_port);
                    ip_address[client_id] = string(addr);

                    string broadcast_msg = "*** User '" + username[client_id] + "' entered from " + ip_address[client_id] + ":" + to_string(port[client_id]) + ". ***\n";
                    for(int i = 1; i < MAXUSR; i++){
                        // new login user
                        if(i == client_id){
                            string new_login_msg = welcome_msg + broadcast_msg + "% ";
                            write(client_fd[i], new_login_msg.c_str(), new_login_msg.length());
                        }
                        // existing users
                        else{
                            write(client_fd[i], broadcast_msg.c_str(), broadcast_msg.length());
                        }
                    }
                    cout << "*** User '" << username[client_id]  << "' entered from " << ip_address[client_id] << ":" << port[client_id] << ". ***" << endl;
                }
                FD_SET(newConnection, &all_fd);
                if(newConnection > max_fd) max_fd = newConnection;
                if(client_id > max_index) max_index = client_id;

                // if there is no more socket is selected, then back to select()
                ready_sockfd--;
                if(ready_sockfd <= 0) continue;
            }
        }

        string prompt = "% ";
        // check if the clients have msg sent
        for(int i = 1; i <= max_index; i++){
            int client_socket = client_fd[i];
            if(client_socket < 0) continue;
            if(FD_ISSET(client_socket, &selected_fd)){
                // read from the socket
                char input[MAXLEN] = "";
                read(client_socket, input, sizeof(input));
                string msg = string(input);
                vector<string> args = string_parsing(msg);

                // remove return and new line in the string
                for(int j = 0; j < msg.length(); j++){
                    if(msg[j] == '\r' || msg[j] == '\n'){
                        msg.erase(msg.begin() + j);
                        j--;
                    }
                }

                if(args.size() == 0) continue;
                if(rwg_command_decision(args, msg, client_socket, i)){
                    shell_operation(args, msg, client_socket, i);
                }

                // new prompt
                write(client_socket, prompt.c_str(), prompt.length());

                // if there is no more socket is selected, then back to select()
                ready_sockfd--;
                if(ready_sockfd <= 0) break;
            }
        }
    }
}