
#ifndef GAME_H
#define GAME_H

typedef struct {
    char board[9];
    char current_player;
    int moves;
} Game;

void game_init(Game *game);

int game_make_move(Game *game, int position, char player);

char game_get_winner(const Game *game);

void game_print_board(const Game *game);

#endif
