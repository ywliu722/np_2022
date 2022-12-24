#include <iostream>
#include <cstdlib>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <sstream>
#include <fstream> // for reading the file
#include <boost/asio.hpp>
#include <windows.h> // for Ctrl+C signal handler

#define MAX_SESSION 5
#define MAXLEN 1024

using namespace std;
using boost::asio::ip::tcp;

boost::asio::io_context io_context;

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

// structure that store session information
typedef struct session_information{
    string id;
    string host;
    string port;
    string file;
    vector<string> commands;
}session_info;

class console_session : public std::enable_shared_from_this<console_session>{
public:
    console_session(shared_ptr<boost::asio::ip::tcp::socket> socket, session_info console_info) : host_resolver(io_context), np_socket(io_context), tcp_socket(socket), info(move(console_info)){};
    void start(){connect_console();};
private:
    // function that handling string to HTML-safe
    // ref: python html.escape(s, quote=True)
    string html_escape(string input){
        for(int i = 0; i < input.length(); i++){
            if(input[i] == '\r'){
                input.erase(input.begin() + i);
                i--;
            }
            else if(input[i] == '\n') input.replace(i, 1, "&NewLine;");
            else if(input[i] == '&') input.replace(i, 1, "&amp;");
            else if(input[i] == '<') input.replace(i, 1, "&lt;");
            else if(input[i] == '>') input.replace(i, 1, "&gt;");
            else if(input[i] == '\"') input.replace(i, 1, "&quot;");
            else if(input[i] == '\'') input.replace(i, 1, "&#x27;");
        }
        return input;
    }
    void do_communicate(){
        auto self(shared_from_this());
        memset(buf, '\0', MAXLEN);
        np_socket.async_read_some(boost::asio::buffer(buf, MAXLEN - 1), 
        [this, self](boost::system::error_code ec, size_t length){
            if(!ec){
                // output received message
                string str = string(buf);
                memset(buf, '\0', MAXLEN);
                string recv_msg = html_escape(str);
                string send_to_client = "<script>document.getElementById('" + info.id + "').innerHTML += '" + recv_msg + "';</script>\n";
                tcp_socket->async_write_some(boost::asio::buffer(send_to_client, send_to_client.length()), [this, self](boost::system::error_code ec, size_t){});

                // check if there is still some commands to sent to the reomte console
                if(info.commands.size() != 0){
                    // if there is prompt in the received message
                    if(str.find("%") != -1){
                        string command = info.commands[0];
                        np_socket.async_write_some(boost::asio::buffer(command), [self, this](boost::system::error_code ec, size_t){});
                        string send_msg = html_escape(command);
                        send_to_client = "<script>document.getElementById('" + info.id + "').innerHTML += '<b>" + send_msg + "</b>';</script>\n";
                        tcp_socket->async_write_some(boost::asio::buffer(send_to_client, send_to_client.length()), [this, self](boost::system::error_code ec, size_t){});
                        info.commands.erase(info.commands.begin()); // remove the current command
                        do_communicate();
                    }
                    else do_communicate();
                }
            }
        });
    }
    void connect_console(){
        auto self(shared_from_this());
        cerr << info.host << " " << info.port << endl;
        host_resolver.async_resolve(info.host, info.port, [this, self](const boost::system::error_code &ec, tcp::resolver::results_type results){
            cerr << results.size() << endl;
            if(!ec){
                boost::asio::async_connect(np_socket, results, [this, self](const boost::system::error_code& ec, const tcp::endpoint& endpoint){
                    if(!ec) do_communicate();
                    else cerr<<"ERROR_CONNECT: " << ec.value() << " " << ec.category().name() << ec.message() <<endl;                  
                });
            }
            else{cerr<<"ERROR_QUERY: " << ec.value() << " " << ec.category().name() << ec.message() <<endl;}
        });
    }
    tcp::resolver host_resolver{io_context};
    shared_ptr<boost::asio::ip::tcp::socket> tcp_socket;
    tcp::socket np_socket{io_context};
    session_info info;
    char buf[MAXLEN];
};

