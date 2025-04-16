#include <iostream>
#include "thread"
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>
#include <set>
#include <unordered_set>
#include <algorithm>

#include <random>
#include <map>
#include <sstream>
// const int BASE_PORT = 8080; // Starting port number
const int BASE_PORT = 48760; // Starting port number
const int BUFFER_SIZE = 1024;

std::mutex mtx;
const int num_processes = 4;
// const int num_processes = 3;

std::vector<std::thread> connect_threads;


std::vector<int> result;


class Process {
public:
    bool stopListening = false; // Flag to indicate when to stop listening
    int id;
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    std::vector<int> client_sockets;
    std::vector<int> client_finished; // 0: not finished, 1:just finished, 2: finished at previous round

    std::map<int, int> socket_id;
    std::map<int, int> id_socket;

    Process(int id) : id(id) {
        setupServer();
        // active = true;
        succ = 0;
        children.clear();
        inCycle = false;
        parent = -1;
        received_ok_from.clear();
        for(int i=0;i<num_processes;i++) {
            pref.insert(std::make_pair(i, i));
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

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 10) < 0) {
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }
        std::cout << "Process " << id << " listening on port " << BASE_PORT + id << "\n";
    }

    void connectTo(int target_process_id) {
        int sock = 0;
        struct sockaddr_in serv_addr;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            std::cout << "Socket creation error\n";
            return;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(BASE_PORT + target_process_id);

        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            std::cout << "Invalid address\n";
            return;
        }

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cout << "Connection Failed to Process " << target_process_id << "\n";
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);
        client_sockets.push_back(sock);
        std::cout << "Process " << id << " connected to Process " << target_process_id << "\n";
        socket_id[sock] = target_process_id;
        id_socket[target_process_id] = sock;
    }

    void listenForMessages() {
        while (true) {
        // while (!stopListening) {
            int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                perror("Accept failed");
                continue;
            }
            std::cout<<"process: "<<id<<" ready to accept new socket: "<<new_socket<<std::endl;
            std::thread(&Process::handleConnection, this, new_socket).detach();
            // handleConnection(new_socket);
            // connect_threads.emplace_back(&Process::handleConnection, this, new_socket);
            std::cout<<"thread: "<<id<<" in listenForMessages\n";
            // if(assigned == true || house_rank[id].size() <= 1) break;
            // close(new_socket);
        }

        std::cout<<"thread: "<<id<<" exit listenForMessages\n";
    }



    void handleConnection(int socket) {
        while(true) {
            TTC();

            std::cout<<"after TTC process: "<<id<<" children:";
            for(auto&k: children) {
                std::cout<<" "<<k;
            }
            std::cout<<std::endl;

            char buffer[BUFFER_SIZE] = {0};
            // int valread = recv(socket, buffer, BUFFER_SIZE, 0);  // 使用 recv 而非 read
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(socket, buffer, BUFFER_SIZE);
            if (valread <= 0) {
                std::cout << "Process " << id << " connection closed or error occurred on socket: " << socket << std::endl;
                close(socket);  // 當對方斷開或出現錯誤時才關閉 socket
                // break;
            }
            std::string arrive_message;
            arrive_message.assign(buffer,valread);
            std::stringstream ss(arrive_message);
            std::string message;
            while(std::getline(ss, message, '\n')) {

                std::lock_guard<std::mutex> lock(mtx);
                // std::cout << "Process " << id << " received: " << message << std::endl;
                // close(socket);  // Close the socket after handling the message
                size_t ptr = message.find('_');
                std::string msg = message.substr(0, ptr);
                std::cout<<"process: "<<id<<" receive msg: "<<message<<std::endl;
                if(msg == "ok") {
                    int sender_id = getSenderIdFromMessage(message); // Placeholder for extracting sender ID
                    onReceivingOk(sender_id);
                }else if(msg == "nextStage") { //*********************************************************need initialized children, parent
                    std::cout<<"process: "<<id<<" receive nextStage, assigned: "<<assigned<<std::endl;
                    if(assigned == false) {
                        // active = true;
                        next = pref[house_rank[id][0]];
                        children.clear();
                        parent = -1;
                        // TTC();
                    }
                }else if(msg == "Remove") {
                    // int remove_house = std::stoi(message.substr(ptr+1, message.size()-1-ptr));
                    size_t ptr2 = message.find('_', ptr+1);

                    std::cout<<"Process: "<<id<<" message1: "<<message.substr(ptr+1, ptr2-1-ptr)<<std::endl;
                    int h = std::stoi(message.substr(ptr+1, ptr2-1-ptr));
                    std::cout<<"Process: "<<id<<" h: "<<h<<std::endl;
                    std::cout<<"Process: "<<id<<" message2: "<<message.substr(ptr2+1, message.size()-1-ptr2)<<std::endl;
                    int from_pid = std::stoi(message.substr(ptr2+1, message.size()-1-ptr2));
                    std::cout<<"Process: "<<id<<" from_pid: "<<from_pid<<std::endl;
                    house_rank[from_pid].clear();
                    client_finished[from_pid] = 1;

                    build_erase_pref(h);

                    std::cout<<"Process: "<<id<<": ";
                    for(int i=0;i<house_rank[id].size();i++){
                        std::cout<<house_rank[id][i]<<" ";
                    }
                    std::cout<<std::endl;
                }
            }
        }
    }

    void sendMessage(int target_socket, const std::string& msg) {
        std::string message=msg+"\n";
        send(target_socket, message.c_str(), message.size(), 0);
    }

    void broadcastMessage(const std::string& message) {
        // std::lock_guard<std::mutex> lock(mtx);
        for (int socket : client_sockets) {
            std::cout<<"Process: "<<id<<" broadcast sending: "<<socket<<" msg: "<<message<<std::endl;
            if(socket_id[socket]!=id  && client_finished[socket_id[socket]] != 2) {
                sendMessage(socket, message);
                if(client_finished[socket_id[socket]]==1) client_finished[socket_id[socket]] = 2; 
            }
            std::cout<<"Process: "<<id<<" finish sending: "<<socket<<std::endl;
        }
    }


    // 3 1 0 2
    std::vector<std::vector<int>> house_rank = {{3,1,2,0}, {0, 2, 3, 1}, {0,3, 1, 2}, {2,1,3,0}};
    // std::vector<std::vector<int>> house_rank = {{1,2,0}, {0, 2, 1}, {0, 1, 2}};
