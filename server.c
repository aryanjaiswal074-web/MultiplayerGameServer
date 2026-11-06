
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "game.h"
#include "protocol.h"

#define PORT 8080

typedef struct {
    int player1_fd;
    int player2_fd;
} GameSession;

int waiting_player = -1;
pthread_mutex_t lobby_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Send the complete message */
int send_all(int fd, const void *data, size_t size) {

    const char *ptr = data;
    size_t total = 0;

    while (total < size) {

        ssize_t n = send(fd, ptr + total, size - total, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }

        if (n == 0) return 0;

        total += (size_t)n;
    }

    return 1;
}

/* Receive the complete message */
int recv_all(int fd, void *data, size_t size) {

    char *ptr = data;
    size_t total = 0;

    while (total < size) {

        ssize_t n = recv(fd, ptr + total, size - total, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (n == 0) return 0;

        total += (size_t)n;
    }

    return 1;
}

/* Send a structured game message */
int send_message(int fd, Message *msg) {
    return send_all(fd, msg, sizeof(Message));
}

/* Notify a player */
void notify_player(int fd, int type, const char *text) {

    Message msg = {0};

    msg.type = type;

    if (text != NULL) {
        snprintf(msg.text, sizeof(msg.text), "%s", text);
    }

    send_message(fd, &msg);
}

/* Send board and turn information */
void broadcast_game(int fd1, int fd2, Game *game) {

    Message msg = {0};

    msg.type = MSG_BOARD;
    memcpy(msg.board, game->board, BOARD_SIZE);

    send_message(fd1, &msg);
    send_message(fd2, &msg);

    memset(&msg, 0, sizeof(msg));

    msg.type = MSG_TURN;
    msg.symbol = game->current_player;

    send_message(fd1, &msg);
    send_message(fd2, &msg);
}

/* Handle one complete multiplayer game */
void *handle_game(void *arg) {

    GameSession session = *(GameSession *)arg;
    free(arg);

    int p1 = session.player1_fd;
    int p2 = session.player2_fd;

    Game game;

    game_init(&game);

    Message msg = {0};

    msg.type = MSG_START;
    msg.symbol = 'X';
    snprintf(msg.text, sizeof(msg.text), "You are Player X");

    if (!send_message(p1, &msg)) goto cleanup;

    memset(&msg, 0, sizeof(msg));

    msg.type = MSG_START;
    msg.symbol = 'O';
    snprintf(msg.text, sizeof(msg.text), "You are Player O");

    if (!send_message(p2, &msg)) goto cleanup;

    printf("\nNew multiplayer match started!\n");

    while (1) {

        broadcast_game(p1, p2, &game);

        int current_fd =
            (game.current_player == 'X') ? p1 : p2;

        Message incoming = {0};

        int status = recv_all(current_fd,
                              &incoming,
                              sizeof(incoming));

        if (status <= 0) {

            int opponent_fd =
                (current_fd == p1) ? p2 : p1;

            notify_player(opponent_fd, MSG_ERROR,
                          "Opponent disconnected.");

            break;
        }

        if (incoming.type != MSG_MOVE ||
            !game_make_move(&game,
                            incoming.position,
                            game.current_player)) {

            notify_player(current_fd, MSG_ERROR,
                          "Invalid move. Try again.");

            continue;
        }

        char winner = game_get_winner(&game);

        if (winner != ' ') {

            Message result = {0};

            result.type = MSG_BOARD;
            memcpy(result.board, game.board, BOARD_SIZE);

            send_message(p1, &result);
            send_message(p2, &result);

            memset(&result, 0, sizeof(result));

            result.type = MSG_RESULT;
            result.result = winner;

            if (winner == 'D') {
                snprintf(result.text, sizeof(result.text),
                         "Game Draw!");
            } else {
                snprintf(result.text, sizeof(result.text),
                         "Player %c wins!", winner);
            }

            send_message(p1, &result);
            send_message(p2, &result);

            printf("%s\n", result.text);

            break;
        }
    }

cleanup:

    close(p1);
    close(p2);

    printf("Game session closed.\n");

    return NULL;
}

/* Main server */
int main() {

    signal(SIGPIPE, SIG_IGN);

    int server_fd;

    struct sockaddr_in address = {0};

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("\n=================================\n");
    printf("     MULTIPLAYER GAME SERVER\n");
    printf("=================================\n");
    printf("Server running on port %d\n", PORT);
    printf("Waiting for players...\n");

    while (1) {

        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("Accept failed");
            continue;
        }

        pthread_mutex_lock(&lobby_mutex);

        if (waiting_player == -1) {

            notify_player(client_fd, MSG_WAIT,
                          "Waiting for an opponent...");

            waiting_player = client_fd;

            printf("Player waiting in lobby.\n");

            pthread_mutex_unlock(&lobby_mutex);

        } else {

            int player1 = waiting_player;
            int player2 = client_fd;

            waiting_player = -1;

            pthread_mutex_unlock(&lobby_mutex);

            GameSession *session =
                malloc(sizeof(GameSession));

            if (session == NULL) {
                notify_player(player1, MSG_ERROR,
                              "Server memory error.");
                notify_player(player2, MSG_ERROR,
                              "Server memory error.");
                close(player1);
                close(player2);
                continue;
            }

            session->player1_fd = player1;
            session->player2_fd = player2;

            pthread_t game_thread;

            if (pthread_create(&game_thread, NULL,
                               handle_game, session) != 0) {

                perror("Game thread creation failed");

                close(player1);
                close(player2);
                free(session);

                continue;
            }

            pthread_detach(game_thread);

            printf("Two players matched! Game starting.\n");
        }
    }

    close(server_fd);

    return 0;
}
