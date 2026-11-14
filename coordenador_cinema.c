#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "defines.h"
#include "cinema.h"

void execute_transaction(int tx_id, int* clientSockets, int count, int simulate_crash, transaction_t transaction);
void* waitForResponse(void* args);

int main(int argc, char** args) {

    if (argc < 8) {
        printf("Format: ./coordenador_cinema <IP> <Port> <timeout_usec> <price> <card_num> <seat_num> <fail/success>");
        return -1;
    }

    int serverSocket;
    int* clientSockets = calloc(PARTICIPANTS_CINEMA, sizeof(int));
    int serverLength;
    int* clientLengths = calloc(PARTICIPANTS_CINEMA, sizeof(int));
    struct sockaddr_in serverAddress;
    struct sockaddr_in* clientAddresses = calloc(PARTICIPANTS_CINEMA, sizeof(struct sockaddr_un));

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(args[1]);
    serverAddress.sin_port = htons(atoi(args[2]));
    serverLength = sizeof(serverAddress);
    bind(serverSocket, (struct sockaddr *)&serverAddress, serverLength);
    listen(serverSocket, PARTICIPANTS_CINEMA);

    int timeout_usec = atoi(args[3]);

    int price = atoi(args[4]);
    int card_num = atoi(args[5]);
    int seat_num = atoi(args[6]);
    transaction_t transaction;
    transaction.price = price;
    transaction.card_num = card_num;
    transaction.seat_num = seat_num;

    int fail = strcmp(args[7], "fail") == 0;

    for (int participant = 0; participant < PARTICIPANTS_CINEMA; participant++) {
        printf("Awaiting participants\n");

        clientLengths[participant] = sizeof(clientAddresses[participant]);
        clientSockets[participant] = accept(serverSocket,
            (struct sockaddr *)&clientAddresses[participant], &clientLengths[participant]);

        struct timeval timeout;
        timeout.tv_sec = timeout_usec/1000000;
        timeout.tv_usec = timeout_usec - (timeout.tv_sec * 1000000);
        setsockopt(clientSockets[participant], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        printf("Participant %d connected (socket: %d)\n", participant, clientSockets[participant]);
    }

    // for (int i = 0; i < participants; i++) printf("\n%d", clientSockets[i]);
    execute_transaction(101, clientSockets, PARTICIPANTS_CINEMA, fail, transaction);

    printf("Waiting for any delayed participant to end processing\n");
    sleep(25);

    for (int i = 0; i < PARTICIPANTS_CINEMA; i++) {
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(clientSockets[i], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        printf("Disconnecting participant %d\n", i);

        Message message = MSG_EXIT;
        write(clientSockets[i], &message, sizeof(Message));
        close(clientSockets[i]);
    }

    free(clientSockets);
    free(clientAddresses);
    free(clientLengths);

    return 0;
}

// ==========================================
// Rotina do Coordenador
// ==========================================
void execute_transaction(int tx_id, int* clientSockets, int count, int simulate_crash, transaction_t transaction) {
    printf("\n=== INICIANDO TRANSACAO TX-%d ===\n", tx_id);

    // FASE 1: Prepare
    printf("[Coordenador] Fase 1: Enviando Prepare\n");
    int* received = calloc(count, sizeof(int));
    pthread_t* threads = calloc(count, sizeof(pthread_t));
    Status* statuses = calloc(count, sizeof(Status));
    listener_args_t* args = calloc(count, sizeof(listener_args_t));

    for (int i = 0; i < count; i++) { // ENVIA COMANDO DE PREPARACAO
        Message message = MSG_PREPARE;
        long result = write(clientSockets[i], &message, sizeof(message));
        received[i] = result != -1;

        result = write(clientSockets[i], &transaction, sizeof(transaction_t));
        received[i] = result != -1 && received[i];

        if (result != -1){
            args[i].socket = clientSockets[i];
            args[i].status = &statuses[i];
            pthread_create(&threads[i], NULL, waitForResponse, &args[i]);
        }
    }

    int all_agreed = TRUE;

    for (int i = 0; i < count; i++) { // ESPERA RECEBER READY DE TODOS
        if (received[i]) {
            pthread_join(threads[i], NULL);
            if (statuses[i] != READY) all_agreed = FALSE;
            if (statuses[i] == TIMEOUT) {
                printf("[WARNING participant %d timed out!\n", i);
            }
        }
        else all_agreed = FALSE;
    }


    printf("\n[Coordenador] Decisao Fase 1: %s\n", all_agreed ? "GLOBAL COMMIT" : "GLOBAL ABORT");

    // SIMULAÇÃO DO PROBLEMA DE BLOQUEIO DO 2PC
    if (simulate_crash) {
        printf("\n[CRITICAL ERROR] 💥 O Coordenador sofreu um SIGSEGV e morreu!\n");
        printf("[CRITICAL ERROR] As mensagens da Fase 2 nunca foram enviadas.\n");
        if (all_agreed) printf("[CRITICAL ERROR] Todos o nodos tinha entrado em acordo.\n");

        free(threads);
        free(statuses);
        free(received);
        free(args);
        return; // Retorna sem enviar Commit ou Abort
    }
    else {
        printf("\n[Coordenador] Enviando: %s\n", all_agreed ? "COMMIT" : "ABORT");
    }


    // FASE 2: Commit ou Abort
    for (int i = 0; i < count; i++) {
            Message message = all_agreed ? MSG_COMMIT : MSG_ABORT;
            long result = write(clientSockets[i], &message, sizeof(message));
            received[i] = result != -1;
            if (result != -1){
                args[i].socket = clientSockets[i];
                args[i].status = &statuses[i];
                pthread_create(&threads[i], NULL, waitForResponse, &args[i]);
            }
    }

    int everything_is_alright = TRUE;
    for (int i = 0; i < count; i++) { // ESPERA RECEBER COMMITED DE TODOS
        if (received[i]) {
            pthread_join(threads[i], NULL);
            if (statuses[i] != COMMITTED) everything_is_alright = FALSE;
            if (statuses[i] == TIMEOUT) printf("[WARNING] participant %d timed out!\n", i);
            // printf("status %d: %d\n", i, statuses[i]);
        }
        else everything_is_alright = FALSE;
    }

    if (everything_is_alright) {
        printf("\n[Coordenador] 💥 Commit realizado!\n");
    }
    else {
        printf("\n[Coordenador] 💥 Commit abortado!\n");
    }

    // Dá um tempo para as threads processarem antes de encerrar a função
    usleep(1000);

    free(threads);
    free(statuses);
    free(received);
    free(args);
}

void* waitForResponse(void* args) {
    listener_args_t* listener = (listener_args_t*)args;
    // printf("WAIT\n");
    long result = read(listener->socket, listener->status, sizeof(*listener->status));
    // printf("received %d\n", *listener->status);
    if (result < 0) {
        // printf("timeout\n");
        *listener->status = TIMEOUT;
    }

    return NULL;
}