// for algorithm 1
    bool active;
    int succ;
    
    std::vector<int> children;
    // std::vector</> parent;
    int parent;
    bool inCycle;
    std::unordered_set<int> received_ok_from; // Tracks children who have sent "ok"
// TTC
    int next; //  the node which holds current top choice of Pi
    bool assigned; // whether be assigned ﬁnal house
    int house; // the house Pi currently holds, intially process i holds house i
    std::map<int, int> pref; // mapping from a house to the node which holds the house.

    // search whether I am in a cycle by checking house_rank
    // void algorithm1() {
    //     while (active == true)
    //     {
    //         /* code */
    //     // Coin Flip Step
    //         std::random_device rd;  // Seed for the random number engine
    //         std::mt19937 gen(rd()); // Mersenne Twister engine
    //         std::uniform_int_distribution<int> dist(0, 3); // Generate 0 to 3
    //         // Simulate coin flip
    //         int coin_flip = dist(gen); // 0 or 1 with equal probability 1 is head, 0 is tail
    //         if(coin_flip == 0){
    //             active = false;
    //         }
    //     // Explore Step
    //     if(active == true) {       
    //     }
    //     // Notify Step
    //     }
    // }

    void algorithm1() {
        if(assigned == true) return;
        next = pref[house_rank[id][0]];
        if(id == 1){
            std::cout<<std::endl;
            std::cout<<"process id: "<<id<<std::endl;
            std::cout<<"next: "<<next<<std::endl;
            for(int i=0;i<house_rank[id].size();i++){
                std::cout<<house_rank[id][i]<<" ";
            }
        }
        if(next == id){
            inCycle = true;
            // assigned=true;
            if(id == 1){
                std::cout<<"id 1 is here\n";
            }
            return;
        }
        int curr = id;
        succ = curr;
        bool isRoot = true; // smallest id in a cycle can be the root
        std::vector<bool>visited(num_processes, 0);
        visited[id] = true;
        do{
            if(succ == pref[house_rank[succ][0]]){ // the other man got its top choice
                std::cout<<"process: "<<id<<" succ: "<<succ<<std::endl;
                for(auto &k: house_rank[id]) std::cout<<k<<" ";
                std::cout<<std::endl;
                break;
            }else{
                curr = succ;
                succ = pref[house_rank[curr][0]];
                std::cout<<"process: "<<id<<" who is succ: "<<succ<<std::endl;

                if(visited[succ] != true) children.emplace_back(succ);

                if(children.size()>4){
                    std::cout<<"issue\n";
                }

                // if(succ == id && (id > curr) && !isRoot) {
                //     parent = curr;
                // }
                // if(succ < id) {
                //     isRoot = false;
                // }

            }

            if(succ == id) { // cycle found and I am in the cycle
                inCycle = true;
                reset_pref(visited, curr, isRoot);
                std::cout<<"process: "<<id<<", parent: "<<parent<<" isroot: "<<isRoot<<std::endl;
                std::cout<<"process: "<<id<<" children:";
                for(auto&k: children) {
                    std::cout<<" "<<k;
                }
                std::cout<<std::endl;
            }
            if(visited[succ] == true) { // cycle found
                // reset_pref(visited, curr);
                break;

            }
            else visited[succ] = true;
            std::cout<<"process: "<<id<<" in alg1 loop\n";
        }while(succ != id); //************* need to solve if there's a branch insert to a cycle -> use visited
        std::cout<<"exit process: "<<id<<" alg1 loop\n";
        std::cout<<"process: "<<id<<" in Cycle: "<<inCycle<<std::endl;

    }

    void reset_pref(std::vector<bool> &visited, int curr, bool &isRoot) {
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
        // while(assigned == false){
            // std::cout<<"process: "<<id<<" ready to accept\n";
            // int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            // if (new_socket < 0) {
            //     perror("Accept failed");
            //     continue;
            // }
            // // std::thread(&Process::handleConnection, this, new_socket).detach();
            // handleConnection(new_socket);
            // // connect_threads.emplace_back(&Process::handleConnection, this, new_socket);
            // std::cout<<"thread: "<<id<<" in listenForMessages\n";
            // if(assigned == true || house_rank[id].size() <= 1) break;

            // if(assigned == true) break;

            // while(1){
                
            //     if(house_rank[id].size()<=1) {
            //         std::cout<<"process: "<<id<<" exit TTC\n";
            //         break;
            //     }
            if(assigned == false) children.clear();

            algorithm1();
            std::cout<<"process: "<<id<<" leave alg1\n";
            succ = next;

            std::string msg;

            if(inCycle == true && assigned==false) {
                int hj = house_rank[id][0];
                house = hj;
                result[id] = house;
                if(id==1 && house==2){
                    std::cout<<"issue\n";
                }

                assigned = true;
                std::cout<<"process: "<<id<<" in cycle: "<<inCycle<<" house: "<<house<<std::endl;

                // Broadcast remove(hi) to all
                msg.clear();
                msg = "Remove_"+std::to_string(house)+"_"+std::to_string(id);
                broadcastMessage(msg);
                // if(children.size()==0) {
                if(parent != -1) {
                    msg.clear();
                    msg = "ok_"+std::to_string(id);
                    // for(auto &k: parent) {
                    //     sendMessage(k, msg);
                    // }
                    std::cout<<"process: "<<id<<" sending OK: "<<msg<<" to parent: "<<parent<<"  socket of Parent: "<<id_socket[parent]<<std::endl;
                    if(parent >= 0 && parent < num_processes) sendMessage(id_socket[parent], msg);
                    // if(parent >= 0 && parent < num_processes) broadcastMessage(msg);
                }
                // inCycle = false;
            }

            std::cout<<"process: "<<id<<" leave TTC with house: "<<house<<std::endl;

            // }
        // }

    }



    void onReceivingOk(int sender_id) {
        // Add the sender to the set of children who sent "ok"
        received_ok_from.insert(sender_id);
        std::cout<<"process: "<<id<<" receive ok size: "<<received_ok_from.size()<<" child size: "<<children.size()<<std::endl;
        // Check if we have received "ok" from all children
        if (received_ok_from.size() == children.size()) {
            onReceivingOkFromAllChildren();
        }
    }

    void onReceivingOkFromAllChildren() {
        // Reset received_ok_from for the next stage
        received_ok_from.clear();

        if (parent == -1) { // root
            std::string msg = "nextStage_";
            broadcastMessage(msg);
            std::cout<<"Process: "<<id<<" broadcast nextStage\n";
        } else {
            sendMessage(id_socket[parent], "ok_"+std::to_string(id));
        }
    }


    int getSenderIdFromMessage(const std::string& message) {
        // This is a placeholder function. In a real implementation,
        // you would need a way to determine the sender ID from the message or socket.
        // For simplicity, this assumes the message format includes the sender ID if needed.
        size_t ptr = message.find('_');
        std::string idd = message.substr(ptr+1, message.size()-1-ptr);
        std::cout<<"Process: "<<id<<" idd: "<<idd<<std::endl;
        return std::stoi(idd); // Replace with actual logic
    }

    void build_erase_pref(int h) {
        for(int i=0;i<num_processes;i++) {
            if(house_rank[i].size()>0) {
                auto it = std::find(house_rank[i].begin(), house_rank[i].end(), h);
                if(it != house_rank[i].end()) house_rank[i].erase(it);
            }
        }
    }

    void check_map() {
        std::cout<<"Process: "<<id<<" : ";
        for(auto &k: id_socket) {
            std::cout<<k.first<<","<<k.second<<"   ";
        }
        std::cout<<std::endl;
    }

};

