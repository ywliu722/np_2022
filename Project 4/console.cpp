#include <iostream>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream> // for parsing strings
#include <fstream> // for reading the file
#include <boost/asio.hpp>

#define MAX_SESSION 5
#define MAXLEN 1024

using namespace std;
using boost::asio::ip::tcp;

// structure that store session information
typedef struct session_information{
    string id;
    string host;
    string port;
    string file;
    vector<string> commands;
}session_info;

// vector that store sessions
int num_session = 0;
vector<session_info> session_infos(MAX_SESSION);

// strings to store SOCKS server information
string socks_host = "";
string socks_port = "";

boost::asio::io_context io_context;

class console_session : public std::enable_shared_from_this<console_session>{
public:
    console_session(session_info console_info) : host_resolver(io_context), tcp_socket(io_context), info(move(console_info)){};
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
        tcp_socket.async_read_some(boost::asio::buffer(buf, MAXLEN - 1), 
        [this, self](boost::system::error_code ec, size_t length){
            if(!ec){
                // output received message
                string str = string(buf);
                memset(buf, '\0', MAXLEN);
                string recv_msg = html_escape(str);
                cout << "<script>document.getElementById('" << info.id << "').innerHTML += '" << recv_msg << "';</script>\n";
                cout << flush;

                // check if there is still some commands to sent to the reomte console
                if(info.commands.size() != 0){
                    // if there is prompt in the received message
                    if(str.find("%") != -1){
                        string command = info.commands[0];
                        tcp_socket.async_write_some(boost::asio::buffer(command), [self, this](boost::system::error_code ec, size_t){});
                        string send_msg = html_escape(command);
                        cout << "<script>document.getElementById('" << info.id << "').innerHTML += '<b>" << send_msg << "</b>';</script>\n";
                        cout << flush;
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

       // resolve SOCKS server
       string socks_ip;
       boost::system::error_code ec;
       auto socks_result = host_resolver.resolve(socks_host, socks_port);
        for(auto it = socks_result.cbegin(); it != socks_result.cend(); it++){
            auto endpoint = *it;
            if(it->endpoint().address().is_v4()){
                socks_ip = endpoint.endpoint().address().to_string();
                break;
            }
        }

        // resolve remote console
        string remote_ip;
        auto remote_result = host_resolver.resolve(info.host, info.port);
        for(auto it = remote_result.cbegin(); it != remote_result.cend(); it++){
            auto endpoint = *it;
            if(it->endpoint().address().is_v4()){
                remote_ip = endpoint.endpoint().address().to_string();
                break;
            }
        }

        // connect to SOCKS server
        tcp::endpoint socks_endpoint(boost::asio::ip::address::from_string(socks_ip), stoi(socks_port));
        tcp_socket.connect(socks_endpoint, ec);

        // construct SOCKS4 request
        unsigned char request[9];
        memset(request, 0, 9);
        request[0] = 4; // VN = 1
        request[1] = 1; // CD = 1 (connect)
        // fill in dst port
        int port = stoi(info.port);
        request[2] = port / 256;
        request[3] = port % 256;
        // fill in dst ip
        for(int i = 4; i <= 7; i++){
            int pos = remote_ip.find('.');
            string tmp = remote_ip.substr(0, pos);
            request[i] = stoi(tmp);
            remote_ip.erase(0, pos + 1);
        }
        // send SOCKS4 request to SOCKS server
        tcp_socket.write_some(boost::asio::buffer(request, 9));

        // receive reply from SOCKS4
        unsigned char reply[9];
        memset(reply, 0, 9);
        tcp_socket.read_some(boost::asio::buffer(reply, 8));
        cerr << "reply: " << int(reply[1]) << endl;

        // communicate with remote console
        if(reply[1] == 90) do_communicate();
    }
    tcp::resolver host_resolver{io_context};
    tcp::socket tcp_socket{io_context};
    session_info info;
    char buf[MAXLEN];
};

void parsing_query(){
    string query_string = getenv("QUERY_STRING");
    string tmp_str;
    stringstream ss(query_string);

    // parsing QUERY_STRING
    while(getline(ss, tmp_str, '&')){
        int query_len = tmp_str.length();
        if(query_len <= 3) continue;

        if(tmp_str[0] == 's'){
            if(tmp_str[1] == 'h'){
                socks_host = tmp_str.substr(3, query_len - 3);
                cerr << "sh: " << socks_host << endl;
            }
            else if(tmp_str[1] == 'p'){
                socks_port = tmp_str.substr(3, query_len - 3);
                cerr << "sp: " << socks_port << endl;
            }
        }
        else{
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
    // print out html header
    cout << "Content-type: text/html\r\n\r\n";
    cout << "<!DOCTYPE html>\n";
    cout << "  <head>\n";
    cout << "    <meta charset=\"UTF-8\" />\n";
    cout << "    <title>NP Project 3 Sample Console</title>\n";
    cout << "    <link\n";
    cout << "      rel=\"stylesheet\"\n";
    cout << "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n";
    cout << "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n";
    cout << "      crossorigin=\"anonymous\"\n";
    cout << "    />\n";
    cout << "    <link\n";
    cout << "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n";
    cout << "      rel=\"stylesheet\"\n";
    cout << "    />\n";
    cout << "    <link\n";
    cout << "      rel=\"icon\"\n";
    cout << "      type=\"image/png\"\n";
    cout << "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n";
    cout << "    />\n";
    cout << "    <style>\n";
    cout << "      * {\n";
    cout << "        font-family: \'Source Code Pro\', monospace;\n";
    cout << "        font-size: 1rem !important;\n";
    cout << "      }\n";
    cout << "      body {\n";
    cout << "        background-color: #212529;\n";
    cout << "      }\n";
    cout << "      pre {\n";
    cout << "        color: #cccccc;\n";
    cout << "      }\n";
    cout << "      b {\n";
    cout << "        color: #01b468;\n";
    cout << "      }\n";
    cout << "    </style>\n";
    cout << "  </head>\n";

    // print out html body
    cout << "  <body>\n";
    cout << "    <table class=\"table table-dark table-bordered\">\n";
    cout << "      <thead>\n";
    cout << "        <tr>\n";
    for(int i = 0; i < num_session; i++){
        cout << "          <th scope=\"col\">" << session_infos[i].host << ":" << session_infos[i].port << "</th>\n";
    }
    cout << "        </tr>\n";
    cout << "      </thead>\n";
    cout << "      <tbody>\n";
    cout << "        <tr>\n";
    for(int i = 0; i < num_session; i++){
        cout << "          <td><pre id=" << session_infos[i].id << " class=\"mb-0\"></pre></td>\n";
    }
    cout << "        </tr>\n";
    cout << "      </tbody>\n";
    cout << "    </table>\n";
    cout << "  </body>\n";
    cout << "</html>\n";
}

void connect_console(){
    for(int i = 0; i < num_session; i++){
        make_shared<console_session>(move(session_infos[i]))->start();
    }
    io_context.run();
}

int main(){
    parsing_query();
    print_html();
    connect_console();
}