# CollaborativeNotepad

CollaborativeNotepad is a concurrent Server-Client application that allows for the collaborative editing of text files in real-time. Built entirely in C, it provides a terminal-based user interface and ensures data consistency across multiple users.

## Features

* **Real-Time Collaborative Editing:** Up to two clients can edit the same document simultaneously while viewing each other's cursor positions in real-time.
* **Centralized Architecture:** The server acts as the Single Source of Truth, ensuring no discrepancies exist between the document versions visible to different clients[.
* **User Authentication:** Secure login and registration system with enforced password complexity (minimum 8 characters, requiring at least one uppercase letter, one lowercase letter, and one number).
* **File Management:** Users can create, download, delete, and list available files directly from the terminal[.
* **Terminal UI:** A clean, multi-window interface built with `ncurses` that separates the document view from the command input.

## Tech Stack

* **Language:** C (POSIX standard) 
* **Networking:** TCP/IP Sockets for reliable data transmission and data integrity
* **Concurrency:** POSIX Threads (Pthreads) utilizing a Thread-per-Connection model
* **Synchronization:** Mutexes to prevent race conditions during concurrent file access 
* **Database:** SQLite3 for persistent storage of user credentials and document content 
* **UI:** `ncurses` library for terminal rendering

## Compilation and Execution

### Prerequisites
Ensure you have `gcc`, `sqlite3`, and `ncurses` installed on your Linux environment.

### Server
Compile the server:
`gcc server.c -o server -lsqlite3 -pthread`

Run the server:
`./server`
*(The server runs on port 6060 by default).*

### Client
Compile the client:
`gcc client.c -o client -lncurses`

Run the client:
`./client <server_ip_address> 6060`

## Usage Commands

Once the client is running, you can interact with the application using the following commands:

* `register <username> <password> <confirm_password>`: Register a new account
* `login <username> <password>`: Authenticate your session
* `create <filename>`: Create a new text file
* `files`: List all files stored on the server
* `edit <filename>`: Open a file in collaborative edit mode
* `download <filename> <optional_path>`: Download a file from the server to your local machine

**Edit Mode Commands:**
* `insert <position> <text>`: Insert text at a specific position (or at the current cursor if omitted)
* `delete <position> <length>`: Delete characters
* `save`: Save the current state to the database
* `exit_edit`: Exit the editing session
