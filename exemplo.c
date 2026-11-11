#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

// Estados possíveis de uma transação no participante
typedef enum { INIT, PREPARING, READY, COMMITTED, ABORTED } Status;
// Mensagens enviadas do coordenador para o participante
typedef enum { MSG_NONE, MSG_PREPARE, MSG_COMMIT, MSG_ABORT, MSG_EXIT } Message;

typedef struct {
    int id;
    Status status;
    Message mailbox;
    int delay_ms;
    int fail_prepare;

    pthread_mutex_t lock;
    pthread_cond_t cv;
    pthread_t thread;
} Participant;

// Estado global do Coordenador para receber votos
pthread_mutex_t coord_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t coord_cv = PTHREAD_COND_INITIALIZER;
int votes_received = 0;

// ==========================================
// Rotina do Participante (Thread)
// ==========================================
void* participant_routine(void* arg) {
    Participant* p = (Participant*)arg;

    while (1) {
        // Aguarda uma mensagem na mailbox usando variável de condição
        pthread_mutex_lock(&p->lock);
        while (p->mailbox == MSG_NONE) {
            pthread_cond_wait(&p->cv, &p->lock);
        }
        Message current_msg = p->mailbox;
        p->mailbox = MSG_NONE; // Consome a mensagem
        pthread_mutex_unlock(&p->lock);

        if (current_msg == MSG_EXIT) break;

        if (current_msg == MSG_PREPARE) {
            printf("[Participante %d] Validando transacao...\n", p->id);
            usleep(p->delay_ms * 1000); // Simula latência/I/O

            pthread_mutex_lock(&p->lock);
            if (p->fail_prepare) {
                printf("[Participante %d] VOTO: ABORT ❌\n", p->id);
                p->status = ABORTED;
            } else {
                printf("[Participante %d] VOTO: COMMIT ✅ (Lock adquirido, estado READY)\n", p->id);
                p->status = READY;
            }
            pthread_mutex_unlock(&p->lock);

            // Notifica o coordenador que o voto foi computado
            pthread_mutex_lock(&coord_lock);
            votes_received++;
            pthread_cond_signal(&coord_cv);
            pthread_mutex_unlock(&coord_lock);
        }
        else if (current_msg == MSG_COMMIT) {
            pthread_mutex_lock(&p->lock);
            if (p->status == READY) {
                printf("[Participante %d] Transacao COMMITADA 💾 (Locks liberados)\n", p->id);
                p->status = COMMITTED;
            }
            pthread_mutex_unlock(&p->lock);
        }
        else if (current_msg == MSG_ABORT) {
            pthread_mutex_lock(&p->lock);
            // Só faz rollback se estava preparado para commitar
            if (p->status == READY || p->status == INIT) {
                printf("[Participante %d] Transacao ABORTADA 🔄 (Locks liberados)\n", p->id);
                p->status = ABORTED;
            }
            pthread_mutex_unlock(&p->lock);
        }
    }
    return NULL;
}

// Função auxiliar para enviar mensagens via Mailbox
void send_msg(Participant* p, Message m) {
    pthread_mutex_lock(&p->lock);
    p->mailbox = m;
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->lock);
}

