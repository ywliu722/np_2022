#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <boost/asio.hpp>
#include <regex>

#define MAXLEN 20000
#define REPLY_LEN 9

using namespace std;
using boost::asio::ip::tcp;

boost::asio::io_context io_context;

typedef struct socks_request_info{
    string src_ip;
    string src_port;
    string dst_ip;
    string dst_port;
    string command;
    string reply;
}socks_request;

class socks_session : public std::enable_shared_from_this<socks_session>{
public:
    socks_session(tcp::socket socket) : client_socket(move(socket)), server_socket(io_context), host_resolver(io_context){}
    void start(){do_read();}
private:
    void do_read(){
        auto self(shared_from_this());
        memset(buf, 0, MAXLEN);
        auto len = client_socket.read_some(boost::asio::buffer(buf, MAXLEN));
        socks_parsing();
        SOCK4A_handle(len);
        check_firewall();
        print_info();
        if(socks_info.command == "CONNECT") do_connect();
        else do_bind();
    }

    void socks_parsing(){
        // get src IP and port
        socks_info.src_ip = client_socket.remote_endpoint().address().to_string();
        socks_info.src_port = to_string(client_socket.remote_endpoint().port());
        
        // reject SOCKS 5 request
        socks_info.reply = "";
        if(buf[0] == 5) socks_info.reply = "Reject";

        // parsing SOCKS CD
        if(buf[1] == 1) socks_info.command = "CONNECT";
        else socks_info.command = "BIND";

        // parsing SOCKS DSTPORT and DSTIP
        unsigned int dst_port = buf[2] * 256 + buf[3];
        socks_info.dst_port = to_string(dst_port);
        socks_info.dst_ip = to_string(buf[4]) + "." + to_string(buf[5]) + "." + to_string(buf[6]) + "." + to_string(buf[7]);
    }

    void SOCK4A_handle(int len){
        if(socks_info.dst_ip.find("0.0.0.") != string::npos && socks_info.dst_ip.find("0.0.0.0") == string::npos){
            int index = 8;
            while(buf[index++] != 0);
            string domain_name = "";
            for(int i = index; i < len - 1; i++){
                domain_name += buf[i];
            }

            boost::system::error_code ec;
            auto result = host_resolver.resolve(domain_name, socks_info.dst_port);
            for(auto it = result.cbegin(); it != result.cend(); it++){
                auto endpoint = *it;
                if(it->endpoint().address().is_v4()){
                    socks_info.dst_ip = endpoint.endpoint().address().to_string();
                    break;
                }
            }
        }
    }

    void check_firewall(){
        ifstream firewall_conf("socks.conf");
        string command, line;
        if(socks_info.command == "CONNECT") command = "c";
        else command = "b";
        while(getline(firewall_conf, line)){
            string tmp;
            stringstream ss(line);
            ss >> tmp;  // permit
            ss >> tmp;  // b or c
            if(tmp != command) continue;

            ss >> tmp;  // ip address
            
            // replace '*' and '.' into regular expression form
            string regex_pattern;
            for(size_t i = 0; i < tmp.length(); i++){
                if(tmp[i] == '*'){
                    regex_pattern += "(.*)";
                }
                else if(tmp[i] == '.'){
                    regex_pattern += "\\.";
                }
                else{
                    regex_pattern += tmp[i];
                }
            }

            // chech if the dst ip is match the firewall rule or not
            if(regex_match(socks_info.dst_ip, regex(regex_pattern))){
                socks_info.reply = "Accept";
                return;
            }
        }
        socks_info.reply = "Reject";
    }

    void print_info(){
        cout << "<S_IP>: " << socks_info.src_ip << endl;
        cout << "<S_PORT>: " << socks_info.src_port << endl;
        cout << "<D_IP>: " << socks_info.dst_ip << endl;
        cout << "<D_PORT>: " << socks_info.dst_port << endl;
        cout << "<Command>: " << socks_info.command << endl;
        cout << "<Reply>: " << socks_info.reply << endl;
    }

