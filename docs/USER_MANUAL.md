# User Manual (OS Lab Project: Retail System)

This manual describes the operation of the Online Retail Store Management System. This is an **Operating Systems Lab** project built using pure C and Linux systems programming (Sockets, Pthreads, IPC Pipes, and POSIX File Locking). It does not use external database engines.

## 0. Start the System

1. Build the project:

make clean && make

2. Start server in Terminal 1:

./retail_server

3. Start client in Terminal 2:

./retail_client

## 1. Login Screen

Available options:

- 1: Admin Login
- 2: Customer Login
- 3: Register as Customer
- 0: Exit

## 2. Admin Functions

After admin login:

- Add Product
  - Enter name, category, price, stock.
- Modify Product
  - Enter product ID and update fields.
- Delete Product
  - Enter product ID to remove.
- Display Inventory
  - View all products and low-stock indicators.
- View/Update Orders
  - Inspect all customer orders.
  - Update status using valid progression.
- View Sales & Revenue Report
  - View order counts, revenue, pending payments, inventory metrics.
- Create New Admin Account
  - Add another admin user.
- View Sys Logs
  - View recorded user/system actions.
- Logout

## 3. Customer Functions

After customer login:

- Display Inventory
- Place Order
  - Select product ID and quantity.
  - Choose payment method: UPI, Card, COD.
  - Confirm order placement.
- View My Orders
- Cancel Order
  - Allowed for eligible statuses only.
- Logout

## 4. Payment Notes

- UPI and Card are marked paid immediately.
- COD remains pending until order is delivered.

## 5. Order Tracking Notes

Common order statuses:

- PLACED
- CONFIRMED
- SHIPPED
- DELIVERED
- CANCELLED

## 6. Files Generated

During use, the program logs data into formatted CSV files:

- data/users.csv
- data/products.csv
- data/sales.csv
- data/admin_logs.log

## 7. Troubleshooting & OS Notes

- **Port already in use**: If the server fails to start with "Address already in use", wait 30 seconds for the kernel to clear the socket or kill the previous process using `fuser -k 9090/tcp`.
- **Zombie Processes**: If you see `<defunct>` processes in `top`, ensure the server is properly cleaning up the payment worker child processes (implemented via `waitpid`).
- **File Locks**: If the program hangs, it might be waiting for a file lock (`fcntl`). Ensure no other process is holding a write lock on the `.csv` files.
- **IPC Failures**: Payment simulation requires successful `pipe()` creation. If simulation fails, check system resource limits.

## 8. Visual Concurrency Demo

To demonstrate the system's ability to handle multiple simultaneous transactions correctly (satisfying the OS Lab requirements), you can run the automated tmux-based demo:

1. Ensure `tmux` and `python3` are installed in your WSL/Linux environment.
2. Run the demo script:
   ```bash
   ./demo_concurrency_tmux.sh
   ```

This will automatically:
- Split your terminal into 4 panes (1 Server, 3 Clients).
- Register 3 demo users simultaneously.
- Attempt to place orders for the same product at the exact same time.
- Verify that order IDs are unique and stock is correctly decremented.

*Note: Press `Ctrl+B` then `D` to exit the tmux demo view.*
