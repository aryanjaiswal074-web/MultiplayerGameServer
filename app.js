const SERVER_URL =
    window.location.hostname === "localhost" ||
    window.location.hostname === "127.0.0.1"
        ? "ws://localhost:8081"
        : "wss://multiplayergameserver-sky4.onrender.com";

let socket = null;
let mySymbol = null;
let currentTurn = null;
let gameStarted = false;
let gameFinished = false;
let rematchAvailable = false;
let rematchRequested = false;
let rematchInvited = false;
let audioContext = null;
let previousBoard = "---------";

const usernameInput = document.getElementById("username");
const findMatchButton = document.getElementById("find-match");
const statusMessage = document.getElementById("status-message");
const serverStatus = document.getElementById("server-status");
const matchStatus = document.getElementById("match-status");
const turnMessage = document.getElementById("turn-message");

const playerXName = document.getElementById("player-x-name");
const playerOName = document.getElementById("player-o-name");
const boardElement = document.getElementById("board");
const cells = document.querySelectorAll(".cell");

const winningCombinations = [
    [0, 1, 2],
    [3, 4, 5],
    [6, 7, 8],
    [0, 3, 6],
    [1, 4, 7],
    [2, 5, 8],
    [0, 4, 8],
    [2, 4, 6]
];

function setStatus(message) {
    statusMessage.textContent = message;
}

function setTurnMessage(message) {
    turnMessage.textContent = message;
}

function getAudioContext() {
    const AudioContextClass =
        window.AudioContext || window.webkitAudioContext;

    if (!AudioContextClass) return null;

    if (!audioContext) {
        audioContext = new AudioContextClass();
    }

    if (audioContext.state === "suspended") {
        audioContext.resume();
    }

    return audioContext;
}

function playTone(
    frequency,
    duration,
    type = "sine",
    volume = 0.12,
    delay = 0
) {
    const context = getAudioContext();
    if (!context) return;

    const oscillator = context.createOscillator();
    const gain = context.createGain();

    const startTime = context.currentTime + delay;

    oscillator.type = type;
    oscillator.frequency.setValueAtTime(frequency, startTime);

    gain.gain.setValueAtTime(0.001, startTime);
    gain.gain.exponentialRampToValueAtTime(
        volume,
        startTime + 0.02
    );
    gain.gain.exponentialRampToValueAtTime(
        0.001,
        startTime + duration
    );

    oscillator.connect(gain);
    gain.connect(context.destination);

    oscillator.start(startTime);
    oscillator.stop(startTime + duration + 0.03);
}

function playMoveSound() {
    playTone(650, 0.09, "sine", 0.09);
}

function playVictorySound() {
    playTone(523, 0.22, "triangle", 0.13, 0);
    playTone(659, 0.22, "triangle", 0.13, 0.18);
    playTone(784, 0.22, "triangle", 0.13, 0.36);
    playTone(1047, 0.48, "triangle", 0.16, 0.54);
}

function playDefeatSound() {
    playTone(392, 0.25, "triangle", 0.12, 0);
    playTone(330, 0.30, "triangle", 0.12, 0.22);
    playTone(262, 0.50, "triangle", 0.13, 0.46);
}

function playDrawSound() {
    playTone(440, 0.18, "sine", 0.10, 0);
    playTone(440, 0.18, "sine", 0.10, 0.25);
}

function resetBoard() {
    previousBoard = "---------";

    cells.forEach(cell => {
        cell.textContent = "";
        cell.disabled = true;

        cell.classList.remove(
            "x-cell",
            "o-cell",
            "winning-cell"
        );
    });

    clearWinningLine();
}

function findWinningCombination(boardString) {
    for (const combination of winningCombinations) {
        const [a, b, c] = combination;

        if (
            boardString[a] !== "-" &&
            boardString[a] === boardString[b] &&
            boardString[b] === boardString[c]
        ) {
            return combination;
        }
    }

    return null;
}

function clearWinningLine() {
    const oldLine = document.getElementById("winning-line");

    if (oldLine) {
        oldLine.remove();
    }
}

function drawWinningLine(combination) {
    clearWinningLine();

    if (!combination) return;

    const firstCell = cells[combination[0]];
    const lastCell = cells[combination[2]];

    if (!firstCell || !lastCell || !boardElement) return;

    combination.forEach(index => {
        cells[index].classList.add("winning-cell");
    });

    const boardRect = boardElement.getBoundingClientRect();
    const firstRect = firstCell.getBoundingClientRect();
    const lastRect = lastCell.getBoundingClientRect();

    const startX =
        firstRect.left + firstRect.width / 2 - boardRect.left;

    const startY =
        firstRect.top + firstRect.height / 2 - boardRect.top;

    const endX =
        lastRect.left + lastRect.width / 2 - boardRect.left;

    const endY =
        lastRect.top + lastRect.height / 2 - boardRect.top;

    const line = document.createElement("div");
    line.id = "winning-line";

    line.style.left = startX + "px";
    line.style.top = startY + "px";

    line.style.width =
        Math.hypot(endX - startX, endY - startY) + "px";

    line.style.transform =
        `translateY(-50%) rotate(${Math.atan2(
            endY - startY,
            endX - startX
        ) * 180 / Math.PI}deg)`;

    boardElement.appendChild(line);
}

