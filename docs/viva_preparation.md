# OS Lab Viva Preparation Guide
**Project:** Online Retail Store Management System

This guide outlines **where**, **how**, and **why** core Operating System concepts are implemented in the project. Use this as a reference to answer examiner questions confidently.

## 1. File Locking
*   **Where:** In `src/retail_server.c` (lines 198-224), specifically in the custom wrapper functions `acquire_file_lock()` and `release_file_lock()`.
*   **How:** Uses the POSIX `fcntl()` system call. It initializes a `struct flock` and sets its `l_type` to either `F_RDLCK` (read lock) or `F_WRLCK` (write lock) depending on whether the system is reading or writing data. After the operation is finished, it calls `F_UNLCK`.
*   **Why:** Since the project uses flat CSV files (`users.csv`, `products.csv`, `sales.csv`) instead of a robust database engine like MySQL, there must be a way to protect data on disk. If two clients try to place an order at the exact same time, two threads might try to open and write to `sales.csv` simultaneously, leading to data corruption or overwritten records. `fcntl()` acts as a mandatory barrier, blocking other processes from modifying the file until the current process releases the lock.

## 2. Concurrency Control (Multithreading & Mutexes)
*   **Where:** In `src/retail_server.c`. Thread creation happens around line 1638 (`pthread_create(&thread_id, NULL, client_worker, client_fd)`). Mutexes are defined globally at the top (e.g., `pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;`).
*   **How:** Implemented using the POSIX Threads (`pthreads`) library.
    *   **Threads:** Every time a new client connects via TCP, the server `accept()`s the connection and spins up a new thread (`pthread_create`) to handle that specific client. This allows the main server loop to go back to listening for more incoming clients.
    *   **Mutexes:** Since all threads share the same global arrays in memory (like `g_products`), any code that reads or modifies this shared memory is wrapped with `pthread_mutex_lock(&g_db_mutex)` and `pthread_mutex_unlock(&g_db_mutex)`.
*   **Why:** Multithreading allows the server to be asynchronous and handle multiple clients concurrently without making them wait in a queue. Mutexes (Mutual Exclusion) are required to prevent **race conditions**. Without mutexes, two threads could read the stock of an item as `1` simultaneously, both decrease it, and write it back, resulting in negative stock and a lost update.

## 3. Inter-Process Communication (IPC) & Process Control
*   **Where:** In `src/server_payment.c` inside the `run_payment_process()` function.
*   **How:** Uses `fork()` to create a child process and `pipe()` to create two one-way communication channels (`request_pipe` and `response_pipe`).
    *   The parent thread writes the payment details to the `request_pipe` and waits.
    *   The child process reads from it, simulates payment processing (calculating "PENDING", "PAID", or "FAILED" based on the method), and writes the result back to the `response_pipe` before exiting with `_exit(0)`.
    *   Crucially, the parent thread calls `waitpid()` to catch the child's exit status.
*   **Why:**
    *   **Why Fork?** To offload tasks. Simulating a payment gateway might involve network delays or heavy processing. By forking a separate process, you ensure that if the payment process crashes or hangs, it doesn't crash or block the main server thread handling the user.
    *   **Why Waitpid?** If a child process finishes but the parent doesn't acknowledge it, the child becomes a **Zombie Process** (occupying a slot in the OS process table without doing anything). `waitpid` ensures the parent cleans up the child's resources properly.

## 4. Data Consistency
*   **Where:** Across `src/retail_server.c`, specifically when processing user purchases.
*   **How:** Achieved by combining two synchronization primitives: **Mutexes** (for memory consistency) and **File Locks** (for disk consistency). When a user buys something, the system locks the `g_db_mutex`, checks if `stock > 0`, decrements the stock in memory, writes the updated stock to the file using an `F_WRLCK` file lock, and then unlocks the mutex.
*   **Why:** This guarantees **Atomicity**. The entire process of checking stock, updating memory, and writing to the file happens as one unbreakable sequence. This proves to the examiner that the system is fully protected against "Dirty Reads" and "Lost Updates".

## 5. Socket Programming (Network IPC)
*   **Where:** `src/retail_server.c` and `src/server_net.c`.
*   **How:** Implemented using a standard TCP Client-Server architecture with `<sys/socket.h>`. The server uses `socket()`, `bind()` to port `9090`, and `listen()`. It then sits in an infinite loop calling `accept()`. Clients use `connect()` to hook into the server and exchange data using `send()` and `recv()`.
*   **Why:** This fulfills the distributed nature of the OS lab requirement. It allows different terminals (or even different computers on the same network) to interact with the centralized store simultaneously.

---

### 🔥 Pro-Tip for the Viva: Mention your Automated Testing Script

If the examiner asks: *"How do you know your concurrency controls actually work?"*

You should immediately point to your `demo_concurrency_tmux.sh` and `auto_client.py` scripts. Tell them:
> *"I proved it using an automated test. I wrote a bash script utilizing `tmux` to fire up three automated Python clients simultaneously. They all tried to buy the exact same item at the exact same millisecond. My server successfully processed the first two requests, decremented the stock to 0, and safely rejected the third client with an 'Insufficient stock' error. This practically proves my Mutexes and File Locks successfully prevented a race condition."*
