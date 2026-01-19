//#define _POSIX_C_SOURCE 200809L //not needed but I am using it because I get compilation error on sa

#include <signal.h>
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


#define P_MIN 32
#define P_MAX 126
#define P_COUNT (P_MAX - P_MIN + 1)
#define BUF_SIZE 1024*1024
#define MIN(a,b) (((a)<(b))?(a):(b))


/*
--- Global Data Structures ---
pcc_total: Global tally of printable characters across all sessions.
sigint_received: Atomic flag to handle SIGINT (Ctrl+C) safely.
client_served: Counter for how many clients have been served.
*/

uint32_t pcc_total[P_COUNT] = {0};
volatile sig_atomic_t sigint_received = 0;
uint32_t clients_served = 0;

//Function to flag sigints
void handler(int sig){sigint_received = 1;}

int main(int argc, char* argv[]){
    
    //1. ---Check Arguments Count and Argument Assignment---
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
        exit(1);
    }
    char* serverPort = argv[1];
    
    int retval = 0;
    // to avoid warnings
    int serverSocket = -1;
    int connectfd = -1;
    char* buf = NULL;
    
    //2. ---Initialization---

    //  2(a). Register the SIGINT handler
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction");
        retval = 1;
        goto cleanup;
    }

    //  2(b). Initialize TCP socket
    struct sockaddr_in serverAddress = {0};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(atoi(serverPort));

    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket");
        retval = 1;
        goto cleanup;
    }

    //  2(c). Set SO_REUSEADDR to allow immediate restart
    int enable = 1;
    if ((setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,&enable,sizeof(enable))) < 0){
        perror("setsockopt");
        retval = 1;
        goto cleanup;
    }

    //  2(d). Bind to INADDR_ANY and Listen
    if ((bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress))) < 0){
        perror("bind");
        retval = 1;
        goto cleanup;
    }
    if ((listen(serverSocket, 10)) < 0){
        perror("listen");
        retval = 1;
        goto cleanup;
    }
    

    //3. ---Connection Handling Loop---

    //Loops runs until SIGINT recieved
    while(1){

        //  3(a). Exit condition - recieved SIGINT
        if (sigint_received){break;}

        //  3(b). Client setup attempt
        struct sockaddr_in clientAddress = {0};
        clientAddress.sin_family = AF_INET;
        socklen_t addr_len = sizeof(clientAddress);

        //  3(c). Connection attempt - if accept succeeds we move forward
        if((connectfd = accept(serverSocket, (struct sockaddr*)&clientAddress, &addr_len)) < 0){
            //  This is why while(1) is being used, if accepts failed we check if it's becuase of SIGINT
            if (errno == EINTR){
                continue;
            }
            else{
                perror("accept");
                retval = 1;
                goto cleanup;
            }
        }

        //4. ---Client Handshake & Processing

        //  4(a). Reading N (file size)
        uint32_t N;
        int client_error = 0;
        char* n_ptr = (char*)&N;
        int n_left = 4;
        while (n_left > 0) {
            int rd = read(connectfd, n_ptr, n_left);
            if (rd <= 0){
                client_error = 1;
                break;
            }
            n_ptr += rd;
            n_left -= rd;
        }
        if (client_error) {
            fprintf(stderr, "Error reading N or EOF\n");
            close(connectfd);
            continue;
        }
        N = ntohl(N);

        //  4(b). Initializing buffer
        buf = malloc(BUF_SIZE);
        if (buf == NULL){
            perror("buffer");
            retval = 1;
            goto cleanup;
        }

        //  4(c). Initializing local counter and stats
        //        This because once we have a valid client connection we must finish reading from it even if sigint is recieved
        uint32_t current_pcc_counter = 0;
        uint32_t local_stats[P_COUNT] = {0};
        //memset(local_stats,0,sizeof(local_stats));

        uint32_t bytes_left = N;
        int bytes_read;
        
        //5. ---Reading Bytes & Counting Printables
        // Loop runs until there are no bytes left
        while (bytes_left > 0){

            //  5(a). Reading attempt
            if((bytes_read = read(connectfd, buf,MIN(bytes_left,BUF_SIZE))) <= 0){
                if (bytes_read < 0 && errno == EINTR){
                    continue;
                }
                fprintf(stderr,"error reading data or unexpected EOF");
                client_error = 1;
                break;
            }

            //  5(b). Local counter and stats update
            for (int i = 0 ; i < bytes_read ; i++){
                if ((P_MIN <= buf[i]) && (buf[i] <= P_MAX)){
                    current_pcc_counter++;
                    local_stats[buf[i]-P_MIN]++;
                }
            }
            bytes_left -= bytes_read;
        }// End of inner while loop

        //6. ---Sending Count Back to the Client
        //If there was no client error 
        if (!client_error){
            current_pcc_counter = htonl(current_pcc_counter);
            char* c_ptr = (char*)&current_pcc_counter;
            int c_left = 4;
            int write_fail = 0;
            
            while (c_left > 0) {
                int wr = write(connectfd, c_ptr, c_left);
                if (wr <= 0) { write_fail = 1; break; }
                c_ptr += wr;
                c_left -= wr;
            }

            //If writing was successful - update global stats and increment the amount of clients served
            if(!write_fail){
                for(int i = 0 ; i < P_COUNT ; i++){
                    pcc_total[i] += local_stats[i];
                }
                clients_served++;
            }
        }

        //Closing current buffer and connection, new ones will be allocated if a new connection with a client is reached
        close(connectfd);
        connectfd = -1;
        free(buf);
    }//End of outer while loop

    //7. ---Shutdown & Statistics
    //  7(a). print the number of times each printable character was observed
    for (int i = 0; i < P_COUNT; i++) {
        if (pcc_total[i] > 0) {
            printf("char '%c': %u times\n", (i + P_MIN), pcc_total[i]);
        }
    }

    //  7(b). Print the amount of cliented served and exit
    printf("Served %u client(s) successfully\n", clients_served);

    cleanup:
        if (serverSocket != -1) {close(serverSocket);}
        if (connectfd != -1) {close(connectfd);}
        exit(retval);
}