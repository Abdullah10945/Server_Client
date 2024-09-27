#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#define PORT 8080
#define STORAGE_LIMIT 10240 // 10 KB
#define BUFFER_SIZE 1024
#define MAX_PATH_SIZE 2048 // Updated buffer size to handle large paths

pthread_mutex_t lock;

void* handle_client(void* client_socket);

long get_file_size(const char* filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

int get_total_storage_usage() {
    DIR* dir = opendir("uploads");
    struct dirent* entry;
    int total_size = 0;

    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            char filepath[MAX_PATH_SIZE]; // Increased buffer size for file path
            snprintf(filepath, sizeof(filepath), "uploads/%s", entry->d_name);
            
            struct stat file_stat;
            if (stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) { // Check if it's a regular file
                total_size += get_file_size(filepath);
            }
        }
        closedir(dir);
    }
    return total_size;
}

char* run_length_decode(char* input) {
    int len = strlen(input);
    char* decoded = malloc(len * 2 + 1); // Decoded data
    int count, j = 0;

    for (int i = 0; i < len; i += 2) {
        count = input[i] - '0'; // Number of repeats
        char c = input[i + 1];  // Character
        for (int k = 0; k < count; k++) {
            decoded[j++] = c;
        }
    }
    decoded[j] = '\0';
    return decoded;
}

void get_client_id(int client_socket, char* client_id, size_t size) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &addr_len);

    // Using IP address and port as client identifier
    snprintf(client_id, size, "%s_%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void handle_upload(int client_socket, char* file_path) {
    char buffer[BUFFER_SIZE];
    int received_size;
    FILE* file;
    char client_id[MAX_PATH_SIZE];  // To store client identifier
    char dir_path[MAX_PATH_SIZE];   // To store client directory path
    char file_name[MAX_PATH_SIZE];  // To store full file path

    // Generate client ID based on IP and port or some unique identifier
    get_client_id(client_socket, client_id, sizeof(client_id));

    // Create directory for client if it doesn't exist
    int result = snprintf(dir_path, sizeof(dir_path), "uploads/%s", client_id);
    if (result >= sizeof(dir_path)) {
        send(client_socket, "$FAILURE$PATH_TOO_LONG$", strlen("$FAILURE$PATH_TOO_LONG$"), 0);
        return;
    }
    if (mkdir(dir_path, 0755) == -1 && errno != EEXIST) {
        perror("Error creating client directory");
        send(client_socket, "$FAILURE$DIR_CREATE_ERROR$", strlen("$FAILURE$DIR_CREATE_ERROR$"), 0);
        return;
    }

    // Build the full path for the file in the client's directory
    printf("file_path: %s\n", file_path);
    printf("dir_path: %s\n", dir_path);
    printf("file_name_only: %s\n", file_name);
    snprintf(file_name, sizeof(file_name), "%s/%s", dir_path, strrchr(file_path, '/') + 1);

    if (result >= sizeof(file_name)) {
    send(client_socket, "$FAILURE$FILE_NAME_TOO_LONG$", strlen("$FAILURE$FILE_NAME_TOO_LONG$"), 0);
    return;
    }
    if (get_total_storage_usage() + BUFFER_SIZE > STORAGE_LIMIT) {
        send(client_socket, "$FAILURE$LOW_SPACE$", strlen("$FAILURE$LOW_SPACE$"), 0);
        return;
    } else {
        send(client_socket, "$SUCCESS$", strlen("$SUCCESS$"), 0);
    }

    // Open file for writing
    file = fopen(file_name, "w");
    if (file == NULL) {
        perror("File open error");
        send(client_socket, "$FAILURE$FILE_OPEN_ERROR$", strlen("$FAILURE$FILE_OPEN_ERROR$"), 0);
        return;
    }

    // Receive file data and write to file
    while ((received_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[received_size] = '\0';
        char* decoded_data = run_length_decode(buffer); // Decode the data
        fwrite(decoded_data, sizeof(char), strlen(decoded_data), file);
        free(decoded_data);
    }

    fclose(file);
    send(client_socket, "$SUCCESS$", strlen("$SUCCESS$"), 0);
}

void handle_view(int client_socket) {
    DIR* dir;
    struct dirent* entry;
    char response[BUFFER_SIZE] = "";
    char client_id[MAX_PATH_SIZE];  // To store client identifier
    char dir_path[MAX_PATH_SIZE];   // To store client directory path

    // Get client ID based on IP and port or some unique identifier
    get_client_id(client_socket, client_id, sizeof(client_id));

    // Open the client's directory
    int result = snprintf(dir_path, sizeof(dir_path), "uploads/%s", client_id);
    if (result >= sizeof(dir_path)) {
    send(client_socket, "$FAILURE$PATH_TOO_LONG$", strlen("$FAILURE$PATH_TOO_LONG$"), 0);
    return;
    }
    dir = opendir(dir_path);

    if (dir == NULL) {
        send(client_socket, "$FAILURE$NO_CLIENT_DATA$", strlen("$FAILURE$NO_CLIENT_DATA$"), 0);
        return;
    }

    // Read the files in the client's directory
    while ((entry = readdir(dir)) != NULL) {
        char filepath[MAX_PATH_SIZE];  // Increased buffer size for file path

        result = snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
        if (result >= sizeof(filepath)) {
            send(client_socket, "$FAILURE$FILE_PATH_TOO_LONG$", strlen("$FAILURE$FILE_PATH_TOO_LONG$"), 0);
            return;
        }

        struct stat file_stat;
        if (stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) { // Check if it's a regular file
            char file_info[MAX_PATH_SIZE];  // Increased buffer size for file info
            result = snprintf(file_info, sizeof(file_info), "File: %s, Size: %ld bytes\n", entry->d_name, file_stat.st_size);
            if (result >= sizeof(filepath)) {
            send(client_socket, "$FAILURE$FILE_PATH_TOO_LONG$", strlen("$FAILURE$FILE_PATH_TOO_LONG$"), 0);
            return;
        }
            strcat(response, file_info);
        }
    }
    closedir(dir);

    if (strlen(response) == 0) {
        send(client_socket, "$FAILURE$NO_CLIENT_DATA$", strlen("$FAILURE$NO_CLIENT_DATA$"), 0);
    } else {
        send(client_socket, response, strlen(response), 0);
    }
}

void handle_download(int client_socket, char* file_name) {
    char buffer[BUFFER_SIZE];
    FILE* file;
    char client_id[MAX_PATH_SIZE];  // To store client identifier
    char file_path[MAX_PATH_SIZE];  // To store file path inside the client's directory

    // Get client ID based on IP and port or some unique identifier
    get_client_id(client_socket, client_id, sizeof(client_id));

    // Build the file path within the client's directory
   int result = snprintf(file_path, sizeof(file_path), "uploads/%s/%s", client_id, file_name);
    if (result >= sizeof(file_path)) {
        send(client_socket, "$FAILURE$FILE_PATH_TOO_LONG$", strlen("$FAILURE$FILE_PATH_TOO_LONG$"), 0);
        return;
    }

    // Try to open the file for reading
    file = fopen(file_path, "r");
    if (file == NULL) {
        send(client_socket, "$FAILURE$FILE_NOT_FOUND$", strlen("$FAILURE$FILE_NOT_FOUND$"), 0);
        return;
    }

    // Read the file and send it to the client
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        send(client_socket, buffer, strlen(buffer), 0);
    }
    fclose(file);
}

