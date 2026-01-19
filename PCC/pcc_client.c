#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // for read/write/close
#include <errno.h>      // for errno
#include <string.h>     // for memset
#include <netinet/in.h> // for sockaddr_in, htonl, htons
#include <arpa/inet.h>  // for inet_pton


#define BUF_SIZE 1024*1024

//Helper Functions:
//Reading Loop to Buffer
void read_all(int fd, char* buf, ssize_t bytes_to_read){
    while (bytes_to_read > 0){
        ssize_t bytes_read = read(fd,buf,bytes_to_read);
        if (bytes_read == -1){
            perror("read");
            close(fd);
            free(buf);
            exit(1);
        }
        buf += bytes_read;
        bytes_to_read -= bytes_read;
    }
}

//Writing Loop to Buffer
void write_all(int fd, char* buf, ssize_t bytes_to_write){
    while (bytes_to_write > 0){
        ssize_t bytes_written = write(fd,buf,bytes_to_write);
        if (bytes_written == -1){
            perror("write");
            close(fd);
            exit(1);
        }
        buf += bytes_written;
        bytes_to_write -= bytes_written;
    }
}

int main(int argc, char* argv[]){

    //1. ---Check Arguments Count and Argument Assignment---
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <file_path>\n", argv[0]);
        exit(1);
    }
    
    char* serverIP = argv[1];
    char* serverPort = argv[2];
    char* filePath = argv[3];

    int retval = 0;

    //2. ---Initialize TCP Socket---
    struct sockaddr_in serverAddress = {0};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(atoi(serverPort));
    //to avoid warnings
    int fd = -1;
    int clientSocket = -1;
    char* buf = NULL;

    if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket");
        retval = 1;
        goto cleanup;
    }

    if ((fd = open(filePath, O_RDONLY)) == -1){
        perror("open");
        retval = 1;
        goto cleanup;
    }

    //3. ---Send Connection Request---
    if (inet_pton(AF_INET, serverIP, &serverAddress.sin_addr) != 1){
        perror("inet_pton");
        retval = 1;
        goto cleanup;
    }
    
    if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0){
        perror("connect");
        retval = 1;
        goto cleanup;
    }

    //4. ---Send N (File Size) to the Server---
    struct stat st;

    if (fstat(fd, &st) == -1){
        perror("fstat");
        retval = 1;
        goto cleanup;
    }
    long N = st.st_size;
    uint32_t sendSize = htonl(N);

    write_all(clientSocket, (char*)&sendSize, sizeof(sendSize));
    buf = malloc(BUF_SIZE);
    if (buf == NULL){
        perror("buffer");
        retval = 1;
        goto cleanup;
    }

    //5. ---Stream the File's Data---
    ssize_t bytes_read;

    while ((bytes_read = read(fd,buf, BUF_SIZE)) > 0){
        write_all(clientSocket, buf, bytes_read);
        }
    
    if (bytes_read == -1){
        perror("read");
        retval = 1;
        goto cleanup;
    }

    //6. ---Waiting for Server to Return the Count---
    uint32_t C;

    read_all(clientSocket,(char*)&C,4);
    uint32_t result = ntohl(C);
    printf("# of printable characters: %u\n",result);

    //7. ---Shutdown and Exit---
    cleanup:
        if (fd != -1) {close(fd);}
        if (clientSocket != -1) {close(clientSocket);}
        if (buf) {free(buf);}
        exit(retval);
}