const SERVER_URL =
    "wss://qualities-calls-taxi-investor.trycloudflare.com";

let socket = null;
let mySymbol = null;
let currentTurn = null;
let gameStarted = false;
let gameFinished = false;

const usernameInput = document.getElementById("username");
const findMatchButton = document.getElementById("find-match");
const statusMessage = document.getElementById("status-message");
const serverStatus = document.getElementById("server-status");
const matchStatus = document.getElementById("match-status");
const turnMessage = document.getElementById("turn-message");

const playerXName = document.getElementById("player-x-name");
const playerOName = document.getElementById("player-o-name");

const cells = document.querySelectorAll(".cell");

function setStatus(message) {
    statusMessage.textContent = message;
}

function setTurnMessage(message) {
    turnMessage.textContent = message;
}

function resetBoard() {
    cells.forEach(cell => {
        cell.textContent = "";
        cell.disabled = true;
        cell.classList.remove("x-cell", "o-cell");
    });
}

function updateBoard(boardString) {
    if (boardString.length !== 9) return;

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

function connectToServer() {
    const username = usernameInput.value.trim();

    if (!username) {
        setStatus("Please enter your name.");
        return;
    }

    if (socket &&
        (socket.readyState === WebSocket.OPEN ||
         socket.readyState === WebSocket.CONNECTING)) {
        setStatus("Already connected or connecting.");
        return;
    }

    mySymbol = null;
    currentTurn = null;
    gameStarted = false;
    gameFinished = false;

    resetBoard();

    matchStatus.textContent = "CONNECTING";
    setTurnMessage("CONNECTING TO SERVER");
    setStatus("Connecting to Game Arena...");

    findMatchButton.disabled = true;
    findMatchButton.textContent = "CONNECTING...";

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
            mySymbol = message.split(":")[1];

            gameStarted = true;
            gameFinished = false;

            matchStatus.textContent = "MATCH STARTED";

            if (mySymbol === "X") {
                playerXName.textContent = username;
                playerOName.textContent = "OPPONENT";
            } else {
                playerXName.textContent = "OPPONENT";
                playerOName.textContent = username;
            }

            setStatus("Match found! You are Player " + mySymbol);
            setTurnMessage("GAME STARTED");
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

            cells.forEach(cell => cell.disabled = true);
            matchStatus.textContent = "GAME OVER";

            if (result === "D") {
                setTurnMessage("IT'S A DRAW!");
                setStatus("The match ended in a draw!");
            } else if (result === mySymbol) {
                setTurnMessage("YOU WIN!");
                setStatus("Congratulations! You won!");
            } else {
                setTurnMessage("YOU LOST!");
                setStatus("Your opponent won!");
            }

            findMatchButton.disabled = false;
            findMatchButton.textContent = "PLAY AGAIN →";
        }

        else if (message === "OPPONENT_LEFT") {
            gameStarted = false;
            gameFinished = true;

            cells.forEach(cell => cell.disabled = true);

            matchStatus.textContent = "OPPONENT LEFT";
            setTurnMessage("OPPONENT DISCONNECTED");
            setStatus("Your opponent disconnected.");

            findMatchButton.disabled = false;
            findMatchButton.textContent = "FIND AN OPPONENT →";
        }

        else if (message === "ERROR:ROOM_FULL") {
            setStatus("Room full. Try again later.");
            matchStatus.textContent = "ROOM FULL";

            findMatchButton.disabled = false;
            findMatchButton.textContent = "FIND AN OPPONENT →";
        }
    });

    socket.addEventListener("error", () => {
        serverStatus.textContent = "CONNECTION ERROR";
        setStatus("Connection failed. Check your server and tunnel.");
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
    if (!socket || socket.readyState !== WebSocket.OPEN) return;
    if (!gameStarted || gameFinished) return;
    if (currentTurn !== mySymbol) return;

    socket.send("MOVE:" + position);
}

findMatchButton.addEventListener("click", connectToServer);

cells.forEach(cell => {
    cell.addEventListener("click", () => {
        makeMove(cell.dataset.position);
    });
});

serverStatus.textContent = "SERVER READY";
setStatus("Enter your name and find an opponent.");
setTurnMessage("WAITING FOR PLAYERS");

console.log("Game Arena loaded.");