int main() {
    // const int num_processes = 3;
    // initialize house rank
    
    result.resize(num_processes);

    // Create multiple processes
    std::vector<std::unique_ptr<Process>> processes;
    for (int i = 0; i < num_processes; i++) {
        processes.push_back(std::make_unique<Process>(i));
    }

    // Connect each process to all other processes
    for (int i = 0; i < num_processes; i++) {
        for (int j = 0; j < num_processes; j++) {
            if (i != j) {
                processes[i]->connectTo(j);
            }
        }
        processes[i]->check_map();
    }



    // Start listening for messages in separate threads for each process
    std::vector<std::thread> listen_threads;
    for (int i = 0; i < num_processes; i++) {
        listen_threads.emplace_back(&Process::listenForMessages, processes[i].get());
    }

    // Simulate message sending
    // processes[0]->broadcastMessage("Hello from Process 0");

    // std::vector<std::thread> ttc_threads;
    // for (int i = 0; i < num_processes; i++) {
    //     ttc_threads.emplace_back(&Process::TTC, processes[i].get());
    // }


    // for(int i=0;i<num_processes;i++) {
    //     std::cout<<"process "<<i<<"'s house: "<< result[i]<<std::endl;
    // }

    
    for(auto &p: processes) {
        p->stopListening = true;
    }
    // // Join threads

    // for (auto& t : connect_threads) {
    //     t.join();
    // }

    std::cout<<"ready to release listen_threads\n";
    int cnt_thread=0;
    // for (auto& t : listen_threads) {
    //     if(t.joinable()) {
    //         std::cout<<"join\n";
    //         t.join();
    //     }
    //     std::cout<<"cnt_thread: "<<cnt_thread++<<std::endl;
    // }

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
        std::cout<<"process "<<i<<"'s house: "<< result[i]<<std::endl;
    }

    for(int i=0;i<num_processes;i++) {
        auto &t = listen_threads[i];
        if(t.joinable()){
            std::cout<<"process "<<i<<"'s house: "<< processes[i]->house<<std::endl;        
            t.join();
        }
    }

    // for (auto& t : ttc_threads) {
    //     if(t.joinable()) t.join();
    // }
    // for (auto& t : connect_threads) {
    //     if(t.joinable()) t.join();
    // }


    // for(int i=0;i<num_processes;i++) {
    //     std::cout<<"process "<<i<<"'s house: "<< processes[i]->house<<std::endl;
    // }

    return 0;
}

