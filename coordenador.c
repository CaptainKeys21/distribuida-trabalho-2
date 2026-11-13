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

void execute_transaction(int tx_id, int* clientSockets, int count, int simulate_crash);
void* waitForResponse(void* args);

int main(int argc, char** args) {

    if (argc < 4) {
        printf("Format: ./coordenador <IP> <Port> <participants>");
        return -1;
    }

    int serverSocket;
    int* clientSockets = calloc(MAX_PARTICIPANTS, sizeof(int));
    int serverLength;
    int* clientLengths = calloc(MAX_PARTICIPANTS, sizeof(int));
    struct sockaddr_in serverAddress;
    struct sockaddr_in* clientAddresses = calloc(MAX_PARTICIPANTS, sizeof(struct sockaddr_un));

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(args[1]);
    serverAddress.sin_port = htons(atoi(args[2]));
    serverLength = sizeof(serverAddress);
    bind(serverSocket, (struct sockaddr *)&serverAddress, serverLength);
    listen(serverSocket, MAX_PARTICIPANTS);

    int participants = atoi(args[3]);

    for (int participant = 0; participant < participants; participant++) {
        printf("Awaiting participants\n");

        clientLengths[participant] = sizeof(clientAddresses[participant]);
        clientSockets[participant] = accept(serverSocket,
            (struct sockaddr *)&clientAddresses[participant], &clientLengths[participant]);

        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt(clientSockets[participant], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        printf("Participant %d connected (socket: %d)\n", participant, clientSockets[participant]);
    }

    // for (int i = 0; i < participants; i++) printf("\n%d", clientSockets[i]);
    execute_transaction(101, clientSockets, participants, 0);

    // CENÁRIO 2: Sucesso, sem timeout
    execute_transaction(102, clientSockets, participants, 1);

    free(clientSockets);
    free(clientAddresses);
    free(clientLengths);

    return 0;
}

// ==========================================
// Rotina do Coordenador
// ==========================================
void execute_transaction(int tx_id, int* clientSockets, int count, int simulate_crash) {
    printf("\n=== INICIANDO TRANSACAO TX-%d ===\n", tx_id);

    // FASE 1: Prepare
    printf("[Coordenador] Fase 1: Enviando Prepare\n");
    int* received = calloc(count, sizeof(int));
    pthread_t* threads = calloc(count, sizeof(pthread_t));
    Status* statuses = calloc(count, sizeof(Status));
    listener_args_t* args = calloc(count, sizeof(listener_args_t));

    for (int i = 0; i < count; i++) { // ENVIA COMANDO DE PREPARACAO
        Message message = MSG_PREPARE;
        // printf("PREPARE: %d, %d\n", i, clientSockets[i]);
        long result = write(clientSockets[i], &message, sizeof(message));
        received[i] = result != -1;
        if (result != -1){
            args[i].socket = clientSockets[i];
            args[i].status = &statuses[i];
            // printf("sock: %d, stat: %p\n", args[i].socket, args[i].status);
            pthread_create(&threads[i], NULL, waitForResponse, &args[i]);
        }
    }

    int all_agreed = TRUE;

    for (int i = 0; i < count; i++) { // ESPERA RECEBER READY DE TODOS
        if (received[i]) {
            pthread_join(threads[i], NULL);
            if (statuses[i] != READY) all_agreed = FALSE;
            // printf("status %d: %d\n", i, statuses[i]);
        }
        else all_agreed = FALSE;
    }


    printf("\n[Coordenador] Decisao Fase 1: %s\n", all_agreed ? "GLOBAL COMMIT" : "GLOBAL ABORT");

    // SIMULAÇÃO DO PROBLEMA DE BLOQUEIO DO 2PC
    if (simulate_crash) {
        printf("\n[CRITICAL ERROR] 💥 O Coordenador sofreu um SIGSEGV e morreu!\n");
        printf("[CRITICAL ERROR] As mensagens da Fase 2 nunca foram enviadas.\n");
        if (all_agreed) printf("[CRITICAL ERROR] Todos os nodos tinham entrado em acordo.\n");

        free(threads);
        free(statuses);
        free(received);
        return; // Retorna sem enviar Commit ou Abort
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
    usleep(200000);

    free(threads);
    free(statuses);
    free(received);
}

void* waitForResponse(void* args) {
    listener_args_t* listener = (listener_args_t*)args;
    // printf("WAIT %d, %p\n", listener->socket, listener->status);
    long result = read(listener->socket, listener->status, sizeof(*listener->status));
    // printf("received %d\n", *listener->status);
    if (result < 0) {
        // printf("timeout\n");
        *listener->status = TIMEOUT;
    }

    return NULL;
}