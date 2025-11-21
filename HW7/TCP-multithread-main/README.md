# Network Programming Homework 6

## Project Overview
This project implements a multi-threaded TCP server and client application for user authentication and message posting. The server supports multiple clients simultaneously and ensures session management with unique logins.

## Features
- **Server**:
  - Multi-threaded TCP server
  - User authentication
  - Session management
  - Unique login enforcement
  - Commands: `USER`, `POST`, `BYE`
- **Client**:
  - Connects to the server
  - Sends commands to authenticate, post messages, and logout

## File Structure
```
Makefile
README.md
TCP_Client/
  client.c
  protocol/
    protocol.c
    protocol.h
TCP_Server/
  account.txt
  server.c
  auth/
    auth.c
    auth.h
  protocol/
    protocol.c
    protocol.h
  user/
    user.c
    user.h
```

## How to Build
1. Run `make clean && make` to build the project.
2. The server executable will be `./server`.
3. The client executable will be `./client`.

## How to Run
### Server
Run the server with a port number:
```bash
./server <port>
```
Example:
```bash
./server 5500
```

### Client
Run the client with the server's IP and port:
```bash
./client <server_ip> <port>
```
Example:
```bash
./client 127.0.0.1 5500
```

## Testing
A test script `test_duplicate_login.sh` is provided to verify the server's behavior for duplicate logins.

## Notes
- Ensure `account.txt` contains valid user data.
- Debug logs have been removed for production use.

## License
This project is for educational purposes only.
