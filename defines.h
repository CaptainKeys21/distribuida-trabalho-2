// Estados possíveis de uma transação no participante
typedef enum { INIT, PREPARING, READY, COMMITTED, ABORTED, TIMEOUT } Status;
// Mensagens enviadas do coordenador para o participante
typedef enum { MSG_NONE, MSG_PREPARE, MSG_COMMIT, MSG_ABORT, MSG_EXIT, MSG_RESET } Message;

typedef struct {
    int id;
    Status status;
    Message mailbox;
    int delay_ms;
    int fail_prepare;
} Participant;

typedef struct {
    int socket;
    Status* status;
} listener_args_t;

#define TRUE 1
#define FALSE 0
#define MAX_PARTICIPANTS 8