class console_cgi : public std::enable_shared_from_this<console_cgi>{
public:
    console_cgi(tcp::socket socket, http_request request_info) : tcp_socket(make_shared<boost::asio::ip::tcp::socket>(move(socket))), request(move(request_info)){}
    void start(){
        num_session = 0;
        session_infos.resize(MAX_SESSION);

        parsing_query();
        print_html();
        connect_console();
    }
private:
    void parsing_query(){
        string query_string = request.query_string;
        string tmp_str;
        stringstream ss(query_string);

        // parsing QUERY_STRING
        while(getline(ss, tmp_str, '&')){
            int query_len = tmp_str.length();
            if(query_len <= 3) break;

            int session_num = tmp_str[1] - '0';
            string query_content = tmp_str.substr(3, query_len - 3);
            if(tmp_str[0] == 'h'){
                session_infos[session_num].host = query_content;
            }
            else if(tmp_str[0] == 'p'){
                session_infos[session_num].port = query_content;
            }
            else if(tmp_str[0] == 'f'){
                session_infos[session_num].file = query_content;
            }
        }

        // check the number of correct-parameter sessions and read the commands
        for(int i = 0; i < MAX_SESSION; i++){
            if(session_infos[i].host == "" || session_infos[i].port == "" || session_infos[i].file == ""){
                break;
            }
            else{
                session_infos[i].id = to_string(i);
                ifstream ifs("./test_case/" + session_infos[i].file);
                string line;
                while(getline(ifs, line)){
                    for(int j = 0; j < line.length(); j++){
                        if(line[j] == '\r' || line[j] == '\n'){
                            line.erase(line.begin() + j);
                            j--;
                        }
                    }
                    session_infos[i].commands.push_back(line + "\r\n");
                }
                ifs.close();
                num_session++;
            }
        }
    }

    void print_html(){
        output = R"(Content-type: text/html


<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <title>NP Project 3 Sample Console</title>
    <link
      rel="stylesheet"
      href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
      integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
      crossorigin="anonymous"
    />
    <link
      href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
      rel="stylesheet"
    />
    <link
      rel="icon"
      type="image/png"
      href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
    />
    <style>
      * {
        font-family: 'Source Code Pro', monospace;
        font-size: 1rem !important;
      }
      body {
        background-color: #212529;
      }
      pre {
        color: #cccccc;
      }
      b {
        color: #01b468;
      }
    </style>
  </head>
  <body>
    <table class="table table-dark table-bordered">
      <thead>
        <tr>
)";
        for(int i = 0; i < num_session; i++) output += "          <th scope=\"col\">" + session_infos[i].host + ":" + session_infos[i].port + "</th>\n";
        output += R"(        </tr>
      </thead>
      <tbody>
        <tr>
)";
        for(int i = 0; i < num_session; i++) output += "          <td><pre id=" + session_infos[i].id + " class=\"mb-0\"></pre></td>\n";
        output += R"(        </tr>
      </tbody>
    </table>
  </body>