void* handle_client(void* client_socket) {
    //int sock = ((int)client_socket);
    //int sock = client_socket;  // Remove the cast to (int)
    int sock = *(int*)client_socket;  // Cast from void* to int*

    char buffer[BUFFER_SIZE];
    int received_size;

    while ((received_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[received_size] = '\0';
        if (strstr(buffer, "$UPLOAD$") == buffer) {
            char* file_path = strtok(buffer + 8, "$");
            handle_upload(sock, file_path);
        } else if (strstr(buffer, "$VIEW$") == buffer) {
            handle_view(sock);
        } else if (strstr(buffer, "$DOWNLOAD$") == buffer) {
            char* file_name = strtok(buffer + 10, "$");
            handle_download(sock, file_name);
        }
    }

    close(sock);
    free(client_socket);
    return NULL;
}

int main() {
    int server_fd, client_socket, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    // Suppress error if directory already exists
    if (mkdir("uploads", 0777) == -1 && errno != EEXIST) {
        perror("Error creating uploads directory");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_init(&lock, NULL);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    listen(server_fd, 5);
    printf("Server listening on port %d\n", PORT);

    while ((client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len))) {
        printf("Client connected\n");
        pthread_t client_thread;
        new_sock = malloc(sizeof(int));
        *new_sock = client_socket;
        pthread_create(&client_thread, NULL, handle_client, (void*)new_sock);
    }

    pthread_mutex_destroy(&lock);
    return 0;
}