#include <iostream>
#include "thread"
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>
#include <set>
#include <unordered_set>
#include <algorithm>

#include <random>
#include <map>
#include <sstream>
#include <chrono>
// const int BASE_PORT = 8080; // Starting port number
const int BASE_PORT = 48760; // Starting port number
const int BUFFER_SIZE = 1024;

using namespace std;


mutex mtx;
// const int num_processes = 4;
const int num_processes = 24;
int num;

vector<int> result;


class Process {
public:
    bool stopListening = false; // Flag to indicate when to stop listening
    int id;
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    vector<int> client_sockets;
    vector<int> client_finished; // 0: not finished, 1:just finished, 2: finished at previous round

    map<int, int> socket_id;
    map<int, int> id_socket;

    int var_msg = 0;

    Process(int id) : id(id) {
        setupServer();
        // active = true;
        succ = 0;
        children.clear();
        inCycle = false;
        parent = -1;
        received_ok_from.clear();
        for(int i=0;i<num_processes;i++) {
            pref.insert(make_pair(i, i));
        }
        house = id;
        client_finished.resize(num_processes, 0);

    }

    void setupServer() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        // server_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (server_fd == 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(BASE_PORT + id);

        bind(server_fd, (struct sockaddr *)&address, sizeof(address));
        
        // if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        //     perror("Bind failed");
        //     exit(EXIT_FAILURE);
        // }
        if (listen(server_fd, 10) < 0) {
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }
        cout << "Process " << id << " listening on port " << BASE_PORT + id << "\n";

    }

    void connectTo(int target_process_id) {
        int sock = 0;
        struct sockaddr_in serv_addr;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            cout << "Socket creation error\n";
            return;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(BASE_PORT + target_process_id);

        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            cout << "Invalid address\n";
            return;
        }

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            cout << "Connection Failed to Process " << target_process_id << "\n";
            return;
        }

        lock_guard<mutex> lock(mtx);
        client_sockets.push_back(sock);

        cout << "Process " << id << " connected to Process " << target_process_id << "\n";

        socket_id[sock] = target_process_id;
        id_socket[target_process_id] = sock;
    }

    void listenForMessages() {
        while(num < num_processes){}
        while (true) {
        // while (!stopListening) {
            int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                perror("Accept failed");
                continue;
            }
            cout<<"process: "<<id<<" ready to accept new socket: "<<new_socket<<endl;

            thread(&Process::handleConnection, this, new_socket).detach();
            cout<<"thread: "<<id<<" in listenForMessages\n";

        }

    }



    void handleConnection(int socket) {
        while(true) {
            TTC();

            char buffer[BUFFER_SIZE] = {0};
            // int valread = recv(socket, buffer, BUFFER_SIZE, 0);  // 使用 recv 而非 read
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(socket, buffer, BUFFER_SIZE);
            if (valread <= 0) {
                close(socket);  // 當對方斷開或出現錯誤時才關閉 socket
                // break;
            }
            string arrive_message;
            arrive_message.assign(buffer,valread);
            stringstream ss(arrive_message);
            string message;
            while(getline(ss, message, '\n')) {

                lock_guard<mutex> lock(mtx);
                size_t ptr = message.find('_');
                string msg = message.substr(0, ptr);

                cout<<"process: "<<id<<" receive msg: "<<message<<endl;
                if(msg == "ok") {
                    int sender_id = getSenderIdFromMessage(message); // Placeholder for extracting sender ID
                    onReceivingOk(sender_id);
                }else if(msg == "nextStage") { //*********************************************************need initialized children, parent
                    cout<<"process: "<<id<<" receive nextStage"<<endl;
                    if(assigned == false) {
                        next = pref[house_rank[id][0]];
                        children.clear();
                        parent = -1;
                    }
                }else if(msg == "Remove") {
                    // int remove_house = stoi(message.substr(ptr+1, message.size()-1-ptr));
                    size_t ptr2 = message.find('_', ptr+1);

                    int h = stoi(message.substr(ptr+1, ptr2-1-ptr));
                    int from_pid = stoi(message.substr(ptr2+1, message.size()-1-ptr2));
                    house_rank[from_pid].clear();
                    client_finished[from_pid] = 1;

                    // add
                    if(h == house) {
                        cout<<"issue\n";
                        // TTC();
                    }
                    build_erase_pref(h, from_pid);

                }
                cout<<"process: "<<id<<" in getline loop\n";
            }
        }
    }

    void sendMessage(int target_socket, const string& msg) {
        string message=msg+"\n";
        send(target_socket, message.c_str(), message.size(), 0);
        var_msg++;
    }

    void broadcastMessage(const string& message) {
        // lock_guard<mutex> lock(mtx);
        for (int socket : client_sockets) {
            cout<<"process: "<<id<<" broadcast sending: "<<socket<<" destid: "<<socket_id[socket]<<" msg: "<<message<<endl;
            if(socket_id[socket]!=id  && client_finished[socket_id[socket]] != 2) {
                sendMessage(socket, message);
                if(client_finished[socket_id[socket]]==1) client_finished[socket_id[socket]] = 2; 
            }
        }
    }


    // 3 1 0 2
    // vector<vector<int>> house_rank = {{3,1,2,0}, {0, 2, 3, 1}, {0,3, 1, 2}, {2,1,3,0}};
    // vector<vector<int>> house_rank = {{1,2,0}, {0, 2, 1}, {0, 1, 2}};
    // vector<vector<int>> house_rank = {{7, 0, 5, 3, 1, 4, 2, 6}, {7, 0, 6, 2, 3, 4, 5, 1}, {0, 6, 2, 7, 1, 3, 4, 5}, {5, 3, 6, 4, 0, 2, 7, 1}, {5, 4, 0, 7, 1, 6, 3, 2}, {3, 7, 5, 1, 2, 4, 0, 6}, {3, 1, 2, 4, 6, 7, 5, 0}, {2, 3, 1, 5, 0, 6, 4, 7}};
    // vector<vector<int>> house_rank = {{0, 5, 1, 8, 7, 2, 3, 6, 4}, {5, 3, 8, 1, 7, 6, 4, 0, 2}, {4, 1, 8, 6, 3, 7, 2, 0, 5}, {1, 5, 3, 6, 7, 8, 2, 0, 4}, {5, 1, 0, 2, 7, 4, 8, 6, 3}, {8, 1, 3, 0, 7, 6, 4, 5, 2}, {6, 8, 3, 0, 2, 4, 5, 1, 7}, {1, 2, 8, 5, 6, 0, 7, 3, 4}, {5, 1, 4, 3, 6, 0, 8, 2, 7}};
    // vector<vector<int>> house_rank = {{6, 3, 2, 5, 0, 4, 1, 7}, {4, 1, 2, 7, 3, 5, 0, 6}, {5, 3, 2, 4, 0, 6, 7, 1}, {2, 0, 3, 4, 5, 6, 1, 7}, {0, 2, 6, 4, 3, 5, 1, 7}, {5, 0, 4, 6, 1, 2, 7, 3}, {1, 5, 7, 4, 2, 3, 6, 0}, {0, 7, 2, 1, 4, 5, 3, 6}};
    // vector<vector<int>> house_rank = {{0, 5, 4, 1, 3, 2, 6}, {6, 3, 2, 0, 1, 5, 4}, {1, 2, 5, 6, 4, 0, 3}, {4, 3, 0, 1, 2, 5, 6}, {2, 0, 6, 4, 1, 3, 5}, {4, 0, 6, 2, 5, 3, 1}, {1, 5, 6, 0, 4, 2, 3}};
    // vector<vector<int>> house_rank = {{1, 2, 0, 3, 4, 5}, {1, 3, 5, 4, 2, 0}, {0, 2, 5, 3, 4, 1}, {5, 3, 4, 1, 2, 0}, {5, 0, 3, 1, 2, 4}, {4, 0, 5, 1, 2, 3}};
    // vector<vector<int>> house_rank = {{10, 5, 1, 6, 9, 12, 7, 11, 3, 0, 8, 4, 2}, {8, 6, 3, 2, 10, 5, 11, 12, 9, 4, 7, 1, 0}, {9, 6, 7, 8, 11, 1, 10, 3, 4, 0, 2, 12, 5}, {1, 0, 3, 4, 5, 9, 2, 6, 10, 8, 7, 11, 12}, {8, 3, 12, 6, 1, 7, 2, 0, 9, 5, 11, 10, 4}, {8, 9, 6, 1, 7, 10, 3, 2, 4, 5, 11, 0, 12}, {3, 6, 7, 11, 9, 1, 4, 5, 10, 0, 2, 8, 12}, {11, 10, 8, 6, 12, 2, 4, 0, 1, 7, 9, 5, 3}, {0, 8, 7, 4, 10, 5, 12, 3, 1, 11, 2, 6, 9}, {3, 10, 8, 11, 9, 5, 6, 2, 1, 12, 7, 0, 4}, {0, 3, 6, 5, 7, 12, 10, 9, 8, 11, 4, 1, 2}, {1, 10, 2, 12, 11, 9, 5, 3, 7, 6, 8, 4, 0}, {10, 12, 9, 8, 11, 0, 2, 5, 4, 1, 3, 7, 6}};
    // vector<vector<int>> house_rank = {{3, 4, 5, 9, 7, 6, 2, 11, 1, 10, 8, 0}, {10, 8, 11, 0, 4, 5, 3, 2, 9, 1, 7, 6}, {2, 0, 6, 1, 9, 8, 4, 7, 5, 11, 10, 3}, {4, 2, 10, 1, 3, 5, 6, 8, 11, 7, 9, 0}, {10, 9, 1, 5, 7, 4, 3, 6, 8, 11, 0, 2}, {5, 4, 8, 0, 9, 10, 6, 1, 2, 7, 11, 3}, {3, 2, 6, 10, 8, 7, 11, 0, 9, 5, 4, 1}, {0, 2, 1, 10, 8, 4, 5, 9, 11, 6, 7, 3}, {8, 5, 10, 4, 1, 9, 11, 3, 6, 0, 7, 2}, {3, 8, 0, 11, 5, 6, 10, 7, 9, 4, 1, 2}, {11, 10, 5, 0, 3, 8, 2, 4, 9, 6, 1, 7}, {1, 7, 0, 11, 8, 5, 2, 6, 10, 9, 4, 3}};
    // vector<vector<int>> house_rank = {{1, 6, 0, 3, 4, 2, 8, 5, 9, 10, 7}, {5, 8, 3, 4, 6, 10, 9, 2, 0, 7, 1}, {9, 2, 7, 8, 10, 6, 5, 4, 1, 3, 0}, {7, 8, 10, 1, 3, 2, 6, 9, 5, 4, 0}, {5, 6, 4, 8, 3, 7, 1, 9, 2, 10, 0}, {0, 4, 9, 6, 2, 7, 1, 3, 5, 8, 10}, {9, 3, 8, 1, 7, 10, 5, 6, 2, 4, 0}, {6, 0, 9, 2, 4, 7, 1, 10, 8, 5, 3}, {2, 6, 9, 10, 3, 8, 4, 7, 1, 0, 5}, {0, 4, 8, 2, 10, 5, 7, 3, 9, 6, 1}, {10, 1, 9, 2, 5, 3, 6, 0, 8, 7, 4}};
    // vector<vector<int>> house_rank = {{2, 3, 9, 11, 12, 0, 10, 1, 8, 7, 6, 14, 4, 5, 13}, {11, 3, 4, 13, 10, 8, 9, 0, 2, 14, 6, 7, 1, 5, 12}, {8, 3, 12, 7, 1, 6, 2, 5, 14, 11, 4, 13, 10, 0, 9}, {14, 0, 2, 8, 4, 10, 7, 11, 6, 5, 9, 12, 3, 13, 1}, {7, 2, 12, 3, 4, 8, 11, 0, 9, 5, 6, 13, 10, 1, 14}, {2, 3, 1, 12, 9, 14, 10, 11, 6, 13, 5, 4, 0, 8, 7}, {0, 9, 3, 10, 7, 13, 8, 11, 1, 14, 5, 12, 4, 2, 6}, {2, 11, 5, 3, 13, 4, 9, 10, 14, 8, 0, 6, 1, 7, 12}, {11, 9, 13, 1, 14, 2, 12, 0, 10, 3, 5, 4, 8, 6, 7}, {2, 4, 14, 9, 13, 12, 3, 11, 7, 1, 6, 8, 10, 5, 0}, {3, 10, 14, 13, 0, 2, 1, 6, 8, 4, 12, 5, 11, 9, 7}, {10, 2, 9, 5, 0, 8, 7, 1, 14, 11, 4, 6, 12, 3, 13}, {3, 5, 11, 13, 6, 7, 12, 1, 2, 9, 8, 4, 0, 14, 10}, {2, 13, 6, 5, 7, 0, 12, 11, 3, 9, 4, 10, 1, 8, 14}, {1, 13, 10, 4, 11, 0, 5, 12, 2, 9, 6, 3, 14, 7, 8}};
    // vector<vector<int>> house_rank = {{5, 11, 8, 14, 9, 6, 7, 3, 18, 4, 10, 17, 15, 1, 19, 2, 13, 12, 0, 16}, {7, 17, 11, 5, 8, 10, 1, 12, 3, 14, 13, 18, 19, 16, 0, 15, 2, 6, 9, 4}, {10, 17, 0, 6, 16, 18, 1, 19, 8, 7, 4, 5, 11, 12, 14, 3, 2, 15, 13, 9}, {9, 3, 15, 1, 13, 8, 17, 0, 6, 11, 12, 4, 19, 7, 16, 18, 5, 14, 10, 2}, {4, 2, 9, 0, 3, 14, 10, 15, 13, 11, 5, 8, 19, 1, 6, 12, 7, 17, 16, 18}, {15, 0, 1, 8, 18, 11, 2, 10, 14, 3, 19, 9, 17, 6, 13, 4, 7, 5, 16, 12}, {7, 0, 3, 8, 5, 16, 18, 2, 6, 12, 1, 14, 17, 10, 19, 9, 4, 13, 15, 11}, {15, 19, 4, 10, 7, 9, 3, 1, 17, 0, 14, 6, 8, 13, 2, 16, 11, 18, 5, 12}, {19, 17, 12, 5, 16, 13, 3, 10, 15, 4, 11, 9, 14, 2, 6, 8, 1, 18, 7, 0}, {18, 12, 2, 15, 11, 7, 0, 13, 4, 5, 9, 8, 6, 19, 17, 3, 1, 16, 10, 14}, {12, 6, 15, 11, 10, 1, 4, 8, 5, 9, 16, 17, 18, 0, 14, 7, 3, 2, 19, 13}, {0, 5, 18, 17, 19, 15, 11, 1, 6, 9, 12, 10, 7, 13, 16, 3, 4, 14, 8, 2}, {7, 12, 9, 10, 1, 18, 2, 13, 16, 11, 14, 4, 0, 8, 5, 19, 17, 15, 6, 3}, {7, 17, 19, 14, 1, 16, 9, 13, 4, 0, 18, 8, 5, 3, 10, 12, 2, 6, 15, 11}, {7, 8, 14, 6, 19, 3, 1, 18, 13, 15, 12, 2, 0, 5, 10, 16, 11, 9, 4, 17}, {4, 5, 2, 1, 11, 6, 19, 7, 14, 15, 13, 18, 0, 3, 9, 10, 17, 12, 16, 8}, {9, 19, 0, 14, 3, 6, 12, 11, 4, 17, 10, 1, 13, 7, 2, 16, 5, 8, 18, 15}, {8, 5, 6, 16, 4, 12, 15, 13, 3, 7, 17, 9, 11, 18, 2, 10, 14, 0, 19, 1}, {4, 5, 14, 13, 6, 3, 10, 15, 18, 16, 11, 0, 9, 12, 2, 7, 8, 17, 19, 1}, {16, 19, 13, 10, 3, 9, 11, 12, 0, 18, 8, 5, 7, 6, 14, 4, 1, 2, 17, 15}};
    // vector<vector<int>> house_rank = {{9, 7, 2, 1, 3, 5, 4, 8, 6, 0}, {2, 9, 3, 6, 7, 1, 4, 0, 8, 5}, {6, 2, 0, 7, 9, 3, 1, 5, 4, 8}, {8, 3, 2, 0, 5, 9, 4, 1, 6, 7}, {6, 4, 2, 9, 7, 0, 5, 3, 1, 8}, {9, 1, 7, 3, 5, 2, 8, 0, 4, 6}, {3, 1, 5, 2, 8, 6, 0, 7, 4, 9}, {2, 0, 7, 1, 6, 4, 8, 5, 3, 9}, {1, 7, 9, 2, 6, 0, 8, 3, 4, 5}, {2, 3, 0, 6, 8, 4, 9, 7, 5, 1}};
    // vector<vector<int>> house_rank = {{2, 4, 3, 0, 1}, {4, 3, 0, 2, 1}, {3, 2, 0, 4, 1}, {3, 4, 2, 0, 1}, {0, 3, 1, 4, 2}};
    // vector<vector<int>> house_rank = {{4, 5, 0, 8, 6, 1, 3, 7, 2, 9}, {2, 3, 0, 7, 8, 6, 9, 5, 1, 4}, {8, 3, 9, 7, 5, 4, 0, 6, 1, 2}, {4, 3, 9, 5, 1, 8, 0, 2, 7, 6}, {6, 1, 3, 5, 9, 8, 2, 4, 7, 0}, {3, 4, 6, 7, 2, 5, 1, 8, 9, 0}, {8, 9, 1, 2, 3, 5, 6, 0, 4, 7}, {1, 2, 9, 5, 0, 8, 6, 7, 3, 4}, {1, 7, 6, 5, 2, 8, 4, 3, 0, 9}, {1, 0, 7, 6, 9, 4, 8, 5, 2, 3}};
    vector<vector<int>> house_rank = {
        {7, 21, 13, 4, 19, 14, 11, 12, 17, 3, 22, 23, 5, 1, 8, 0, 15, 9, 2, 10, 18, 16, 6, 20},
        {14, 10, 11, 8, 3, 16, 12, 23, 0, 20, 1, 19, 22, 9, 17, 21, 15, 5, 7, 4, 13, 18, 6, 2},
        {21, 16, 5, 23, 13, 7, 14, 2, 11, 3, 22, 20, 6, 9, 10, 8, 1, 12, 0, 19, 18, 15, 17, 4},
        {12, 6, 23, 7, 13, 3, 21, 9, 5, 14, 11, 4, 2, 19, 18, 20, 22, 15, 17, 1, 16, 10, 0, 8},
        {21, 23, 8, 15, 19, 12, 4, 1, 3, 6, 18, 2, 17, 0, 11, 9, 7, 14, 13, 10, 5, 20, 22, 16},
        {8, 6, 9, 0, 19, 4, 21, 11, 17, 23, 20, 16, 7, 5, 1, 22, 2, 12, 15, 3, 13, 10, 18, 14},
        {0, 7, 11, 23, 19, 18, 8, 5, 13, 6, 21, 9, 17, 10, 3, 16, 22, 15, 20, 2, 1, 12, 4, 14},
        {22, 23, 5, 17, 2, 1, 21, 13, 18, 7, 14, 20, 0, 8, 12, 9, 4, 10, 3, 11, 19, 6, 15, 16},
        {6, 9, 21, 3, 11, 16, 5, 19, 7, 1, 18, 2, 17, 14, 4, 20, 12, 15, 13, 8, 10, 22, 0, 23},
        {17, 10, 20, 16, 11, 4, 1, 9, 12, 23, 21, 3, 7, 19, 8, 15, 2, 18, 14, 0, 22, 6, 13, 5},
        {2, 14, 6, 1, 18, 13, 20, 9, 16, 17, 11, 0, 4, 23, 8, 7, 19, 3, 22, 15, 5, 21, 10, 12},
        {22, 14, 2, 12, 4, 8, 10, 9, 11, 3, 16, 7, 1, 18, 19, 0, 15, 20, 17, 21, 23, 13, 5, 6},
        {2, 21, 20, 11, 5, 23, 17, 7, 10, 9, 6, 19, 16, 15, 14, 4, 13, 3, 22, 8, 0, 12, 1, 18},
        {21, 12, 7, 16, 0, 9, 20, 22, 10, 18, 14, 8, 3, 2, 11, 17, 6, 5, 1, 23, 13, 19, 15, 4},
        {22, 19, 20, 4, 6, 7, 9, 0, 8, 11, 10, 21, 13, 3, 16, 14, 23, 15, 12, 2, 17, 5, 18, 1},
        {22, 4, 23, 10, 1, 20, 3, 9, 7, 6, 2, 11, 18, 17, 16, 12, 15, 21, 19, 8, 5, 13, 0, 14},
        {13, 14, 4, 20, 9, 3, 18, 23, 11, 6, 21, 17, 7, 5, 12, 19, 10, 2, 8, 0, 1, 22, 15, 16},
        {6, 18, 21, 19, 16, 15, 11, 14, 10, 13, 20, 8, 0, 9, 1, 12, 2, 17, 23, 7, 4, 5, 3, 22},
        {6, 2, 22, 14, 5, 7, 15, 11, 9, 12, 8, 23, 0, 20, 10, 21, 18, 16, 3, 1, 13, 17, 4, 19},
        {0, 6, 15, 1, 20, 7, 4, 10, 13, 3, 16, 14, 12, 5, 23, 18, 22, 21, 11, 2, 8, 9, 17, 19},
        {1, 18, 13, 14, 17, 4, 2, 9, 15, 11, 6, 7, 12, 3, 22, 5, 10, 23, 19, 20, 16, 0, 21, 8},
        {20, 14, 4, 8, 7, 9, 13, 15, 6, 2, 11, 12, 17, 22, 1, 5, 0, 16, 3, 10, 23, 19, 18, 21},
        {10, 1, 22, 12, 6, 4, 13, 17, 18, 7, 19, 2, 9, 16, 5, 11, 15, 0, 21, 3, 20, 14, 23, 8},
        {2, 6, 7, 4, 22, 5, 15, 3, 20, 14, 23, 17, 16, 18, 13, 11, 9, 19, 21, 8, 0, 1, 12, 10}
    };

