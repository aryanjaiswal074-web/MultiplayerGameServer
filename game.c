
#include <stdio.h>
#include "game.h"

void game_init(Game *game) {

    for (int i = 0; i < 9; i++) {
        game->board[i] = '1' + i;
    }

    game->current_player = 'X';
    game->moves = 0;
}

int game_make_move(Game *game, int position, char player) {

    if (position < 1 || position > 9) {
        return 0;
    }

    if (player != game->current_player) {
        return 0;
    }

    if (game->board[position - 1] == 'X' ||
        game->board[position - 1] == 'O') {
        return 0;
    }

    game->board[position - 1] = player;
    game->moves++;

    if (game_get_winner(game) == ' ' && game->moves < 9) {

        game->current_player =
            (player == 'X') ? 'O' : 'X';
    }

    return 1;
}

char game_get_winner(const Game *game) {

    int wins[8][3] = {
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

        int a = wins[i][0];
        int b = wins[i][1];
        int c = wins[i][2];

        if (game->board[a] == game->board[b] &&
            game->board[b] == game->board[c] &&
            (game->board[a] == 'X' ||
             game->board[a] == 'O')) {

            return game->board[a];
        }
    }

    if (game->moves == 9) {
        return 'D';
    }

    return ' ';
}

void game_print_board(const Game *game) {

    printf("\n");

    for (int i = 0; i < 9; i++) {

        printf(" %c ", game->board[i]);

        if (i % 3 != 2) {
            printf("|");
        }

        if (i % 3 == 2 && i != 8) {
            printf("\n---+---+---\n");
        }
    }

    printf("\n\n");
}