function updateBoard(boardString) {
    if (boardString.length !== 9) return;

    const hasNewMove =
        boardString.split("").filter(
            value => value === "X" || value === "O"
        ).length >
        previousBoard.split("").filter(
            value => value === "X" || value === "O"
        ).length;

    if (hasNewMove) {
        playMoveSound();
    }

    previousBoard = boardString;

    cells.forEach((cell, index) => {
        const value = boardString[index];

        cell.textContent =
            value === "X" || value === "O" ? value : "";

        cell.classList.toggle("x-cell", value === "X");
        cell.classList.toggle("o-cell", value === "O");
    });

    if (!gameStarted || gameFinished) {
        cells.forEach(cell => cell.disabled = true);
        return;
    }

    cells.forEach((cell, index) => {
        const value = boardString[index];
        const empty = value !== "X" && value !== "O";

        cell.disabled = !(currentTurn === mySymbol && empty);
    });

    if (currentTurn === mySymbol) {
        setTurnMessage("YOUR TURN");
        setStatus("Your turn. You are Player " + mySymbol);
    } else {
        setTurnMessage("OPPONENT'S TURN");
        setStatus("Waiting for your opponent...");
    }
}

/* Prepare the interface for another match. */
function prepareForMatch() {
    mySymbol = null;
    currentTurn = null;
    gameStarted = false;
    gameFinished = false;
    rematchAvailable = false;
    rematchRequested = false;
    rematchInvited = false;

    resetBoard();

    matchStatus.textContent = "SEARCHING";
    setTurnMessage("FINDING OPPONENT");
    setStatus("Searching for another player...");

    findMatchButton.disabled = true;
    findMatchButton.textContent = "SEARCHING...";
}

/* Start a new matchmaking request on an existing socket. */
function requestMatch() {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
        connectToServer();
        return;
    }

    if (rematchAvailable && gameFinished) {
        if (rematchInvited) {
            rematchInvited = false;
            rematchRequested = true;

            findMatchButton.disabled = true;
            findMatchButton.textContent = "ACCEPTING...";

            setStatus("You accepted the rematch invitation.");
            setTurnMessage("WAITING FOR MATCH");

            socket.send("REMATCH_ACCEPT");
        } else {
            rematchRequested = true;

            findMatchButton.disabled = true;
            findMatchButton.textContent = "WAITING FOR OPPONENT...";

            setStatus("Rematch request sent. Waiting for your opponent...");
            setTurnMessage("REMATCH REQUESTED");

            socket.send("REMATCH_REQUEST");
        }

        return;
    }

    prepareForMatch();
    socket.send("FIND_MATCH");
}

