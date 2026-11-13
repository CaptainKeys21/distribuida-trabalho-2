# INFO

This folder contains the source file for two different demonstrations: a simple, barebones implementation of the Two-Phase Commit protocol without any additional functionality, as well as a simple cinema ticket sale system implemented using the former as a base.

The protocol was implemented using berkeley sockets in the TCP mode. The arguments each program takes are specified by each one of them, if any are missing. The coordinator executes its operations as a procedure, while the participants operate as finite state machines reacting to their respective coordinator's inputs.

To simulate database I/O operations, simple binary files were used.

# The protocol implementation

## Compilation

To compile the backup system, you must have GCC installed in your system. All that is necessary is to run the following commands to compile the coordinator's and the participants' programs: `gcc coordenador.c -o coordenador` and `gcc participante.c -o participante`.

## Use

To run the coordinator program, provide the desired address, port and participant quantity: `./coordenador 127.0.0.1 9734 3`. To run the participant program, provide the same arguments as the ones passed to the coordinator, except the participant amount, alongside a delay value, in microseconds, to simulate processing a request, and the string "fail" to simulate the node aborting the procedure, or any other string to proceed as normal: `./participante 127.0.0.1 9734 100 success`.

The coordinator program must be initialized before any participant is started. The participants will immediately attempt to connect to their server. If the server is not running after starting a participant, the participant's process will exit. The coordinator will only start if the amount of participants connected to it reaches its participant quantity argument.

# The example

## Compilation

To compile the backup system, you must have GCC installed in your system. All that is necessary is to run the following commands to compile the coordinator's and the two participants' programs: `gcc coordenador_cinema.c -o coordenador_cinema`, `gcc cinema_manager.c -o cinema_manager`  and `gcc payment.c -o payment`.

## Use

To run the coordinator program, provide the desired address, port, transaction price, card number, cinema seat number and "fail" or "success" (to simulate server failure): `./coordenador_cinema 127.0.0.1 9734 40 4 10 success`. To run the participant programs, provide the same address and port as the coordinator, alongside a string containing the name of the files each of them will open (or create, if not present): `./cinema_manager 127.0.0.1 9734 cinema_seats` and `./payment 127.0.0.1 9734 bank_accounts`.

The named files created by the programs may also be edited to create different configurations of available seats and bank balances.