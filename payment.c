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
        printf("Format: ./participante <IP> <Port> <name>\n");
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

    char* log_name = calloc(1, strlen(name) + 5);
    strcpy(log_name, name);
    strcat(log_name, "_log");
    FILE* undo_log = fopen(log_name, "wb+");
    int log_entries = 0;

    FILE* accounts;
    if (!access(name, F_OK)) {
        accounts = fopen(name, "rb+");
        printf("Accounts file already existed!\n");
    }
    else {
        accounts = fopen(name, "wb+");
        printf("Accounts file created!\n");

        for (int i = 0; i < TOTAL_SEATS; i++) {
            fseek(accounts, i * sizeof(int), SEEK_SET);
            int money = DEFAULT_MONEY;
            fwrite(&money, sizeof(int), 1, accounts);
        }
    }

    Status status = INIT;
    while (1) {
        if (status == COMMITTED || status == INIT || status == TIMEOUT || status == ABORTED) {
            status = INIT;
            setSocketTimeout(0, serverSocket);
        }
        else {
            setSocketTimeout(100000, serverSocket);
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

            // printf("%d, %d\n", transaction.card_num, transaction.price);

            fseek(accounts, transaction.card_num * sizeof(int), SEEK_SET);
            int balance = 0;
            fread(&balance, sizeof(int), 1, accounts);

            // printf("bal: %d\n", balance);

            if (balance >= transaction.price) {
                status = READY;
                printf("\tPayment successful.\n");
            }
            else {
                status = ABORT;
                printf("\tPayment declined.\n");
            }

            int card = transaction.card_num;
            int price = transaction.price;
            fseek(undo_log, 2 * log_entries * sizeof(int), SEEK_SET);
            fwrite(&card, sizeof(int), 1, undo_log);
            fwrite(&price, sizeof(int), 1, undo_log);
            log_entries++;

            fseek(accounts, transaction.card_num * sizeof(int), SEEK_SET);
            int new_balance = balance - transaction.price;
            // printf("offset: %ld, value: %d\n", transaction.card_num * sizeof(int), new_balance);
            size_t written = fwrite(&new_balance, sizeof(int), 1, accounts);
            // printf("written: %lu\n", written);

            // Eu sei que parece sem sentido, mas esse read atualiza o valor no arquivo, sem ele new_balance nunca e escrito
            fseek(accounts, transaction.card_num * sizeof(int), SEEK_SET);
            int newest_balance = 0;
            fread(&newest_balance, sizeof(int), 1, accounts);
            // printf("newest: %d\n", newest_balance);

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

            int card = 0;
            int change = 0;

            fseek(undo_log, 2 * (log_entries - 1) * sizeof(int), SEEK_SET);
            fread(&card, sizeof(int), 1, undo_log);
            fread(&change, sizeof(int), 1, undo_log);

            int balance = 0;
            fseek(accounts, card * sizeof(int), SEEK_SET);
            fread(&balance, sizeof(int), 1, accounts);

            balance += change;
            fseek(accounts, card * sizeof(int), SEEK_SET);
            // printf("offset: %ld, value: %d\n", card * sizeof(int), balance);
            size_t written2 = fwrite(&balance, sizeof(int), 1, accounts);
            // printf("written2: %lu\n", written2);
            log_entries--;

            // NAO REMOVER
            fseek(accounts, card * sizeof(int), SEEK_SET);
            int newestest_balance = 0;
            fread(&newestest_balance, sizeof(int), 1, accounts);
            // printf("newestest: %d\n", newestest_balance);

            if (status == READY || status == INIT) printf("\tSuccessfully aborted!\n");
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