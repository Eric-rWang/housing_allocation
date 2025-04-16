#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>

const int BASE_PORT = 8080; // Starting port number
const int BUFFER_SIZE = 1024;

std::mutex mtx;


class Process {
public:
    int id;
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    std::vector<int> client_sockets;

    Process(int id) : id(id) {
        setupServer();
    }

    void setupServer() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
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
        if (listen(server_fd, 3) < 0) {
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }
        std::cout << "Process " << id << " listening on port " << BASE_PORT + id << "\n";
    }

    void connectTo(int target_process_id) {
        int sock = 0;
        struct sockaddr_in serv_addr;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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
    }

    void listenForMessages() {
        while (true) {
            int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                perror("Accept failed");
                continue;
            }
            std::thread(&Process::handleConnection, this, new_socket).detach();
        }
    }

    void handleConnection(int socket) {
        char buffer[BUFFER_SIZE] = {0};
        int valread = read(socket, buffer, BUFFER_SIZE);
        std::string message(buffer);
        
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Process " << id << " received: " << message << std::endl;
        close(socket);  // Close the socket after handling the message
    }

    void sendMessage(int target_socket, const std::string& message) {
        send(target_socket, message.c_str(), message.size(), 0);
    }

    void broadcastMessage(const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);
        for (int socket : client_sockets) {
            sendMessage(socket, message);
        }
    }
};

int main() {
    const int num_processes = 3;

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
    }

    // Start listening for messages in separate threads for each process
    std::vector<std::thread> listen_threads;
    for (int i = 0; i < num_processes; i++) {
        listen_threads.emplace_back(&Process::listenForMessages, processes[i].get());
    }

    // Simulate message sending
    processes[0]->broadcastMessage("Hello from Process 0");

    // Join threads
    for (auto& t : listen_threads) {
        t.join();
    }

    return 0;
}

