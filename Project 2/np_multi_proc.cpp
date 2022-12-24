#include "np_multi_proc.hpp"
#include "np_multi_proc_function.hpp"

// signal handler
void sigint_handler(int signum){
    cout << "sigint_handler: signal detected!" << endl;
    if(signum == SIGINT){
        cout << "SIGINT caught!" << endl;
        if(shm_unlink("client_info") == -1) cout << "Free client_info failed" << endl;
        if(shm_unlink("broadcast_msg") == -1) cout << "Free broadcast_msg failed" << endl;
        if(shm_unlink("user_pipe_msg") == -1) cout << "Free user_pipe_msg failed" << endl;
        exit(1);
    }
}

// handling signal that someone is piped to this client and the situation that someone is leaving
void userpipe_handler(int signum){
    string msg = string(user_pipe_shm);

    // the user pipe dst is finish the reading
    if(!(msg.find("finish") == string::npos)){
        vector<string> args = string_parsing(msg);
        close(current_user_pipe[args[0]]);
        current_user_pipe.erase(args[0]);

        string path = user_pipe_path + args[0];
        unlink(path.c_str());
    }
    // someone is pipe to this client
    if(msg.find("leave") == string::npos){
        string path = user_pipe_path + msg;
        int user_pipe_fd = open(path.c_str(), O_RDONLY);
        current_user_pipe[msg] = user_pipe_fd;
    }

    // someone is left
    else{
        vector<string> args = string_parsing(msg);        
        stringstream ss;
        ss << setw(2) << setfill('0') << args[0];
        string user_id = ss.str();

        ss.str("");
        ss << setw(2) << setfill('0') << client_id_for_signal;
        string client = ss.str();

        string key1 = client + user_id;
        string key2 = user_id + client;

        // the FIFO os created by this client
        if(current_user_pipe.find(key1) != current_user_pipe.end()){
            close(current_user_pipe[key1]);
            current_user_pipe.erase(key1);

            string path = user_pipe_path + key1;
            unlink(path.c_str());
        }
        // the FIFO is created by others
        if(current_user_pipe.find(key2) != current_user_pipe.end()){
            close(current_user_pipe[key2]);
            current_user_pipe.erase(key2);
        }
    }
}

// handling signal with broadcast messages (including broadcast, yell, tell)
void broadcast_handler(int signum){
    string broadcast_msg = string(broadcast_msg_shm);
    write(sockfd_for_signal, broadcast_msg.c_str(), broadcast_msg.length());
}

void shm_init(){
    // user info shared memory
    int client_info_fd;
    client_info_fd = shm_open("client_info", O_RDWR | O_CREAT, 0777);
    ftruncate(client_info_fd, sizeof(usr_info) * MAXUSR);
    user_info_shm = (usr_info*)mmap(NULL, sizeof(usr_info) * MAXUSR, PROT_READ | PROT_WRITE, MAP_SHARED, client_info_fd, 0);
    if(user_info_shm == NULL) cout << "User Info mmap failed!" << endl;
    close(client_info_fd);
    memset(user_info_shm, 0, sizeof(user_info) * MAXUSR);

    // broadcast msg
    int broadcast_msg_fd;
    broadcast_msg_fd = shm_open("broadcast_msg", O_RDWR | O_CREAT, 0777);
    ftruncate(broadcast_msg_fd, MAXMSGLEN);
    broadcast_msg_shm = (char*)mmap(NULL, MAXMSGLEN, PROT_READ | PROT_WRITE, MAP_SHARED, broadcast_msg_fd, 0);
    if(broadcast_msg_shm == NULL) cout << "Broadcast msg mmap failed!" << endl;
    close(broadcast_msg_fd);
    memset(broadcast_msg_shm, 0, MAXMSGLEN);

    // user pipe msg
    int user_pipe_fd;
    user_pipe_fd = shm_open("user_pipe_msg", O_RDWR | O_CREAT, 0777);
    ftruncate(user_pipe_fd, MAXMSGLEN);
    user_pipe_shm = (char*)mmap(NULL, MAXMSGLEN, PROT_READ | PROT_WRITE, MAP_SHARED, user_pipe_fd, 0);
    if(user_info_shm == NULL) cout << "User pipe msg mmap failed!" << endl;
    close(user_pipe_fd);
    memset(user_info_shm, 0, MAXMSGLEN);
}

