
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "protocol.h"

#define PORT 8080

/* Send complete message */
int send_all(int fd, const void *data, size_t size) {

    const char *ptr = data;
    size_t total = 0;

    while (total < size) {

        ssize_t n = send(fd, ptr + total,
                         size - total, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }

        if (n == 0) return 0;

        total += (size_t)n;
    }

    return 1;
}

/* Receive complete message */
int recv_all(int fd, void *data, size_t size) {

    char *ptr = data;
    size_t total = 0;

    while (total < size) {

        ssize_t n = recv(fd, ptr + total,
                         size - total, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (n == 0) return 0;

        total += (size_t)n;
    }

    return 1;
}

/* Display game board */
void display_board(const char board[BOARD_SIZE]) {

    printf("\n");
    printf("       TIC TAC TOE\n\n");

    for (int i = 0; i < BOARD_SIZE; i++) {

        printf("   %c ", board[i]);

        if (i % 3 != 2) {
            printf("|");
        }

        if (i % 3 == 2 && i != 8) {
            printf("\n  ---+---+---\n");
        }
    }

    printf("\n\n");
}

/* Accept player move */
int get_player_move(void) {

    char input[64];
    char *end;
    long position;

    while (1) {

        printf("Enter position (1-9): ");

        if (fgets(input, sizeof(input), stdin) == NULL) {
            return -1;
        }

        errno = 0;
        position = strtol(input, &end, 10);

        while (*end == ' ' || *end == '\t') {
            end++;
        }

        if (errno == 0 &&
            end != input &&
            (*end == '\n' || *end == '\0') &&
            position >= 1 && position <= 9) {

            return (int)position;
        }

        printf("Invalid input. Enter a number from 1 to 9.\n");
    }
}

int main(void) {

    int sock;
    struct sockaddr_in server_address;

    char my_symbol = '\0';

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_address.sin_addr) <= 0) {

        perror("Invalid server address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {

        perror("Connection failed");
        close(sock);
        return 1;
    }

    printf("\n================================\n");
    printf("      MULTIPLAYER TIC TAC TOE\n");
    printf("================================\n");

    printf("Connected to game server!\n");

    while (1) {

        Message incoming = {0};

        int status = recv_all(sock, &incoming,
                              sizeof(incoming));

        if (status <= 0) {
            printf("Server disconnected.\n");
            break;
        }

        switch (incoming.type) {

            case MSG_WAIT:

                printf("\n%s\n", incoming.text);
                break;

            case MSG_START:

                my_symbol = incoming.symbol;

                printf("\nMATCH FOUND!\n");
                printf("%s\n", incoming.text);
                printf("Your symbol: %c\n", my_symbol);
                break;

            case MSG_BOARD:

                display_board(incoming.board);
                break;

            case MSG_TURN:

                if (incoming.symbol == my_symbol) {

                    printf("Your turn! (%c)\n", my_symbol);

                    int position = get_player_move();

                    if (position == -1) {
                        printf("Input closed.\n");
                        close(sock);
                        return 0;
                    }

                    Message outgoing = {0};

                    outgoing.type = MSG_MOVE;
                    outgoing.position = position;

                    if (!send_all(sock, &outgoing,
                                  sizeof(outgoing))) {

                        printf("Failed to send move.\n");
                        close(sock);
                        return 1;
                    }

                } else {

                    printf("Waiting for opponent (%c)...\n",
                           incoming.symbol);
                }

                break;

            case MSG_ERROR:

                printf("\nSERVER: %s\n", incoming.text);
                break;

            case MSG_RESULT:

                printf("\n================================\n");
                printf("          GAME OVER\n");
                printf("================================\n");

                printf("%s\n", incoming.text);

                if (incoming.result == my_symbol) {
                    printf("Congratulations! You won!\n");
                } else if (incoming.result == 'D') {
                    printf("It's a draw!\n");
                } else {
                    printf("Better luck next time!\n");
                }

                close(sock);
                return 0;

            default:

                printf("Unknown server message.\n");
                break;
        }
    }

    close(sock);

    return 0;
}