</html>)";
    }

    void connect_console(){
        auto self(shared_from_this());
        tcp_socket->async_write_some(boost::asio::buffer(output, output.length()), [this, self](boost::system::error_code ec, size_t){
            for(int i = 0; i < num_session; i++){
                make_shared<console_session>(tcp_socket, move(session_infos[i]))->start();
            }
        });
    }
    int num_session;
    shared_ptr<boost::asio::ip::tcp::socket> tcp_socket;
    http_request request;
    vector<session_info> session_infos;
    string output;
};

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
                request_parameters.cgi_filename = tmp_str.substr(1, tmp_str.find('?') - 1);
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

                // reply 200 OK to inform that the request is successful
                tcp_socket.async_write_some(boost::asio::buffer(string("HTTP/2.0 200 OK\r\n")), [this, self](boost::system::error_code ec, size_t){});

                if(request_parameters.cgi_filename == "panel.cgi"){
                    do_panel();
                }
                else if(request_parameters.cgi_filename == "console.cgi"){
                    make_shared<console_cgi>(move(tcp_socket), move(request_parameters))->start();
                }
            }
        });
    }

    void do_panel(){
        auto self(shared_from_this());
        string output;
        output = R"(
<!DOCTYPE html>
<html lang="en">
  <head>
    <title>NP Project 3 Panel</title>
    <link
      rel="stylesheet"
      href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
      integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
      crossorigin="anonymous"
    />
    <link
      href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
      rel="stylesheet"
    />
    <link
      rel="icon"
      type="image/png"
      href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"
    />
    <style>
      * {
        font-family: 'Source Code Pro', monospace;
      }
    </style>
  </head>
  <body class="bg-secondary pt-5">
    <form action="console.cgi" method="GET">
      <table class="table mx-auto bg-light" style="width: inherit">
        <thead class="thead-dark">
          <tr>
            <th scope="col">#</th>
            <th scope="col">Host</th>
            <th scope="col">Port</th>
            <th scope="col">Input File</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <th scope="row" class="align-middle">Session 1</th>
            <td>
              <div class="input-group">
                <select name="h0" class="custom-select">
                  <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                </select>
                <div class="input-group-append">
                  <span class="input-group-text">.cs.nctu.edu.tw</span>
                </div>
              </div>
            </td>
            <td>
              <input name="p0" type="text" class="form-control" size="5" />
            </td>
            <td>
              <select name="f0" class="custom-select">
                <option></option>
                <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
              </select>
            </td>
          </tr>
          <tr>
            <th scope="row" class="align-middle">Session 2</th>
            <td>
              <div class="input-group">
                <select name="h1" class="custom-select">
                  <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                </select>
                <div class="input-group-append">
                  <span class="input-group-text">.cs.nctu.edu.tw</span>
                </div>
              </div>
            </td>
            <td>
              <input name="p1" type="text" class="form-control" size="5" />
            </td>
            <td>
              <select name="f1" class="custom-select">
                <option></option>
                <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
              </select>
            </td>
          </tr>
          <tr>
            <th scope="row" class="align-middle">Session 3</th>
            <td>
              <div class="input-group">
                <select name="h2" class="custom-select">
                  <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                </select>
                <div class="input-group-append">
                  <span class="input-group-text">.cs.nctu.edu.tw</span>
                </div>
              </div>
            </td>
            <td>
              <input name="p2" type="text" class="form-control" size="5" />
            </td>
            <td>
              <select name="f2" class="custom-select">
                <option></option>
                <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
              </select>
            </td>
          </tr>
          <tr>
            <th scope="row" class="align-middle">Session 4</th>
            <td>
              <div class="input-group">
                <select name="h3" class="custom-select">
                  <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                </select>
                <div class="input-group-append">
                  <span class="input-group-text">.cs.nctu.edu.tw</span>
                </div>
              </div>
            </td>
            <td>
              <input name="p3" type="text" class="form-control" size="5" />
            </td>
            <td>
              <select name="f3" class="custom-select">
                <option></option>
                <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
              </select>
            </td>
          </tr>
          <tr>
            <th scope="row" class="align-middle">Session 5</th>
            <td>
              <div class="input-group">
                <select name="h4" class="custom-select">
                  <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                </select>
                <div class="input-group-append">
                  <span class="input-group-text">.cs.nctu.edu.tw</span>
                </div>
              </div>
            </td>
            <td>
              <input name="p4" type="text" class="form-control" size="5" />
            </td>
            <td>
              <select name="f4" class="custom-select">
                <option></option>
                <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
              </select>
            </td>
          </tr>
          <tr>
            <td colspan="3"></td>
            <td>
              <button type="submit" class="btn btn-info btn-block">Run</button>
            </td>
          </tr>
        </tbody>
      </table>
    </form>
  </body>
</html>)";
        tcp_socket.async_write_some(boost::asio::buffer(output, output.length()),[this, self](boost::system::error_code ec, size_t){
                tcp_socket.close();
        });
    }

    tcp::socket tcp_socket;
    char buf[MAXLEN];
};

class http_server{
public:
    http_server(short port) : tcp_acceptor(io_context, tcp::endpoint(tcp::v4(), port)){
        do_accept();
    }
private:
    void do_accept(){
        tcp_acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket){
            if(!ec){
                make_shared<http_session>(move(socket))->start();
            }
            do_accept();
        });
    }
    tcp::acceptor tcp_acceptor;
};

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType){
    if(fdwCtrlType == CTRL_C_EVENT){
        cout << "Ctrl+C detected!" << endl;
        exit(0);
    }
    return TRUE;
}

int main(int argc, char *argv[]){
    try{
        // set up Ctrl+C signal handler
        if(!SetConsoleCtrlHandler(CtrlHandler, TRUE)){
            cerr << "" << endl;
        }
        if(argc != 2){
            cerr << "./http_server [port]" << endl;
            return 1;
        }

        http_server server(atoi(argv[1]));
        io_context.run();
    }
    catch(exception &e){
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}