// for algorithm 1
    bool active;
    int succ;
    
    vector<int> children;
    int parent;
    bool inCycle;
    unordered_set<int> received_ok_from; // Tracks children who have sent "ok"
// TTC
    int next; //  the node which holds current top choice of Pi
    bool assigned; // whether be assigned ﬁnal house
    int house; // the house Pi currently holds, intially process i holds house i
    map<int, int> pref; // mapping from a house to the node which holds the house.

    void algorithm1() {
        if(assigned == true) return;
        next = pref[house_rank[id][0]];
        if(next == id){
            inCycle = true;
            return;
        }
        int curr = id;
        succ = curr;
        bool isRoot = true; // smallest id in a cycle can be the root
        vector<bool>visited(num_processes, 0);
        visited[id] = true;
        do{
            if(succ == pref[house_rank[succ][0]]){ // the other man got its top choice
                break;
            }else{
                curr = succ;
                succ = pref[house_rank[curr][0]];

                if(visited[succ] != true) children.emplace_back(succ);


            }

            if(succ == id) { // cycle found and I am in the cycle
                inCycle = true;
                reset_pref(visited, curr, isRoot);
            }
            if(visited[succ] == true) { // cycle found
                cout<<id<<" cycle found\n";
                break;

            }
            else visited[succ] = true;
        }while(succ != id); //************* need to solve if there's a branch insert to a cycle -> use visited

    }

    void reset_pref(vector<bool> &visited, int curr, bool &isRoot) {
        int k = curr;
        int k_next = pref[house_rank[k][0]];
        int min_id = curr;
        while(k_next != curr) {
            pref[house_rank[k][0]] = k;
            k = k_next;
            k_next = pref[house_rank[k][0]];
            
            if(min_id > k) min_id = k;
        }


        if(min_id == id) {
            parent = -1;
            isRoot = true;
        }else{
            parent = min_id;
            isRoot = false;
        }
    }

    void TTC() {
        if(assigned == false) children.clear();

        algorithm1();
        succ = next;

        string msg;

        if(inCycle == true && assigned==false) {
            int hj = house_rank[id][0];
            house = hj;
            result[id] = house;

            assigned = true;

            // Broadcast remove(hi) to all
            msg.clear();
            msg = "Remove_"+to_string(house)+"_"+to_string(id);
            broadcastMessage(msg);
            // if(children.size()==0) {
            if(parent != -1) {
                msg.clear();
                msg = "ok_"+to_string(id);
                if(parent >= 0 && parent < num_processes) sendMessage(id_socket[parent], msg);
            }
        }
        cout<<"process: "<<id<<" house: "<<house<<" assigned: "<<assigned<<endl;
    }



    void onReceivingOk(int sender_id) {
        // Add the sender to the set of children who sent "ok"
        received_ok_from.insert(sender_id);
        // Check if we have received "ok" from all children
        if (received_ok_from.size() == children.size()) {
            onReceivingOkFromAllChildren();
        }
    }

    void onReceivingOkFromAllChildren() {
        // Reset received_ok_from for the next stage
        received_ok_from.clear();

        if (parent == -1) { // root
            string msg = "nextStage_";
            broadcastMessage(msg);
        } else {
            sendMessage(id_socket[parent], "ok_"+to_string(id));
        }
    }


    int getSenderIdFromMessage(const string& message) {
        size_t ptr = message.find('_');
        string idd = message.substr(ptr+1, message.size()-1-ptr);
        return stoi(idd); // Replace with actual logic
    }

    void build_erase_pref(int h, int from_pid) {
        for(int i=0;i<num_processes;i++) {
            if(house_rank[i].size()>0) {
                auto it = find(house_rank[i].begin(), house_rank[i].end(), h);
                if(it != house_rank[i].end()) {
                    house_rank[i].erase(it);
                }
            }
        }
        pref[from_pid] = h;
        if(h == house){
            house = house_rank[id][0];
            pref[id] = house;
        }
    }


};