// ==========================================
// Rotina do Coordenador
// ==========================================
void execute_transaction(int tx_id, Participant* parts, int count, int timeout_ms, int simulate_crash) {
    printf("\n=== INICIANDO TRANSACAO TX-%d ===\n", tx_id);
    
    // Reset global state
    pthread_mutex_lock(&coord_lock);
    votes_received = 0;
    pthread_mutex_unlock(&coord_lock);

    // FASE 1: Prepare
    printf("[Coordenador] Fase 1: Enviando Prepare (Timeout de %dms)\n", timeout_ms);
    for (int i = 0; i < count; i++) {
        send_msg(&parts[i], MSG_PREPARE);
    }

    // Configura o Timeout Absoluto para o Cond Wait
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    // Aguarda todos os votos OR Timeout
    pthread_mutex_lock(&coord_lock);
    int rc = 0;
    while (votes_received < count && rc == 0) {
        rc = pthread_cond_timedwait(&coord_cv, &coord_lock, &ts);
    }
    pthread_mutex_unlock(&coord_lock);

    int all_agreed = 1;
    if (rc == ETIMEDOUT) {
        printf("[Rede/Coordenador] ⚠️ TIMEOUT atingido esperando votos!\n");
        all_agreed = 0;
    } else {
        // Verifica se todos estão READY
        for (int i = 0; i < count; i++) {
            pthread_mutex_lock(&parts[i].lock);
            if (parts[i].status != READY) {
                all_agreed = 0;
            }
            pthread_mutex_unlock(&parts[i].lock);
        }
    }

    printf("\n[Coordenador] Decisao Fase 1: %s\n", all_agreed ? "GLOBAL COMMIT" : "GLOBAL ABORT");

    // SIMULAÇÃO DO PROBLEMA DE BLOQUEIO DO 2PC
    if (simulate_crash && all_agreed) {
        printf("\n[CRITICAL ERROR] 💥 O Coordenador sofreu um SIGSEGV e morreu!\n");
        printf("[CRITICAL ERROR] As mensagens da Fase 2 nunca foram enviadas.\n");
        return; // Retorna sem enviar Commit ou Abort
    }

    // FASE 2: Commit ou Abort
    for (int i = 0; i < count; i++) {
        send_msg(&parts[i], all_agreed ? MSG_COMMIT : MSG_ABORT);
    }

    // Dá um tempo para as threads processarem antes de encerrar a função
    usleep(200000); 
}

// Inicializa a struct e a thread do participante
void init_participant(Participant* p, int id, int delay, int fail) {
    p->id = id;
    p->status = INIT;
    p->mailbox = MSG_NONE;
    p->delay_ms = delay;
    p->fail_prepare = fail;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cv, NULL);
    pthread_create(&p->thread, NULL, participant_routine, p);
}

// ==========================================
// Main
// ==========================================
int main() {
    Participant parts[2];

    // CENÁRIO 1: Timeout de Rede (Um participante lento)
    init_participant(&parts[0], 1, 100, 0); // Normal
    init_participant(&parts[1], 2, 2000, 0); // Lento: 2s (Timeout é 1s)
    
    execute_transaction(101, parts, 2, 1000, 0);

    // Reseta estado para o próximo teste
    for(int i=0; i<2; i++) {
        pthread_mutex_lock(&parts[i].lock);
        parts[i].status = INIT;
        pthread_mutex_unlock(&parts[i].lock);
    }
    // Muda a simulação do participante 2 para não ser lento
    parts[1].delay_ms = 100;

    // CENÁRIO 2: Sucesso, sem timeout
    execute_transaction(102, parts, 2, 1000, 0);

    // Reseta estado para o próximo teste
    for(int i=0; i<2; i++) {
        pthread_mutex_lock(&parts[i].lock);
        parts[i].status = INIT;
        pthread_mutex_unlock(&parts[i].lock);
    }

    // CENÁRIO 3: Queda do Coordenador (O Calcanhar de Aquiles do 2PC)
    execute_transaction(103, parts, 2, 1000, 1);

    printf("\n=== ESTADO PÓS-CRASH ===\n");
    for(int i=0; i<2; i++) {
        printf("Status Participante %d: %d (2 = READY)\n", parts[i].id, parts[i].status);
    }
    printf("(Ambos os nós estão presos no estado READY, retendo locks e aguardando um coordenador morto).\n");

    // Cleanup (Manda sair e junta as threads)
    for(int i=0; i<2; i++) send_msg(&parts[i], MSG_EXIT);
    for(int i=0; i<2; i++) pthread_join(parts[i].thread, NULL);

    return 0;
}