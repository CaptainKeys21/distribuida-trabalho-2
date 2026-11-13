#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include "defines.h"
#include "cinema.h"

void setSocketTimeout(long usec, int socket);

int main(int argc, char** args) {

    if (argc < 4) {
        printf("Format: ./manager <IP> <Port> <name>\n");
        return -1;
    }

    int serverSocket;
    int socketLength;
    struct sockaddr_in address;
    int result;

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(args[1]);
    address.sin_port = htons(atoi(args[2]));
    socketLength = sizeof(address);
    result = connect(serverSocket, (struct sockaddr *)&address, socketLength);
    if(result == -1) {
        printf("Failure to connect! Error: %d\n", result);
        return  -2;
    }
    char* name = args[3];
    FILE* undo_log = fopen(name, "rwb");
    int log_entries = 0;

    FILE* seats = fopen(name, "rwb");

    for (int i = 0; i < TOTAL_SEATS; i++) {
        fseek(seats, i * sizeof(int), SEEK_SET);
        int empty = 0;
        fwrite(&empty, sizeof(int), 1, seats);
    }

    Status status = INIT;
    while (1) {
        if (status == COMMITTED || status == INIT || status == TIMEOUT) {
            status = INIT;
            setSocketTimeout(0, serverSocket);
        }
        else {
            setSocketTimeout(10000, serverSocket);
        }

        // Aguarda uma mensagem na mailbox usando variável de condição
        Message current_msg;
        long result = read(serverSocket, &current_msg, sizeof(current_msg));

        if (result < 0) {
            printf("TIMEOUT OCCURRED! IT SEEMS LIKE THE COORDINATOR HAS CRASHED!\nAWAITING NEW SIGNAL FROM COORDINATOR\n");
            current_msg = MSG_NONE;
            status = TIMEOUT;
        }

        if (current_msg == MSG_EXIT) break;

        if (current_msg == MSG_PREPARE) {
            printf("Received PREPARE command\n");

            transaction_t transaction;
            read(serverSocket, &transaction, sizeof(transaction_t));

            fseek(seats, transaction.seat_num, SEEK_SET);
            int reserved = 0;
            fread(&reserved, sizeof(int), 1, seats);

            if (reserved) {
                status = ABORT;
                printf("\tSeat already reserved.\n");
                reserved = 1;
            }
            else {
                status = READY;
                fseek(seats, transaction.seat_num, SEEK_SET);
                int reserve = 1;
                fwrite(&reserve, sizeof(int), 1, seats);

                printf("\tSeat successfully reserved.\n");
            }

            int num = transaction.seat_num;
            fwrite(&num, sizeof(int), 1, undo_log);
            fwrite(&reserved, sizeof(int), 1, undo_log);
            log_entries++;

            write(serverSocket, &status, sizeof(status));
        }
        else if (current_msg == MSG_COMMIT) {
            printf("Received COMMIT command\n");
            status = status == READY ? COMMITTED : status;

            if (status == COMMITTED) printf("\tSuccessfully commited!\n");
            // printf("send: %d\n", status);
            write(serverSocket, &status, sizeof(status));
        }
        else if (current_msg == MSG_ABORT) {
            printf("Received ABORT command\n");
            status = (status == READY || status == INIT) ? ABORTED : status;
            int reserved_seat = 0;

            fseek(undo_log, 2 * log_entries * sizeof(int), SEEK_SET);
            fread(&reserved_seat, sizeof(int), 1, undo_log);

            int unreserved = 0;
            fseek(seats, reserved_seat * sizeof(int), SEEK_SET);
            fwrite(&unreserved, sizeof(int), 1, seats);
            log_entries--;

            if (status == READY || status == INIT) printf("\tSuccessfully aborted!\n");
            // printf("send: %d\n", status);
            write(serverSocket, &status, sizeof(status));
        }
        else if (current_msg == MSG_RESET) {
            printf("Received RESET command\n");
            status = INIT;
        }
    }
    fclose(undo_log);
    return 0;
}

void setSocketTimeout(long usec, int socket) {
    struct timeval no_timeout;
    no_timeout.tv_sec = usec/1000000;
    no_timeout.tv_usec = usec - (no_timeout.tv_sec * 1000000);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
}