int main() {
    auto start = chrono::high_resolution_clock::now();
    num = 0;
    result.resize(num_processes);

    // Create multiple processes
    vector<unique_ptr<Process>> processes;
    for (int i = 0; i < num_processes; i++) {
        processes.push_back(make_unique<Process>(i));
    }

    // Connect each process to all other processes
    for (int i = 0; i < num_processes; i++) {
        for (int j = 0; j < num_processes; j++) {
            if (i != j) {
                processes[i]->connectTo(j);
            }
        }
    }



    // Start listening for messages in separate threads for each process
    vector<thread> listen_threads;
    for (int i = 0; i < num_processes; i++) {
        listen_threads.emplace_back(&Process::listenForMessages, processes[i].get());
        num++;
    }



    // // Join threads


    bool flag = 0;
    while(!flag) {
        flag = 1;
        for(int i=0;i<num_processes;i++) {
            if(processes[i]->house_rank[i].size()>1){
                flag = false;
                break;
            }
        }
    }


    for(int i=0;i<num_processes;i++) {
        cout<<"process "<<i<<"'s house: "<< result[i]<<endl;
    }
    int total_msg = 0;
    for(int i=0;i<num_processes;i++) {
        cout<<"# of msg of process "<<i<<": "<<processes[i]->var_msg<<endl;
        total_msg += processes[i]->var_msg;
    }
    cout<<"total msg: "<<total_msg<<endl;



    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end - start).count();
    cout << "Execution time: " << duration << " microseconds" << endl;

    for(int i=0;i<num_processes;i++) {
        auto &t = listen_threads[i];
        if(t.joinable()){
            t.join();
        }
    }


    return 0;
}

