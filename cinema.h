#pragma once
typedef struct {
    int seat_num;
    int card_num;
    int price;
} transaction_t;

#define TOTAL_SEATS 80
#define DEFAULT_MONEY 50
#define PARTICIPANTS_CINEMA 2