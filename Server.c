#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/sha.h>
#include <dirent.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define DB_NAME "clients.db"

// Function prototypes
void init_db();
char* run_length_decode(char* input);
int sign_up(const char* username, const char* password);
int login(const char* username, const char* password);
void* handle_client(void* arg);
void create_user_directory(int user_id);
void send_response(int client_socket, const char* message);
void handle_upload(int client_socket, int user_id, const char* filename);
void handle_view(int client_socket, int user_id);
void handle_download(int client_socket, int user_id, const char* filename);

// Mutex for thread safety
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

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

// Initialize SQLite DB and create table if not exists
void init_db() {
    sqlite3* db;
    char* err_msg = 0;
    int rc = sqlite3_open(DB_NAME, &db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS clients ("
                      "user_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "username TEXT UNIQUE,"
                      "password_hash TEXT,"
                      "created_at TEXT);";
    
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        exit(1);
    }
    
    sqlite3_close(db);
}

// Hash password using SHA256
char* hash_password(const char* password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)password, strlen(password), hash);

    // Convert hash to hex string
    char* hash_str = malloc(2 * SHA256_DIGEST_LENGTH + 1);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(&hash_str[i * 2], "%02x", hash[i]);
    }
    return hash_str;
}

// Send response message to client
void send_response(int client_socket, const char* message) {
    send(client_socket, message, strlen(message), 0);
}

// Sign-up function: stores a new user in the database
int sign_up(const char* username, const char* password) {
    sqlite3* db;
    sqlite3_open(DB_NAME, &db);

    char* err_msg = 0;
    char* hashed_password = hash_password(password);

    char sql[BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "INSERT INTO clients (username, password_hash, created_at) "
                               "VALUES ('%s', '%s', datetime('now'));", username, hashed_password);

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    free(hashed_password);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return -1;  // Sign-up failed
    }

    int user_id = sqlite3_last_insert_rowid(db);  // Get the user_id of the new user
    sqlite3_close(db);
    return user_id;  // Return new user's ID
}

// Login function: validates username and password
int login(const char* username, const char* password) {
    sqlite3* db;
    sqlite3_open(DB_NAME, &db);

    char* hashed_password = hash_password(password);
    char sql[BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "SELECT user_id FROM clients WHERE username='%s' AND password_hash='%s';", 
             username, hashed_password);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    free(hashed_password);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int user_id = sqlite3_column_int(stmt, 0);  // Get the user_id if login is successful
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return user_id;
    } else {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;  // Invalid credentials
    }
}

// Create a directory for the user if it doesn't exist
void create_user_directory(int user_id) {
    char dir_path[BUFFER_SIZE];
    snprintf(dir_path, sizeof(dir_path), "uploads/%d", user_id);
    mkdir(dir_path, 0777);
}

// Handle file upload from client
void handle_upload(int client_socket, int user_id, const char* filename) {
    char filepath[BUFFER_SIZE];
    snprintf(filepath, sizeof(filepath), "uploads/%d/%s", user_id, filename);
    printf(filepath);
    FILE* file = fopen(filepath, "wb");
    if (file == NULL) {
        send_response(client_socket, "$UPLOAD_FAILURE$");
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes_received;
    while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        char* decoded = run_length_decode(buffer);
        fwrite(decoded, 1, strlen(decoded), file);
        free(decoded);
        if (bytes_received < BUFFER_SIZE) {
            break; // End of file
        }
    }
    
    fclose(file);
    send_response(client_socket, "$UPLOAD_SUCCESS$");
    printf("Server: File uploaded successfully to %s\n", filepath);
}

// Handle file viewing (list files in user's directory)
void handle_view(int client_socket, int user_id) {
    char dir_path[BUFFER_SIZE];
    snprintf(dir_path, sizeof(dir_path), "uploads/%d", user_id);

    DIR* dir = opendir(dir_path);
    if (dir == NULL) {
        send_response(client_socket, "$VIEW_FAILURE$");
        return;
    }

    struct dirent* entry;
    char file_list[BUFFER_SIZE] = "";
    struct stat file_stat;
    char full_path[BUFFER_SIZE];
    
    while ((entry = readdir(dir)) != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        // Use stat() to check if the entry is a regular file
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            strcat(file_list, entry->d_name);
            strcat(file_list, "\n");
        }
    }
    closedir(dir);

    if (strlen(file_list) == 0) {
        strcpy(file_list, "No files found\n");
    }

    send_response(client_socket, file_list);
}

