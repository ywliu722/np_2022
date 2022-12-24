#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <string>
#include <sstream>

#define MAXLEN 1024

using namespace std;
using boost::asio::ip::tcp;

typedef struct HTTP_Request{
    string request_method;
    string request_uri;
    string query_string;
    string server_protocol;
    string http_host;
    string server_addr;
    string server_port;
    string remote_addr;
    string remote_port;
    string cgi_filename;
}http_request;

class http_session : public std::enable_shared_from_this<http_session>{
public:
    http_session(tcp::socket socket) : tcp_socket(move(socket)){}
    void start(){do_read();}
private:
    void do_read(){
        auto self(shared_from_this());
        tcp_socket.async_read_some(boost::asio::buffer(buf, MAXLEN),
        [this, self](boost::system::error_code ec, size_t length){
            if(!ec){
                cout << "--------------------------------" << endl;
                http_request request_parameters;

                string start_line, host, tmp_str;
                string request_str = string(buf);
                stringstream ss(request_str);
                
                // get the first 2 lines of the HTTP request
                getline(ss, start_line, '\n');
                getline(ss, host, '\n');
                
                // parsing start line
                ss.str(start_line);
                ss >> tmp_str; // request_method
                request_parameters.request_method = tmp_str;
                cout << "request_method: " << request_parameters.request_method << endl;

                ss >> tmp_str; // request uri
                request_parameters.request_uri = tmp_str;
                int delimeter_pos = tmp_str.find('?');
                request_parameters.cgi_filename = "./" + tmp_str.substr(1, tmp_str.find('?') - 1);
                request_parameters.query_string = tmp_str.substr(delimeter_pos + 1, tmp_str.length() - delimeter_pos);
                cout << "request_uri: " << request_parameters.request_uri << endl;
                cout << "cgi_filename: " << request_parameters.cgi_filename << endl;
                cout << "query_string: " << request_parameters.query_string << endl;

                ss >> tmp_str;
                request_parameters.server_protocol = tmp_str;
                cout << "server_protocol: " << request_parameters.server_protocol << endl;

                // parsing host
                ss.str(host);
                ss >> tmp_str;
                ss >> tmp_str;
                request_parameters.http_host = tmp_str;
                cout << "http_host: " << request_parameters.http_host << endl;

                // parsing IP address and port
                request_parameters.server_addr = tcp_socket.local_endpoint().address().to_string();
                request_parameters.server_port = to_string(tcp_socket.local_endpoint().port());
                request_parameters.remote_addr = tcp_socket.remote_endpoint().address().to_string();
                request_parameters.remote_port = to_string(tcp_socket.remote_endpoint().port());
                cout << "server_addr: " << request_parameters.server_addr << endl;
                cout << "server_port: " << request_parameters.server_port << endl;
                cout << "remote_addr: " << request_parameters.remote_addr << endl;
                cout << "remote_port: " << request_parameters.remote_port << endl;

                // set environment variables
                setenv("REQUEST_METHOD", request_parameters.request_method.c_str(), 1);
                setenv("REQUEST_URI", request_parameters.request_uri.c_str(), 1);
                setenv("QUERY_STRING", request_parameters.query_string.c_str(), 1);
                setenv("SERVER_PROTOCOL", request_parameters.server_protocol.c_str(), 1);
                setenv("HTTP_HOST", request_parameters.http_host.c_str(), 1);
                setenv("SERVER_ADDR", request_parameters.server_addr.c_str(), 1);
                setenv("SERVER_PORT", request_parameters.server_port.c_str(), 1);
                setenv("REMOTE_ADDR", request_parameters.remote_addr.c_str(), 1);
                setenv("REMOTE_PORT", request_parameters.remote_port.c_str(), 1);

                // reply 200 OK to inform that the request is successful
                tcp_socket.async_write_some(boost::asio::buffer(string("HTTP/1.1 200 OK\r\n")), [this, self](boost::system::error_code ec, size_t){});
                // prepare to exec the cgi program
                int sock_fd = tcp_socket.native_handle();
                dup2(sock_fd, STDOUT_FILENO);

                // allocate char* array and copy command to argument
                char **arguments = new char* [2];
                arguments[0] = new char [request_parameters.cgi_filename.length() + 1];
                strcpy(arguments[0], request_parameters.cgi_filename.c_str());
                arguments[1] = NULL;

                // exec the cgi program
                if(execvp(arguments[0], arguments) == -1){
                    cerr << "Unknown command: [" << arguments[0] << "]." <<endl;
                }
                exit(0);
            }
        });
    }
    tcp::socket tcp_socket;
    char buf[MAXLEN];
};

class http_server{
public:
    http_server(boost::asio::io_context &io_context, short port) : tcp_acceptor(io_context, tcp::endpoint(tcp::v4(), port)){
        do_accept(io_context);
    }
private:
    void do_accept(boost::asio::io_context &io_context){
        tcp_acceptor.async_accept([&io_context, this](boost::system::error_code ec, tcp::socket socket){
            if(!ec){
                io_context.notify_fork(io_context.fork_prepare);
                if(fork() == 0){
                    io_context.notify_fork(io_context.fork_child);
                    make_shared<http_session>(move(socket))->start();
                }
                else{
                    io_context.notify_fork(io_context.fork_parent);
                    socket.close();
                    do_accept(io_context);
                }
            }
            do_accept(io_context);
        });
    }
    tcp::acceptor tcp_acceptor;
};

int main(int argc, char* argv[]){
    try{
        if(argc != 2){
            cerr << "./http_server [port]" << endl;
            return 1;
        }

        boost::asio::io_context io_context;
        http_server server(io_context, atoi(argv[1]));
        io_context.run();
    }
    catch(exception &e){
        cerr << "Exception: " << e.what() << "\n";
    }
}