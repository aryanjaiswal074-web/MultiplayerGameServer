
#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PLAYERS 2
#define MESSAGE_SIZE 256
#define QUEUE_SIZE 8

struct Player {
    struct lws *wsi;
    char symbol;
    int slot;

    char messages[QUEUE_SIZE][MESSAGE_SIZE];
    int head;
    int count;
};

static struct Player *players[MAX_PLAYERS] = {NULL, NULL};

static char board[9];
static char current_turn = 'X';
static int game_active = 0;

/* Add a message to a player's outgoing queue */
static void queue_message(struct Player *player, const char *message) {
    if (player == NULL || player->wsi == NULL)
        return;

    if (player->count >= QUEUE_SIZE) {
        lwsl_err("Message queue full\n");
        return;
    }

    int tail = (player->head + player->count) % QUEUE_SIZE;

    snprintf(
        player->messages[tail],
        MESSAGE_SIZE,
        "%s",
        message
    );

    player->count++;

    lws_callback_on_writable(player->wsi);
}

/* Send a message to both players */
static void broadcast(const char *message) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i] != NULL)
            queue_message(players[i], message);
    }
}

/* Initialize a new game */
static void reset_game(void) {
    for (int i = 0; i < 9; i++)
        board[i] = '1' + i;

    current_turn = 'X';
    game_active = 1;
}

/* Check for a winner */
static char check_winner(void) {
    const int lines[8][3] = {
        {0, 1, 2},
        {3, 4, 5},
        {6, 7, 8},
        {0, 3, 6},
        {1, 4, 7},
        {2, 5, 8},
        {0, 4, 8},
        {2, 4, 6}
    };

    for (int i = 0; i < 8; i++) {
        int a = lines[i][0];
        int b = lines[i][1];
        int c = lines[i][2];

        if (board[a] == board[b] &&
            board[b] == board[c] &&
            (board[a] == 'X' || board[a] == 'O')) {
            return board[a];
        }
    }

    return 0;
}

/* Check for a draw */
static int check_draw(void) {
    for (int i = 0; i < 9; i++) {
        if (board[i] != 'X' && board[i] != 'O')
            return 0;
    }

    return 1;
}

/* Broadcast the current board */
static void send_board(void) {
    char message[64];

    snprintf(
        message,
        sizeof(message),
        "BOARD:%.*s:%c",
        9,
        board,
        current_turn
    );

    broadcast(message);
}

/* Handle browser connections and messages */
static int callback_game(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
) {
    struct Player *player = (struct Player *)user;

    switch (reason) {

        case LWS_CALLBACK_ESTABLISHED: {

            memset(player, 0, sizeof(*player));

            player->wsi = wsi;
            player->slot = -1;

            int slot = -1;

            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i] == NULL) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1) {
                lwsl_notice("Room full: rejecting connection\n");
                return -1;
            }

            player->slot = slot;
            player->symbol = slot == 0 ? 'X' : 'O';

            players[slot] = player;

            printf("Player %c connected\n", player->symbol);

            queue_message(player, "WAIT");

            if (players[0] != NULL && players[1] != NULL) {

                reset_game();

                queue_message(players[0], "START:X");
                queue_message(players[1], "START:O");

                send_board();

                printf("Two players matched. Game started!\n");
            }

            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {

            if (player->count > 0) {

                unsigned char buffer[LWS_PRE + MESSAGE_SIZE];

                char *message = player->messages[player->head];

                size_t message_len = strlen(message);

                memcpy(
                    buffer + LWS_PRE,
                    message,
                    message_len
                );

                int result = lws_write(
                    wsi,
                    buffer + LWS_PRE,
                    message_len,
                    LWS_WRITE_TEXT
                );

                if (result < 0 || (size_t)result != message_len)
                    return -1;

                player->head = (player->head + 1) % QUEUE_SIZE;
                player->count--;

                if (player->count > 0)
                    lws_callback_on_writable(wsi);
            }

            break;
        }

        case LWS_CALLBACK_RECEIVE: {

            if (!game_active || player->slot < 0)
                break;

            if (len == 0 || len >= 64)
                break;

            char input[64] = {0};

            memcpy(input, in, len);
            input[len] = '\0';

            if (strncmp(input, "MOVE:", 5) != 0)
                break;

            char *end = NULL;
            long position = strtol(input + 5, &end, 10);

            if (end == input + 5 || *end != '\0')
                break;

            if (position < 1 || position > 9)
                break;

            if (player->symbol != current_turn)
                break;

            int index = (int)position - 1;

            if (board[index] == 'X' || board[index] == 'O')
                break;

            board[index] = player->symbol;

            char winner = check_winner();

            if (winner != 0) {

                send_board();

                char result[32];

                snprintf(
                    result,
                    sizeof(result),
                    "RESULT:%c",
                    winner
                );

                broadcast(result);

                game_active = 0;

                printf("Player %c won!\n", winner);
            }
            else if (check_draw()) {

                send_board();
                broadcast("RESULT:D");

                game_active = 0;

                printf("Game ended in a draw\n");
            }
            else {

                current_turn =
                    current_turn == 'X' ? 'O' : 'X';

                send_board();
            }

            break;
        }

        case LWS_CALLBACK_CLOSED: {

            printf("Player disconnected\n");

            int slot = player->slot;

            if (slot >= 0 &&
                slot < MAX_PLAYERS &&
                players[slot] == player) {

                players[slot] = NULL;

                game_active = 0;

                int opponent = 1 - slot;

                if (players[opponent] != NULL) {
                    queue_message(
                        players[opponent],
                        "OPPONENT_LEFT"
                    );
                }
            }

            break;
        }

        default:
            break;
    }

    return 0;
}

/* WebSocket protocol configuration */
static const struct lws_protocols protocols[] = {
    {
        .name = "tic-tac-toe",
        .callback = callback_game,
        .per_session_data_size = sizeof(struct Player),
        .rx_buffer_size = 256
    },
    {0}
};

/* Main server */
int main(void) {

    const char *port_env = getenv("PORT");
    int port = 8081;

    if (port_env != NULL && port_env[0] != '\0') {
        char *end = NULL;
        long parsed_port = strtol(port_env, &end, 10);

        if (end == port_env || *end != '\0' ||
            parsed_port < 1 || parsed_port > 65535) {
            fprintf(stderr, "Invalid PORT environment variable\n");
            return 1;
        }

        port = (int)parsed_port;
    }

    struct lws_context_creation_info info;

    memset(&info, 0, sizeof(info));

    info.port = port;
    info.iface = "0.0.0.0";
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);

    if (context == NULL) {
        printf("Could not create WebSocket server\n");
        return 1;
    }

    printf("\n");
    printf("=================================\n");
    printf("       GAME ARENA SERVER\n");
    printf("=================================\n");
    printf("WebSocket port: %d\n", port);
    printf("Listening on: 0.0.0.0\n");
    printf("Waiting for two players...\n");
    printf("=================================\n\n");

    while (1) {
        lws_service(context, 1000);
    }

    lws_context_destroy(context);

    return 0;
}