/* Create a new connection only when one doesn't already exist. */
function connectToServer() {
    const username = usernameInput.value.trim();

    if (!username) {
        setStatus("Please enter your name.");
        return;
    }

    if (
        socket &&
        socket.readyState === WebSocket.OPEN
    ) {
        requestMatch();
        return;
    }

    if (
        socket &&
        socket.readyState === WebSocket.CONNECTING
    ) {
        setStatus("Already connecting...");
        return;
    }

    getAudioContext();
    prepareForMatch();

    matchStatus.textContent = "CONNECTING";
    setTurnMessage("CONNECTING TO SERVER");
    setStatus("Connecting to Game Arena...");

    try {
        socket = new WebSocket(SERVER_URL, "tic-tac-toe");
    } catch (error) {
        setStatus("Could not create WebSocket connection.");
        findMatchButton.disabled = false;
        findMatchButton.textContent = "FIND AN OPPONENT →";
        return;
    }

    socket.addEventListener("open", () => {
        serverStatus.textContent = "SERVER ONLINE";
        matchStatus.textContent = "CONNECTED";
        setStatus("Connected! Searching for an opponent...");
    });

    socket.addEventListener("message", event => {
        const message = event.data;

        console.log("Server message:", message);

        if (message === "WAIT") {
            setStatus("Waiting for another player...");
            matchStatus.textContent = "WAITING";
        }

        else if (message.startsWith("START:")) {

            // Clear the previous match's board and winning line.
            resetBoard();

            mySymbol = message.split(":")[1];

            gameStarted = true;
            gameFinished = false;
            rematchAvailable = false;
            rematchRequested = false;
            rematchInvited = false;

            matchStatus.textContent = "MATCH STARTED";

            if (mySymbol === "X") {
                playerXName.textContent = usernameInput.value.trim();
                playerOName.textContent = "OPPONENT";
            } else {
                playerXName.textContent = "OPPONENT";
                playerOName.textContent = usernameInput.value.trim();
            }

            setStatus("Match found! You are Player " + mySymbol);
            setTurnMessage("GAME STARTED");

            findMatchButton.disabled = true;
            findMatchButton.textContent = "MATCH IN PROGRESS";
        }

        else if (message.startsWith("BOARD:")) {
            const parts = message.split(":");

            if (parts.length !== 3) return;

            currentTurn = parts[2];
            updateBoard(parts[1]);
        }

        else if (message.startsWith("RESULT:")) {
            const result = message.split(":")[1];

            gameFinished = true;
            gameStarted = false;
            rematchAvailable = true;
            rematchRequested = false;
            rematchInvited = false;

            cells.forEach(cell => cell.disabled = true);

            matchStatus.textContent = "GAME OVER";

            const winningCombination =
                findWinningCombination(previousBoard);

            if (result === "D") {
                setTurnMessage("IT'S A DRAW!");
                setStatus("The match ended in a draw!");
                playDrawSound();
            } else {
                drawWinningLine(winningCombination);

                if (result === mySymbol) {
                    setTurnMessage("YOU WIN!");
                    setStatus("Congratulations! You won!");
                    playVictorySound();
                } else {
                    setTurnMessage("YOU LOST!");
                    setStatus("Your opponent won!");
                    playDefeatSound();
                }
            }

            findMatchButton.disabled = false;
            findMatchButton.textContent = "PLAY AGAIN →";
        }

        else if (message === "REMATCH_WAITING") {
            rematchRequested = true;

            if (!gameStarted) {
                findMatchButton.disabled = true;
                findMatchButton.textContent = "WAITING FOR OPPONENT...";
                setStatus("Rematch request sent. Waiting for your opponent...");
                setTurnMessage("REMATCH REQUESTED");
            }
        }

        else if (message === "REMATCH_INVITE") {
            if (rematchAvailable && gameFinished && !gameStarted) {
                rematchInvited = true;
                rematchRequested = false;

                findMatchButton.disabled = false;
                findMatchButton.textContent = "ACCEPT REMATCH →";

                matchStatus.textContent = "REMATCH INVITATION";
                setTurnMessage("YOUR OPPONENT WANTS A REMATCH!");
                setStatus("Your opponent wants to play again! Accept the rematch.");
            }
        }

        else if (message === "ERROR:REMATCH_UNAVAILABLE") {
            rematchAvailable = false;
            rematchRequested = false;
            rematchInvited = false;

            setStatus("That rematch is no longer available. Find a new opponent.");
            findMatchButton.disabled = false;
            findMatchButton.textContent = "FIND AN OPPONENT →";
        }

        else if (message === "OPPONENT_LEFT") {
            gameStarted = false;
            gameFinished = true;
            rematchAvailable = false;
            rematchRequested = false;
            rematchInvited = false;

            cells.forEach(cell => cell.disabled = true);

            matchStatus.textContent = "OPPONENT LEFT";
            setTurnMessage("OPPONENT DISCONNECTED");
            setStatus("Your opponent disconnected.");

            findMatchButton.disabled = false;
            findMatchButton.textContent = "FIND AN OPPONENT →";
        }

        else if (message === "ERROR:ROOM_FULL") {
            setStatus("All rooms are full. Try again later.");
            matchStatus.textContent = "ROOM FULL";

            findMatchButton.disabled = false;
            findMatchButton.textContent = "FIND AN OPPONENT →";
        }
    });

    socket.addEventListener("error", () => {
        serverStatus.textContent = "CONNECTION ERROR";
        setStatus("Connection failed. Check your server.");
    });

    socket.addEventListener("close", event => {
        console.log("WebSocket closed:", event.code, event.reason);

        serverStatus.textContent = "SERVER OFFLINE";

        if (!gameFinished) {
            setStatus("Connection closed. Please reconnect.");
        }

        gameStarted = false;

        cells.forEach(cell => cell.disabled = true);

        findMatchButton.disabled = false;
        findMatchButton.textContent = "FIND AN OPPONENT →";
    });
}

function makeMove(position) {
    if (!socket || socket.readyState !== WebSocket.OPEN)
        return;

    if (!gameStarted || gameFinished)
        return;

    if (currentTurn !== mySymbol)
        return;

    socket.send("MOVE:" + position);
}

findMatchButton.addEventListener("click", requestMatch);

cells.forEach(cell => {
    cell.addEventListener("click", () => {
        makeMove(cell.dataset.position);
    });
});

serverStatus.textContent = "SERVER READY";
setStatus("Enter your name and find an opponent.");
setTurnMessage("WAITING FOR PLAYERS");

console.log("Game Arena loaded.");