void client_process(int sockfd, int client_id){
    // set up signal handler for user pipe opened and broadcast messages
    signal(SIG_USERPIPE, userpipe_handler);
    signal(SIG_BROADCAST, broadcast_handler);

    setenv("PATH", "bin:.", 1); // set the default environment variables

    client_id_for_signal = client_id;
    sockfd_for_signal = sockfd;
    line_cnt = 0;
    pid_wait.clear();
    pid_wait.resize(MAXPIPE);
    numpiped_to.clear();
    numpiped_to.resize(MAXPIPE, false);
    current_user_pipe.clear();

    string prompt = "% ";
    while(1){
        if(user_info_shm[client_id].pid != 0){
            break;
        }
    }
    cout << user_info_shm[client_id].pid << endl;
    while(1){
        // read from the socket
        char input[MAXLEN] = "";
        read(sockfd, input, sizeof(input));
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
        if(rwg_command_decision(args, msg, sockfd, client_id)){
            shell_operation(args, msg, sockfd, client_id);
        }

        // new prompt
        write(sockfd, prompt.c_str(), prompt.length());
    }
}

int main(int argc, char* argv[]){
    if(argc != 2){
        cerr << "Usage: ./" << argv[0] << " [port]" <<endl;
    }

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigint_handler);

    shm_init();

    int master_socket, newConnection;
    int flag;
    int ready_sockfd; // how many sockfd is ready to be read decided by select()
    struct sockaddr_in serv_addr, cli_addr;

    master_socket = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
    setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

    if(bind(master_socket, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0){
        cerr << "Cannot bind local address!" << endl;
    }

    listen(master_socket, MAXUSR);


    while(1){
        int new_client_id;
        pid_t child_pid;
        socklen_t clilen = sizeof(cli_addr);
        newConnection = accept(master_socket, (struct sockaddr*) &cli_addr, &clilen);
        if(newConnection < 0){
            cout << "Connected Failed" << endl;
        }
        // assign a client_id to new client
        for(new_client_id = 1; new_client_id < MAXUSR; new_client_id++){
            if(user_info_shm[new_client_id].port == 0){
                break;
            }
        }

        // record the information of the new client
        usr_info new_client_info;
        string default_name = "(no name)";
        memset(new_client_info.username,'\0', sizeof(new_client_info.username));
        memcpy(new_client_info.username, default_name.c_str(), default_name.length());
        inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, new_client_info.ip_address, sizeof(cli_addr));
        new_client_info.port = ntohs(cli_addr.sin_port);

        string broadcast_msg = "*** User '" + string(new_client_info.username) + "' entered from " + string(new_client_info.ip_address) + ":" + to_string(new_client_info.port) + ". ***\n";
        string new_login_msg = welcome_msg + broadcast_msg + "% ";
        write(newConnection, new_login_msg.c_str(), new_login_msg.length());

        // broadcast to other clients
        memset(broadcast_msg_shm, 0, MAXMSGLEN);
        memcpy(broadcast_msg_shm, broadcast_msg.c_str(), broadcast_msg.length());
        for(int i = 1; i < MAXUSR; i++){
            if(user_info_shm[i].pid != 0){
                kill(user_info_shm[i].pid, SIG_BROADCAST);
                usleep(2 * 1000);
            }
        }

        while(1){
            child_pid = fork();
            if(child_pid != -1) break;
            cout << "Child fork error!" << endl;
        }

        // child process
        if(child_pid == 0){
            client_process(newConnection, new_client_id);
            exit(0);
        }

        // master process
        else{
            new_client_info.pid = child_pid;
            memcpy(&user_info_shm[new_client_id], &new_client_info, sizeof(usr_info));
            cout << user_info_shm[new_client_id].username << " " << user_info_shm[new_client_id].ip_address << " " << user_info_shm[new_client_id].port << endl;
            close(newConnection);
        }
    }
    return 0;
}