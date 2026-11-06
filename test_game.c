
#include <stdio.h>
#include "game.h"

int main() {

    Game game;

    game_init(&game);

    printf("Initial Game Board:\n");
    game_print_board(&game);

    game_make_move(&game, 1, 'X');
    game_make_move(&game, 4, 'O');
    game_make_move(&game, 2, 'X');
    game_make_move(&game, 5, 'O');
    game_make_move(&game, 3, 'X');

    printf("Final Game Board:\n");
    game_print_board(&game);

    char winner = game_get_winner(&game);

    if (winner == 'X' || winner == 'O') {
        printf("Winner: Player %c\n", winner);
    }
    else if (winner == 'D') {
        printf("Game Draw!\n");
    }
    else {
        printf("Game is still running.\n");
    }

    return 0;
}

