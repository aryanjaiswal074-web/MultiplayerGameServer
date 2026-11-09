#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROOMS 10
#define MAX_PLAYERS_PER_ROOM 2
#define MESSAGE_SIZE 256
#define QUEUE_SIZE 16

struct Room;

struct Player {
    struct lws *wsi;
    struct Room *room;
    char symbol;
    int slot;
    int waiting_public;

    char messages[QUEUE_SIZE][MESSAGE_SIZE];
    int head;
    int count;
};

struct Room {
    int used;
    int game_active;
    struct Player *players[2];
    char board[9];
    char current_turn;
    int rematch_ready[2];
};

static struct Room rooms[MAX_ROOMS];

static void queue_message(struct Player *player, const char *message) {
    if (!player || !player->wsi)
        return;

    if (player->count >= QUEUE_SIZE) {
        lwsl_err("Message queue full\n");
        return;
    }

    int tail = (player->head + player->count) % QUEUE_SIZE;

    snprintf(player->messages[tail], MESSAGE_SIZE, "%s", message);

    player->count++;

    lws_callback_on_writable(player->wsi);
}

static void broadcast(struct Room *room, const char *message) {
    if (!room)
        return;

    for (int i = 0; i < 2; i++) {
        if (room->players[i])
            queue_message(room->players[i], message);
    }
}

static void reset_game(struct Room *room) {
    for (int i = 0; i < 9; i++)
        room->board[i] = '1' + i;

    room->current_turn = 'X';
    room->game_active = 1;
    room->rematch_ready[0] = 0;
    room->rematch_ready[1] = 0;
}

static void release_room_if_empty(struct Room *room) {
    if (!room)
        return;

    if (!room->players[0] && !room->players[1]) {
        printf("Room %ld released.\n", (long)(room - rooms) + 1);
        memset(room, 0, sizeof(*room));
    }
}

static void detach_player(struct Player *player, int notify_opponent) {
    if (!player || !player->room)
        return;

    struct Room *room = player->room;
    int slot = player->slot;

    if (slot >= 0 && slot < 2 &&
        room->players[slot] == player) {

        room->players[slot] = NULL;
        room->game_active = 0;
        room->rematch_ready[0] = 0;
        room->rematch_ready[1] = 0;

        int opponent_slot = 1 - slot;
        struct Player *opponent = room->players[opponent_slot];

        if (opponent && notify_opponent) {
            queue_message(opponent, "OPPONENT_LEFT");
        }
    }

    player->room = NULL;
    player->slot = -1;
    player->waiting_public = 0;

    release_room_if_empty(room);
}

static struct Room *find_waiting_room(void) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        struct Room *room = &rooms[i];

        if (!room->used || room->game_active)
            continue;

        if (room->players[0] &&
            !room->players[1] &&
            room->players[0]->waiting_public)
            return room;

        if (!room->players[0] &&
            room->players[1] &&
            room->players[1]->waiting_public)
            return room;
    }

    return NULL;
}

static struct Room *create_room(void) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].used) {
            struct Room *room = &rooms[i];

            memset(room, 0, sizeof(*room));
            room->used = 1;

            return room;
        }
    }

    return NULL;
}

static char check_winner(struct Room *room) {
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

        char value = room->board[a];

        if ((value == 'X' || value == 'O') &&
            value == room->board[b] &&
            value == room->board[c])
            return value;
    }

    return 0;
}

static int check_draw(struct Room *room) {
    for (int i = 0; i < 9; i++) {
        if (room->board[i] != 'X' &&
            room->board[i] != 'O')
            return 0;
    }

    return 1;
}

static void send_board(struct Room *room) {
    char message[64];

    snprintf(
        message,
        sizeof(message),
        "BOARD:%.9s:%c",
        room->board,
        room->current_turn
    );

    broadcast(room, message);
}

static void start_match(struct Room *room) {
    if (!room || !room->players[0] || !room->players[1])
        return;

    reset_game(room);

    room->players[0]->symbol = 'X';
    room->players[0]->slot = 0;
    room->players[0]->waiting_public = 0;

    room->players[1]->symbol = 'O';
    room->players[1]->slot = 1;
    room->players[1]->waiting_public = 0;

    queue_message(room->players[0], "START:X");
    queue_message(room->players[1], "START:O");

    send_board(room);

    printf(
        "Room %ld started: X vs O\n",
        (long)(room - rooms) + 1
    );

    fflush(stdout);
}

/* A rematch starts only after both players agree. */
static void handle_rematch(struct Player *player, int accepted) {
    if (!player || !player->room ||
        player->slot < 0 || player->slot > 1) {
        queue_message(player, "ERROR:REMATCH_UNAVAILABLE");
        return;
    }

    struct Room *room = player->room;
    int opponent_slot = 1 - player->slot;

    if (room->game_active ||
        !room->players[0] ||
        !room->players[1] ||
        room->players[player->slot] != player) {
        queue_message(player, "ERROR:REMATCH_UNAVAILABLE");
        return;
    }

    room->rematch_ready[player->slot] = 1;

    queue_message(player, "REMATCH_WAITING");

    if (!accepted && room->players[opponent_slot]) {
        queue_message(
            room->players[opponent_slot],
            "REMATCH_INVITE"
        );
    }

    if (room->rematch_ready[0] &&
        room->rematch_ready[1]) {

        printf(
            "Room %ld: rematch accepted by both players.\n",
            (long)(room - rooms) + 1
        );

        start_match(room);
    }

    fflush(stdout);
}

