# User Manual

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

During use, the program seamlessly logs data into formatted CSV files:

- data/users.csv
- data/products.csv
- data/sales.csv
- data/admin_logs.log

## 7. User Interface

The retail client utilizes a modernized, dynamic, command-line grid interface built with **libfort**, generating beautiful ASCII tables using ANSI escape cyan/magenta colors. No raw text output!
