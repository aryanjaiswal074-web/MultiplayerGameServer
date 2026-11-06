
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MSG_WAIT 1
#define MSG_START 2
#define MSG_MOVE 3
#define MSG_BOARD 4
#define MSG_TURN 5
#define MSG_RESULT 6
#define MSG_ERROR 7

#define BOARD_SIZE 9
#define MAX_MESSAGE 128

typedef struct {

    int type;
    int position;

    char symbol;
    char board[BOARD_SIZE];
    char result;

    char text[MAX_MESSAGE];

} Message;

#endif
