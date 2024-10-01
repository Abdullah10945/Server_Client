#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Function prototypes
char* run_length_encode(const char* input);
char* run_length_decode(char* input);
void sign_up(int sock);
void login(int sock);
void handle_file_operations(int sock);
void receive_server_response(int sock);
void client_menu(int sock);

// Run-Length Encoding function
char* run_length_encode(const char* input) {
    int len = strlen(input);
    char* encoded = malloc(len * 2 + 1); // Worst case, no compression
    int count = 1, j = 0;

    for (int i = 0; i < len; i++) {
        if (input[i] == input[i + 1]) {
            count++;
        } else {
            j += sprintf(&encoded[j], "%d%c", count, input[i]);
            count = 1;
        }
    }
    encoded[j] = '\0';
    return encoded;
}

// Run-Length Decoding function
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

// Function to receive and display server responses
void receive_server_response(int sock) {
    char server_response[BUFFER_SIZE];
    int bytes_received = recv(sock, server_response, sizeof(server_response) - 1, 0);
    if (bytes_received > 0) {
        server_response[bytes_received] = '\0';  // Null-terminate response
        printf("Server response: %s\n", server_response);
    } else {
        perror("Receive failed");
    }
}

// Function to sign up a new client
void sign_up(int sock) {
    char username[BUFFER_SIZE], password[BUFFER_SIZE];

    while (1) {
        printf("Enter username for sign-up: ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = 0;  // Remove newline character

        printf("Enter password for sign-up: ");
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = 0;  // Remove newline character

        char sign_up_request[BUFFER_SIZE];
        snprintf(sign_up_request, sizeof(sign_up_request), "$SIGNUP$%s$%s$", username, password);
        
        send(sock, sign_up_request, strlen(sign_up_request), 0);
        
        // Receive server response and store it in a variable
        char server_response[BUFFER_SIZE];
        int bytes_received = recv(sock, server_response, sizeof(server_response) - 1, 0);
        if (bytes_received > 0) {
            server_response[bytes_received] = '\0';  // Null-terminate response
            printf("Server response: %s\n", server_response);  // Print server response
        } else {
            perror("Receive failed");
            continue; // Optionally continue to retry sign-up
        }

        // Check server response for successful sign-up
        if (strstr(server_response, "$SIGNUP_SUCCESS$")) {
            printf("Sign-up successful!\n");
            break; // Exit loop if sign-up was successful
        } else {
            printf("Sign-up failed: %s\n", server_response);  // Print specific failure message
        }
    }
}

// Function to log in an existing client
void login(int sock) {
    char username[BUFFER_SIZE], password[BUFFER_SIZE];
    char server_response[BUFFER_SIZE]; // To hold the server's response

    while (1) {
        printf("Enter username for login: ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = 0;  // Remove newline character

        printf("Enter password for login: ");
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = 0;  // Remove newline character

        char login_request[BUFFER_SIZE];
        snprintf(login_request, sizeof(login_request), "$LOGIN$%s$%s$", username, password);
        
        send(sock, login_request, strlen(login_request), 0);
         // Receive server response and store it in a variable
        char server_response[BUFFER_SIZE];
        int bytes_received = recv(sock, server_response, sizeof(server_response) - 1, 0);
        if (bytes_received > 0) {
            server_response[bytes_received] = '\0';  // Null-terminate response
            printf("Server response: %s\n", server_response);  // Print server response
        } else {
            perror("Receive failed");
            continue; // Optionally continue to retry sign-up
        }

        // Now check the server response for successful login
        // Note: You should modify the receive_server_response function to return the response string.
         // Check server response for successful sign-up
        if (strstr(server_response, "$LOGIN_SUCCESS$")) {
            printf("Logged-in successful!\n");
            break; // Exit loop if sign-up was successful
        } else {
            printf("Log-in failed: %s\n", server_response);  // Print specific failure message
        }
    }
}

// Function to handle file upload, view, and download operations
void handle_file_operations(int sock) {
    int choice;
    printf("Choose an operation:\n1. Upload file\n2. View files\n3. Download file\nEnter your choice: ");
    scanf("%d", &choice);
    getchar();  // Consume leftover newline

    if (choice == 1) {
        char file_path[BUFFER_SIZE];
        printf("Enter the path of the file to upload: ");
        fgets(file_path, sizeof(file_path), stdin);
        file_path[strcspn(file_path, "\n")] = 0;  // Remove newline

        char upload_request[BUFFER_SIZE];
        snprintf(upload_request, sizeof(upload_request), "$UPLOAD$%s$", file_path);
        send(sock, upload_request, strlen(upload_request), 0);
        receive_server_response(sock);
        
        // Send file (simplified logic, you can improve file sending here)
        FILE* file = fopen(file_path, "rb");
        if (file != NULL) {
            char buffer[BUFFER_SIZE];
            while (fread(buffer, 1, BUFFER_SIZE, file)) {
                char* encoded = run_length_encode(buffer);
                send(sock, encoded, strlen(encoded), 0);
                free(encoded);
            }
            fclose(file);
            printf("File uploaded successfully!\n");
        } else {
            printf("File open error!\n");
        }
    } else if (choice == 2) {
        char view_request[BUFFER_SIZE];
        snprintf(view_request, sizeof(view_request), "$VIEW$");
        send(sock, view_request, strlen(view_request), 0);
        receive_server_response(sock);
    } else if (choice == 3) {
        char file_name[BUFFER_SIZE];
        printf("Enter the file name to download: ");
        fgets(file_name, sizeof(file_name), stdin);
        file_name[strcspn(file_name, "\n")] = 0;  // Remove newline

        char download_request[BUFFER_SIZE];
        snprintf(download_request, sizeof(download_request), "$DOWNLOAD$%s$", file_name);
        send(sock, download_request, strlen(download_request), 0);
        receive_server_response(sock);
        
        // Receive and decode the file
        FILE* file = fopen(file_name, "wb");
        if (file != NULL) {
            char buffer[BUFFER_SIZE];
            while (recv(sock, buffer, BUFFER_SIZE, 0) > 0) {
                char* decoded = run_length_decode(buffer);
                fwrite(decoded, 1, strlen(decoded), file);
                free(decoded);
            }
            fclose(file);
            printf("File downloaded successfully!\n");
        } else {
            printf("File open error!\n");
        }
    } else {
        printf("Invalid choice\n");
    }
}

int main() {
    int sock;
    struct sockaddr_in server_addr;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Could not create socket\n");
        return -1;
    }

    // Set up the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }

    // Main client menu
    client_menu(sock);

    // Close the connection
    close(sock);
    return 0;
}

// Function to manage the main menu for the client
void client_menu(int sock) {
    int choice;
    
    while (1) {
        printf("1. Sign-up \n2. Login\nEnter your choice: ");
        scanf("%d", &choice);
        getchar();  // Consume leftover newline

        if (choice == 1) {
            sign_up(sock);
            break; // Exit the loop after successful sign-up
        } else if (choice == 2) {
            login(sock);
            break; // Exit the loop after successful login
        } else {
            printf("Invalid choice, please try again.\n");
        }
    }

    // Handle file operations (upload, view, download) after successful login/sign-up
    handle_file_operations(sock);
}