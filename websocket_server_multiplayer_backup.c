#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define MAX_ROOMS 100
#define MESSAGE_SIZE 256
#define QUEUE_SIZE 16
#define ROOM_CODE_SIZE 9

struct Player;

struct Room {
    int used;
    int is_private;
    char code[ROOM_CODE_SIZE];
    struct Player *players[2];
    char board[10];
    char turn;
    int active;
};

struct Player {
    struct lws *wsi;
    struct Room *room;
    int slot;
    int waiting_public;
    char messages[QUEUE_SIZE][MESSAGE_SIZE];
    int head;
    int count;
};

static struct Room rooms[MAX_ROOMS];
static unsigned long next_room_id = 1;

static void queue_message(struct Player *p, const char *msg) {
    if (!p || !p->wsi)
        return;

    if (p->count >= QUEUE_SIZE) {
        lwsl_err("Outgoing queue full\n");
        return;
    }

    int tail = (p->head + p->count) % QUEUE_SIZE;

    snprintf(p->messages[tail], MESSAGE_SIZE, "%s", msg);

    p->count++;

    lws_callback_on_writable(p->wsi);
}

static void broadcast(struct Room *room, const char *msg) {
    if (!room)
        return;

    for (int i = 0; i < 2; i++)
        queue_message(room->players[i], msg);
}

static struct Room *new_room(int is_private) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].used) {
            struct Room *r = &rooms[i];

            memset(r, 0, sizeof(*r));

            r->used = 1;
            r->is_private = is_private;

            if (is_private) {
                snprintf(
                    r->code,
                    sizeof(r->code),
                    "%08lX",
                    next_room_id++ & 0xFFFFFFFFUL
                );
            }

            return r;
        }
    }

    return NULL;
}

static void reset_game(struct Room *r) {
    for (int i = 0; i < 9; i++)
        r->board[i] = (char)('1' + i);

    r->board[9] = '\0';
    r->turn = 'X';
    r->active = 1;
}

static void send_board(struct Room *r) {
    char msg[64];

    snprintf(
        msg,
        sizeof(msg),
        "BOARD:%.9s:%c",
        r->board,
        r->turn
    );

    broadcast(r, msg);
}

static char check_winner(struct Room *r) {
    static const int lines[8][3] = {
        {0,1,2},
        {3,4,5},
        {6,7,8},
        {0,3,6},
        {1,4,7},
        {2,5,8},
        {0,4,8},
        {2,4,6}
    };

    for (int i = 0; i < 8; i++) {
        int a = lines[i][0];
        int b = lines[i][1];
        int c = lines[i][2];

        char v = r->board[a];

        if ((v == 'X' || v == 'O') &&
            v == r->board[b] &&
            v == r->board[c])
            return v;
    }

    return 0;
}

static int check_draw(struct Room *r) {
    for (int i = 0; i < 9; i++) {
        if (r->board[i] != 'X' && r->board[i] != 'O')
            return 0;
    }

    return 1;
}

static void start_match(struct Room *r) {
    if (!r || !r->players[0] || !r->players[1])
        return;

    reset_game(r);

    queue_message(r->players[0], "START:X");
    queue_message(r->players[1], "START:O");

    send_board(r);

    lwsl_notice(
        "Match started%s%s\n",
        r->is_private ? " in room " : "",
        r->is_private ? r->code : ""
    );
}

static void detach_player(struct Player *p) {
    struct Room *r = p->room;

    if (!r)
        return;

    int slot = p->slot;

    if (slot >= 0 && slot < 2 &&
        r->players[slot] == p) {

        r->players[slot] = NULL;
        r->active = 0;

        struct Player *other = r->players[1 - slot];

        if (other)
            queue_message(other, "OPPONENT_LEFT");
    }

    p->room = NULL;
    p->slot = -1;
    p->waiting_public = 0;

    if (!r->players[0] && !r->players[1])
        memset(r, 0, sizeof(*r));
}

static void join_room(struct Player *p, struct Room *r) {
    if (!r || r->players[1]) {
        queue_message(p, "ERROR:ROOM_FULL");
        return;
    }

    if (p->room)
        detach_player(p);

    int slot = r->players[0] ? 1 : 0;

    r->players[slot] = p;

    p->room = r;
    p->slot = slot;
    p->waiting_public = 0;

    queue_message(p, "WAIT");

    if (r->is_private) {
        char msg[32];

        snprintf(msg, sizeof(msg), "ROOM:%s", r->code);

        queue_message(p, msg);
    }

    if (r->players[0] && r->players[1])
        start_match(r);
}

