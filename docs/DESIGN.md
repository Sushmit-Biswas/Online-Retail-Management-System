# Design Document

## 1. Problem Statement

Build a Linux systems programming based online retail store management system that supports inventory and order lifecycle management with a user-friendly interface.

## 2. Architecture

The system follows a networked Linux systems architecture:

- retail_server.c
  - TCP server (`socket`, `bind`, `listen`, `accept`) for concurrent client requests.
  - One thread per client (`pthread_create`) for multi-user handling.
  - Shared state protection via mutexes (`pthread_mutex_lock` / `pthread_mutex_unlock`).
  - Payment sub-process simulation using `fork` + `pipe` IPC.
  - Signal handling for graceful shutdown.
- retail_client.c
  - Interactive TCP client for admin/customer workflows over the socket protocol.

## 3. Persistence Model

CSV (Comma-Separated Values) format and log-file persistence are used for simplicity and portability:

- users.csv format:
  username,password_hash,role,active
- products.csv format:
  id,name,category,price,stock
- sales.csv format:
  order_id,username,product_id,product_name,quantity,unit_price,total,payment_method,payment_status,order_status,timestamp
- admin_logs.log format:
  [timestamp] User: username | Action: description

**Data Structural Updates**:
- Tables in the frontend (`retail_client`) render dynamically via the embedded `libfort` external library, showcasing `FT_SOLID_STYLE` terminal aesthetics with neon ANSI coloring (Cyan/Magenta).
- Product deletions automatically re-index IDs (`1, 2, 3...`) sequentially to maintain table consistency.

## 4. Role Model

- Admin (role=1)
  - Manage products
  - View/update all orders
  - View analytics and logs
  - Create other admin accounts
- Customer (role=2)
  - View inventory
  - Place orders
  - View/cancel own orders

## 5. Order State Machine

Valid transitions:

- PLACED -> CONFIRMED or CANCELLED
- CONFIRMED -> SHIPPED or CANCELLED
- SHIPPED -> DELIVERED
- DELIVERED and CANCELLED are terminal

## 6. Payment Model

- UPI: immediately marked PAID
- Card: immediately marked PAID (input format validated)
- COD: marked PENDING until delivered, then auto-marked PAID when admin sets status to DELIVERED

## 7. Edge Cases Handled

- Empty input for key prompts
- Invalid numeric input and out-of-range menu values
- Invalid username format or duplicate usernames
- Incorrect login credentials and max attempts
- Non-existent product/order IDs
- Out-of-stock and insufficient-stock ordering
- Invalid order status transitions
- Prevent cancellation in terminal shipping states

## 8. Security Notes

- Passwords are stored as hash values, not plaintext.
- This is educational security, not production-grade cryptographic security.

## 9. Complexity Summary

- Product lookup and order lookup are O(n) in memory arrays.
- File read/write is linear in record count.
- Fits educational project constraints and simplicity goals.

## 10. Linux Concept Mapping

- Process Management: `fork()` in payment simulation.
- IPC: `pipe()` channels between server and payment worker process.
- Signals: `SIGINT`/`SIGTERM` graceful stop, `SIGPIPE` ignored.
- File I/O: `fopen`/`fgets`/`fprintf` persistence for all stores.
- Networking: TCP socket server-client model.
- Multithreading: per-client threads with pthreads.
- Synchronization: mutex locking around shared inventory/orders/log state.