static void join_public_matchmaking(struct Player *player) {
    if (!player || !player->wsi)
        return;

    if (player->room)
        detach_player(player, 1);

    struct Room *room = find_waiting_room();

    if (!room)
        room = create_room();

    if (!room) {
        queue_message(player, "ERROR:ROOM_FULL");
        return;
    }

    int slot = room->players[0] ? 1 : 0;

    if (room->players[slot]) {
        queue_message(player, "ERROR:ROOM_FULL");
        return;
    }

    room->players[slot] = player;

    player->room = room;
    player->slot = slot;
    player->symbol = slot == 0 ? 'X' : 'O';
    player->waiting_public = 1;

    printf(
        "Player joined Room %ld as %c\n",
        (long)(room - rooms) + 1,
        player->symbol
    );

    queue_message(player, "WAIT");

    if (room->players[0] && room->players[1])
        start_match(room);

    fflush(stdout);
}

static void handle_move(struct Player *player, const char *input) {
    struct Room *room = player->room;

    if (!room || !room->game_active || player->slot < 0)
        return;

    if (player->symbol != room->current_turn)
        return;

    char *end = NULL;
    long position = strtol(input + 5, &end, 10);

    if (end == input + 5 || *end != '\0' ||
        position < 1 || position > 9)
        return;

    int index = (int)position - 1;

    if (room->board[index] == 'X' ||
        room->board[index] == 'O')
        return;

    room->board[index] = player->symbol;

    char winner = check_winner(room);

    if (winner) {
        send_board(room);

        char result[32];
        snprintf(result, sizeof(result), "RESULT:%c", winner);

        broadcast(room, result);

        room->game_active = 0;
        room->players[0]->waiting_public = 0;
        room->players[1]->waiting_public = 0;

        printf(
            "Room %ld: Player %c won.\n",
            (long)(room - rooms) + 1,
            winner
        );
    }
    else if (check_draw(room)) {
        send_board(room);
        broadcast(room, "RESULT:D");

        room->game_active = 0;
        room->players[0]->waiting_public = 0;
        room->players[1]->waiting_public = 0;

        printf(
            "Room %ld ended in a draw.\n",
            (long)(room - rooms) + 1
        );
    }
    else {
        room->current_turn =
            room->current_turn == 'X' ? 'O' : 'X';

        send_board(room);
    }

    fflush(stdout);
}

static int callback_game(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
) {
    struct Player *player = (struct Player *)user;

    switch (reason) {

        case LWS_CALLBACK_ESTABLISHED:
            memset(player, 0, sizeof(*player));

            player->wsi = wsi;
            player->slot = -1;

            printf("New player connected.\n");

            join_public_matchmaking(player);
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (player->count > 0) {
                unsigned char buffer[LWS_PRE + MESSAGE_SIZE];

                char *message = player->messages[player->head];
                size_t message_len = strlen(message);

                memcpy(buffer + LWS_PRE, message, message_len);

                int result = lws_write(
                    wsi,
                    buffer + LWS_PRE,
                    message_len,
                    LWS_WRITE_TEXT
                );

                if (result < 0 || (size_t)result != message_len)
                    return -1;

                player->head =
                    (player->head + 1) % QUEUE_SIZE;

                player->count--;

                if (player->count > 0)
                    lws_callback_on_writable(wsi);
            }
            break;

        case LWS_CALLBACK_RECEIVE: {
            if (len == 0 || len >= 64)
                break;

            char input[64] = {0};
            memcpy(input, in, len);
            input[len] = '\0';

            if (strncmp(input, "MOVE:", 5) == 0) {
                handle_move(player, input);
            }
            else if (strcmp(input, "FIND_MATCH") == 0) {
                printf("Player requested public matchmaking.\n");
                join_public_matchmaking(player);
            }
            else if (strcmp(input, "REMATCH_REQUEST") == 0) {
                printf("Player requested a rematch.\n");
                handle_rematch(player, 0);
            }
            else if (strcmp(input, "REMATCH_ACCEPT") == 0) {
                printf("Player accepted a rematch invitation.\n");
                handle_rematch(player, 1);
            }

            break;
        }

        case LWS_CALLBACK_CLOSED:
            printf("Player disconnected.\n");

            detach_player(player, 1);

            fflush(stdout);
            break;

        default:
            break;
    }

    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        .name = "tic-tac-toe",
        .callback = callback_game,
        .per_session_data_size = sizeof(struct Player),
        .rx_buffer_size = 256
    },
    {0}
};

int main(void) {
    const char *port_env = getenv("PORT");
    int port = 8081;

    if (port_env && port_env[0]) {
        char *end = NULL;
        long parsed = strtol(port_env, &end, 10);

        if (end == port_env || *end ||
            parsed < 1 || parsed > 65535) {
            fprintf(stderr, "Invalid PORT environment variable\n");
            return 1;
        }

        port = (int)parsed;
    }

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = port;
    info.iface = "0.0.0.0";
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);

    if (!context) {
        fprintf(stderr, "Could not create WebSocket server\n");
        return 1;
    }

    printf("\n");
    printf("=================================\n");
    printf("       GAME ARENA SERVER\n");
    printf("=================================\n");
    printf("WebSocket port: %d\n", port);
    printf("Maximum rooms: %d\n", MAX_ROOMS);
    printf("Maximum players: %d\n", MAX_ROOMS * 2);
    printf("Waiting for players...\n");
    printf("=================================\n\n");

    fflush(stdout);

    while (1)
        lws_service(context, 1000);

    lws_context_destroy(context);
    return 0;
}