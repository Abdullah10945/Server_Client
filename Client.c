#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Run-Length Encoding function (same as before)
char* run_length_encode(char* input) {
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

// Function to send file data to the server (same as before)
void send_file_data(int sock, const char* file_path) {
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        perror("File open error");
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Read the file, encode it, and send it to the server
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE - 1, file)) > 0) {
        buffer[bytes_read] = '\0'; // Null-terminate the buffer
        char* encoded_data = run_length_encode(buffer); // Encode file data
        send(sock, encoded_data, strlen(encoded_data), 0);
        free(encoded_data);
    }

    fclose(file);
}

// Function to download a file from the server
void download_file(int sock, const char* file_name) {
    char buffer[BUFFER_SIZE];
    FILE* file = fopen(file_name, "w");
    if (file == NULL) {
        perror("File open error");
        return;
    }

    int bytes_received;
    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
    }

    fclose(file);
    printf("File downloaded successfully!\n");
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char server_reply[BUFFER_SIZE];
    char file_path[BUFFER_SIZE];  // Buffer to store user input
    char file_name[BUFFER_SIZE];  // Buffer to store file name for download

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Could not create socket\n");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        return -1;
    }

    // Display a menu for the user to choose an operation
    int choice;
    printf("Choose an operation:\n");
    printf("1. Upload a file\n");
    printf("2. View files\n");
    printf("3. Download a file\n");
    printf("Enter your choice: ");
    scanf("%d", &choice);
    getchar();  // To consume the newline character left by scanf

    if (choice == 1) {
        // Ask the user for the file path for upload
        printf("Enter the path to the file you want to upload: ");
        fgets(file_path, sizeof(file_path), stdin);
        file_path[strcspn(file_path, "\n")] = 0;  // Remove newline character

        // Send upload request
        char upload_request[BUFFER_SIZE];
        snprintf(upload_request, sizeof(upload_request), "$UPLOAD$%s$", file_path);
        send(sock, upload_request, strlen(upload_request), 0);

        // Receive server response
        if (recv(sock, server_reply, BUFFER_SIZE, 0) < 0) {
            perror("Receive failed");
            return -1;
        }
        printf("Server reply: %s\n", server_reply);

        // If server is ready for file upload, send the file data
        if (strstr(server_reply, "$SUCCESS$") != NULL) {
            send_file_data(sock, file_path);
        }

    } else if (choice == 2) {
        // Send a request to view files
        char view_request[BUFFER_SIZE];
        snprintf(view_request, sizeof(view_request), "$VIEW$");
        send(sock, view_request, strlen(view_request), 0);

        // Receive the list of files from the server
        if (recv(sock, server_reply, BUFFER_SIZE, 0) < 0) {
            perror("Receive failed");
            return -1;
        }
        printf("Available files:\n%s\n", server_reply);

    } else if (choice == 3) {
        // Ask the user for the file name to download
        printf("Enter the file name you want to download: ");
        fgets(file_name, sizeof(file_name), stdin);
        file_name[strcspn(file_name, "\n")] = 0;  // Remove newline character

        // Send download request
        char download_request[BUFFER_SIZE];
        snprintf(download_request, sizeof(download_request), "$DOWNLOAD$%s$", file_name);
        send(sock, download_request, strlen(download_request), 0);

        // Receive server response
        if (recv(sock, server_reply, BUFFER_SIZE, 0) < 0) {
            perror("Receive failed");
            return -1;
        }
        printf("Server reply: %s\n", server_reply);

        // If server is ready for file download, receive the file data
        if (strstr(server_reply, "$SUCCESS$") != NULL) {
            download_file(sock, file_name);
        }
    } else {
        printf("Invalid choice. Exiting.\n");
    }

    // Close the connection
    close(sock);
    return 0;
}
