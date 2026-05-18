# Creative Writing Practice App Prototype
A lightweight multi‑client TCP server written in C for real‑time collaborative storytelling.

## Overview
This server allows multiple clients to join shared writing sessions, contribute lines to a story, and view updates in real time. It is designed as a minimal, educational example of:
- TCP networking in C

- Concurrency with threads

- Shared state synchronization

- Command‑driven protocol design

A simple CLI client is included for testing and demonstration.

## Features

### Server
- Multi‑client TCP server written in C

- Thread‑safe shared story buffer

- Mutex‑protected session state

- Command‑based text protocol

- Logging for debugging and auditing

- Modular structure (networking, parsing, story logic)

### CLI Client
- Connects to the server via TCP

- Sends commands interactively

- Displays story updates and server responses

## Build
```
gcc -o server src/server/*.c -pthread
gcc -o client src/client/*.c -pthread
```

## Run
```
./server <port>
./client <server-ip> <port>
```

## Commands
| Command        | Description                                                       |
| -------------- | ----------------------------------------------------------------- |
| JOIN           | Registers your username with the server                           |
| SESSION CREATE | Creates a new collaborative writing session with the chosen genre |
| SESSION JOIN   | Joins an existing session                                         |
| LIST SESSIONS  | Lists all active sessions                                         |
| VIEW           | Displays the current story                                        |
| WRITE          | Adds text to the current story                                    |
| EXIT SESSION   | Leaves the current session                                        |
| QUIT           | Disconnects from the server                                       |
