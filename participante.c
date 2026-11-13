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

void setSocketTimeout(long usec, int socket);

int main(int argc, char** args) {

    if (argc < 5) {
        printf("Format: ./participante <IP> <Port> <delay us> <fail/success>\n");
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
    int delay_us = atoi(args[3]);
    int fail = strcmp(args[4], "fail") == 0;
    // printf("fail: %d\n", fail);

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
            // printf("[Participante %d] Validando transacao...\n", p->id);
            usleep(delay_us); // Simula latência/I/O
            //TODO: FUNCIONALIDADE AQUI

            status = fail ? ABORT : READY;

            if (status == ABORT) printf("\tInduced failure.\n");
            else printf("\tSuccessfully prepared.\n");
            // printf("send: %d\n", status);
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
            //TODO: REVERT TO PREVIOUS STATE
            if (status == READY || status == INIT) printf("\tSuccessfully aborted!\n");
            // printf("send: %d\n", status);
            write(serverSocket, &status, sizeof(status));
        }
        else if (current_msg == MSG_RESET) {
            printf("Received RESET command\n");
            status = INIT;
        }
    }

    return 0;
}

void setSocketTimeout(long usec, int socket) {
    struct timeval no_timeout;
    no_timeout.tv_sec = usec/1000000;
    no_timeout.tv_usec = usec - (no_timeout.tv_sec * 1000000);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
}