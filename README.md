# Online Retail Store Management System (Linux Systems Programming)

This project is an Online Retail Store Management System built in C for Linux systems programming.

It runs in client-server mode:

- Networked client-server app (`retail_server` + `retail_client`)

## Tech Stack

- Linux runtime (tested with WSL)
- C programming language (GCC)
- File-based storage using plain text files (CSV)

Note: This implementation intentionally does not use MySQL, Apache, or a web server.

## Features Implemented

- Role-based authentication for Admin and Customer
- Customer self-registration
- Persistent user store with hashed password values
- Product inventory management (Add / Modify / Delete / Display)
- Real-time stock update when order is placed/cancelled
- Order placement with payment simulation
- Order status tracking from placement to delivery
- Customer order history and cancellation flow
- Admin order management and status updates
- Sales and revenue reporting dashboard
- Action logging for auditing
- Multi-client handling over TCP sockets
- Graceful signal-based server shutdown
- IPC-based payment worker simulation
- Thread-safe synchronization for shared inventory and orders

## Linux Systems Concept Coverage

- Process Management: Implemented (payment worker uses `fork()` lifecycle)
- Inter-Process Communication (IPC): Implemented (payment worker and server communicate via `pipe()`)
- Signals: Implemented (`SIGINT` / `SIGTERM` graceful shutdown, `SIGPIPE` ignored)
- File I/O and File Operations: Implemented (persistent stores for users, products, orders, logs)
- Networking: Implemented (TCP socket server and client)
- Multithreading: Implemented (`pthread_create` per client connection)
- Synchronization: Implemented (`pthread_mutex` locks protect shared state and prevent race conditions)

## Folder Structure

- src/: Source code modules
- include/: Header files
- data/: Runtime data files
- docs/: Design, installation, and user manuals

## Build and Run

1. Build:
   make clean && make

2. Run networked server:
   ./retail_server

3. Run networked client:
   ./retail_client

4. Automated Concurrency Demo (Visual):
   ./demo_concurrency_tmux.sh

## Default Credentials

- Username: admin
- Password: admin123

This default admin is auto-created only when no active admin exists in data/users.txt.

## Data Files

- data/users.csv: User records
- data/products.csv: Product inventory records
- data/sales.csv: Order and payment records
- data/admin_logs.log: Audit logs

## Deliverables Included

- Functional client-server retail management system
- Design documentation and User Manual
