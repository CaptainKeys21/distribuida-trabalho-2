# INFO

This folder contains the source files for a simple cinema ticket sale system implemented using the Two-Phase Commit protocol.

The protocol was implemented using berkeley sockets in the TCP mode. The arguments each program takes are specified by each one of them, if any are missing. The coordinator executes its operations as a procedure, while the participants operate as finite state machines reacting to their respective coordinator's inputs.

To simulate database I/O operations, simple binary files were used.

# The example

## Compilation

To compile the backup system, you must have GCC installed in your system. All that is necessary is to run the following commands to compile the coordinator's and the two participants' programs: `gcc coordenador_cinema.c -o coordenador_cinema`, `gcc cinema_manager.c -o cinema_manager`  and `gcc payment.c -o payment`.

## Use

To run the coordinator program, provide the desired address, port, transaction price, card number, cinema seat number and "fail" or "success" (to simulate server failure): `./coordenador_cinema 127.0.0.1 9734 40 4 10 success`. To run the participant programs, provide the same address and port as the coordinator, alongside a string containing the name of the files each of them will open (or create, if not present) and either "timeout", "fail" or "success" to determine the node's behavior: `./cinema_manager 127.0.0.1 9734 cinema_seats success` and `./payment 127.0.0.1 9734 bank_accounts success`.

The named files created by the programs may also be edited to create different configurations of available seats and bank balances.