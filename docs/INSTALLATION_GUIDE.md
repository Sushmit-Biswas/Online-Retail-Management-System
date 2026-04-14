# Installation Guide

## Prerequisites

- Linux environment (native Linux or WSL)
- GCC compiler
- Make

On Ubuntu/WSL:

sudo apt update
sudo apt install -y build-essential

> **Note**: The client now compiles automatically with the `libfort` library (C source embedded in `src/fort.c`). Ensure you build the project cleanly anytime changes are made.

## Steps

1. Navigate to project directory.
2. Build project:

make clean && make

3. Run server (Terminal 1):

./retail_server

4. Run client (Terminal 2):

./retail_client

## Optional: Run Automatic Demo

chmod +x demo_watch_full.sh
./demo_watch_full.sh

## Optional: Run Socket QA

chmod +x qa_socket.sh
./qa_socket.sh

## Cleaning Build Artifacts

make clean

## Troubleshooting

- If binary does not run, rebuild with make clean && make.
- If data files are missing, they are recreated automatically at runtime.
- If no admin exists, default admin is auto-created:
  - username: admin
  - password: admin123