// Handle file download request
void handle_download(int client_socket, int user_id, const char* filename) {
    char filepath[BUFFER_SIZE];
    snprintf(filepath, sizeof(filepath), "uploads/%d/%s", user_id, filename);

    FILE* file = fopen(filepath, "rb");
    if (file == NULL) {
        send_response(client_socket, "$DOWNLOAD_FAILURE$");
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }
    
    fclose(file);
    send_response(client_socket, "$DOWNLOAD_SUCCESS$");
    printf("Server: File downloaded: %s\n", filepath);
}

// Handle client connection (sign-up, login, upload, view, download)
void* handle_client(void* arg) {
    int client_socket = *((int*)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    char username[BUFFER_SIZE], password[BUFFER_SIZE], filename[BUFFER_SIZE];
    
    int user_id = -1; // Keep track of logged-in user
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        recv(client_socket, buffer, BUFFER_SIZE, 0);
        
        if (strstr(buffer, "$SIGNUP$") != NULL) {
            sscanf(buffer, "$SIGNUP$%[^$]$%[^$]$", username, password);
            printf("Server: Signing up user with username: %s\n", username);
            
            pthread_mutex_lock(&mutex);
            user_id = sign_up(username, password);
 pthread_mutex_unlock(&mutex);
            if (user_id != -1) {
                create_user_directory(user_id);  // Create user's directory on successful sign-up
                send_response(client_socket, "$SIGNUP_SUCCESS$");
                printf("Server: Sign-up success for user: %s\n", username);
            } else {
                send_response(client_socket, "$SIGNUP_FAILURE$");
                printf("Server: Sign-up failed for user: %s\n", username);
            }
        } else if (strstr(buffer, "$LOGIN$") != NULL) {
            sscanf(buffer, "$LOGIN$%[^$]$%[^$]$", username, password);
            printf("Server: Logging in user with username: %s\n", username);
            
            pthread_mutex_lock(&mutex);
            user_id = login(username, password);
            pthread_mutex_unlock(&mutex);
            if (user_id != -1) {
                send_response(client_socket, "$LOGIN_SUCCESS$");
                printf("Server: Login success for user: %s\n", username);
            } else {
                send_response(client_socket, "$LOGIN_FAILURE$");
                printf("Server: Login failed for user: %s\n", username);
            }
        } else if (strstr(buffer, "$UPLOAD$") != NULL && user_id != -1) {
            sscanf(buffer, "$UPLOAD$%[^$]$", filename);
            printf("Server: File upload requested: %s\n", filename);
            handle_upload(client_socket, user_id, filename);
        } else if (strstr(buffer, "$VIEW$") != NULL && user_id != -1) {
            printf("Server: File view requested\n");
            handle_view(client_socket, user_id);
        } else if (strstr(buffer, "$DOWNLOAD$") != NULL && user_id != -1) {
            sscanf(buffer, "$DOWNLOAD$%[^$]$", filename);
            printf("Server: File download requested: %s\n", filename);
            handle_download(client_socket, user_id, filename);
        }
    }
    close(client_socket);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);

    init_db();  // Initialize the SQLite database

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Server: Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the port
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Server: Bind failed");
        exit(1);
    }

    // Listen for connections
    if (listen(server_socket, 3) < 0) {
        perror("Server: Listen failed");
        exit(1);
    }

    printf("Server is running on port %d\n", PORT);

    // Accept incoming connections
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) {
            perror("Server: Accept failed");
            continue;
        }

        printf("Server: Client connected\n");

        // Handle the client in a separate thread
        pthread_t thread;
        int* client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = client_socket;
        pthread_create(&thread, NULL, handle_client, client_socket_ptr);
    }

    close(server_socket);
    return 0;
}