    void do_connect(){
        unsigned char reply[REPLY_LEN];
        memset(reply, 0, REPLY_LEN);
        if(socks_info.reply == "Accept") reply[1] = 90;
        else reply[1] = 91;
        client_socket.send(boost::asio::buffer(reply, REPLY_LEN - 1));

        if(socks_info.reply == "Reject"){
            client_socket.close();
            exit(1);
        }
        
        tcp::endpoint dst_endpoint(boost::asio::ip::address::from_string(socks_info.dst_ip), stoi(socks_info.dst_port));
        boost::system::error_code ec;
        server_socket.connect(dst_endpoint);

        client_to_server();
        server_to_client();
    }

    void do_bind(){
        tcp::acceptor server_accepter(io_context, tcp::endpoint(tcp::v4(), 0));
        
        send_reply(server_accepter.local_endpoint().port());
        server_accepter.accept(server_socket);
        send_reply(server_accepter.local_endpoint().port());

        client_to_server();
        server_to_client();
    }

    void send_reply(int port){
        unsigned char reply[REPLY_LEN];
        memset(reply, 0, REPLY_LEN);
        if(socks_info.reply == "Accept") reply[1] = 90;
        else reply[1] = 91;
        reply[2] = port / 256;
        reply[3] = port % 256;
        client_socket.write_some(boost::asio::buffer(reply, REPLY_LEN - 1));
    }

    void client_to_server(){
        auto self(shared_from_this());
        auto buffer = make_shared<array<char, MAXLEN>>();
        buffer->fill('\0');

        client_socket.async_read_some(boost::asio::buffer(*buffer, MAXLEN - 1), 
        [this, self, buffer](boost::system::error_code ec, size_t length){
            
            if(!ec){
                server_socket.write_some(boost::asio::buffer(*buffer, length));
                client_to_server();
            }
            else if(ec == boost::asio::error::eof){
                client_socket.close();
                server_socket.close();
                exit(1);
            }
        });
    }

    void server_to_client(){
        auto self(shared_from_this());
        auto buffer = make_shared<array<char, MAXLEN>>();
        buffer->fill('\0');

        server_socket.async_read_some(boost::asio::buffer(*buffer, MAXLEN - 1), 
        [this, self, buffer](boost::system::error_code ec, size_t length){
            if(!ec){
                client_socket.send(boost::asio::buffer(*buffer, length));
                server_to_client();
            }
            else if(ec == boost::asio::error::eof){
                server_socket.close();
                client_socket.close();
                exit(1);
            }
        });
    }

    tcp::socket client_socket{io_context};
    tcp::socket server_socket{io_context};
    tcp::resolver host_resolver{io_context};
    unsigned char buf[MAXLEN];
    socks_request socks_info;
};

class socks_server : public std::enable_shared_from_this<socks_server>{
public:
    socks_server(short port) : tcp_acceptor(io_context, tcp::endpoint(tcp::v4(), port)){
        tcp::acceptor::reuse_address option(true);
        tcp_acceptor.set_option(option);
        do_accept();
    }
private:
    void do_accept(){
        tcp_acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket){
            if(!ec){
                io_context.notify_fork(io_context.fork_prepare);
                if(fork() == 0){
                    io_context.notify_fork(io_context.fork_child);
                    make_shared<socks_session>(move(socket))->start();
                }
                else{
                    io_context.notify_fork(io_context.fork_parent);
                    socket.close();
                    do_accept();
                }
            }
        });
    }
    tcp::acceptor tcp_acceptor;
};

int main(int argc, char* argv[]){
    try{
        if(argc != 2){
            cerr << "./socks_server [port]" << endl;
            return 1;
        }

        socks_server server(atoi(argv[1]));
        io_context.run();
    }
    catch(exception &e){
        //cerr << "Exception: " << e.what() << "\n";
    }
}