static void join_public(struct Player *p) {
    if (p->room)
        detach_player(p);

    struct Room *waiting = NULL;

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].used &&
            !rooms[i].is_private &&
            rooms[i].players[0] &&
            !rooms[i].players[1] &&
            rooms[i].players[0]->waiting_public) {

            waiting = &rooms[i];
            break;
        }
    }

    if (!waiting)
        waiting = new_room(0);

    if (!waiting) {
        queue_message(p, "ERROR:SERVER_FULL");
        return;
    }

    join_room(p, waiting);

    if (p->room && p->slot == 0)
        p->waiting_public = 1;
}

static void create_private(struct Player *p) {
    if (p->room)
        detach_player(p);

    struct Room *r = new_room(1);

    if (!r) {
        queue_message(p, "ERROR:SERVER_FULL");
        return;
    }

    join_room(p, r);
}

static void join_private(struct Player *p, const char *code) {
    if (strlen(code) != ROOM_CODE_SIZE - 1) {
        queue_message(p, "ERROR:ROOM_NOT_FOUND");
        return;
    }

    for (int i = 0; i < ROOM_CODE_SIZE - 1; i++) {
        if (!isxdigit((unsigned char)code[i])) {
            queue_message(p, "ERROR:ROOM_NOT_FOUND");
            return;
        }
    }

    char normalized[ROOM_CODE_SIZE];

    for (int i = 0; i < ROOM_CODE_SIZE - 1; i++) {
        normalized[i] = (char)toupper((unsigned char)code[i]);
    }

    normalized[ROOM_CODE_SIZE - 1] = '\0';

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].used &&
            rooms[i].is_private &&
            strcmp(rooms[i].code, normalized) == 0) {

            if (rooms[i].players[1]) {
                queue_message(p, "ERROR:ROOM_FULL");
            }
            else if (p->room == &rooms[i]) {
                queue_message(p, "ERROR:ALREADY_IN_ROOM");
            }
            else {
                join_room(p, &rooms[i]);
            }

            return;
        }
    }

    queue_message(p, "ERROR:ROOM_NOT_FOUND");
}

static void handle_move(struct Player *p, const char *input) {
    struct Room *r = p->room;

    if (!r || !r->active || p->slot < 0)
        return;

    char symbol = p->slot == 0 ? 'X' : 'O';

    if (symbol != r->turn)
        return;

    char *end;

    long pos = strtol(input + 5, &end, 10);

    if (end == input + 5 || *end ||
        pos < 1 || pos > 9)
        return;

    int index = (int)pos - 1;

    if (r->board[index] == 'X' ||
        r->board[index] == 'O')
        return;

    r->board[index] = symbol;

    char winner = check_winner(r);

    if (winner) {
        send_board(r);

        char result[16];

        snprintf(result, sizeof(result), "RESULT:%c", winner);

        broadcast(r, result);

        r->active = 0;
    }
    else if (check_draw(r)) {
        send_board(r);

        broadcast(r, "RESULT:D");

        r->active = 0;
    }
    else {
        r->turn = r->turn == 'X' ? 'O' : 'X';

        send_board(r);
    }
}

static int callback_game(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
) {
    struct Player *p = (struct Player *)user;

    switch (reason) {

        case LWS_CALLBACK_ESTABLISHED:

            memset(p, 0, sizeof(*p));

            p->wsi = wsi;
            p->slot = -1;

            /* Existing browser clients use automatic matchmaking. */
            join_public(p);

            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:

            if (p->count) {
                unsigned char buffer[LWS_PRE + MESSAGE_SIZE];

                const char *msg = p->messages[p->head];

                size_t n = strlen(msg);

                memcpy(buffer + LWS_PRE, msg, n);

                int written = lws_write(
                    wsi,
                    buffer + LWS_PRE,
                    n,
                    LWS_WRITE_TEXT
                );

                if (written < 0 || (size_t)written != n)
                    return -1;

                p->head = (p->head + 1) % QUEUE_SIZE;

                p->count--;

                if (p->count)
                    lws_callback_on_writable(wsi);
            }

            break;

        case LWS_CALLBACK_RECEIVE: {

            if (len == 0 || len >= 64)
                break;

            char input[64];

            memcpy(input, in, len);

            input[len] = '\0';

            if (strncmp(input, "MOVE:", 5) == 0)
                handle_move(p, input);

            else if (strcmp(input, "FIND_MATCH") == 0)
                join_public(p);

            else if (strcmp(input, "CREATE_ROOM") == 0)
                create_private(p);

            else if (strncmp(input, "JOIN_ROOM:", 10) == 0)
                join_private(p, input + 10);

            break;
        }

        case LWS_CALLBACK_CLOSED:

            detach_player(p);

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

    if (port_env && *port_env) {
        char *end;

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

    printf(
        "Game Arena: listening on 0.0.0.0:%d, max %d rooms\n",
        port,
        MAX_ROOMS
    );

    fflush(stdout);

    while (1)
        lws_service(context, 1000);

    lws_context_destroy(context);

    return 0;
}