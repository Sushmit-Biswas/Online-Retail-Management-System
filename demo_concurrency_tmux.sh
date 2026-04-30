#!/bin/bash

echo "Compiling project..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

# Stop existing server if running just in case
killall retail_server > /dev/null 2>&1

echo "Starting tmux session for concurrency demo..."

# Kill existing tmux demo session if it exists
tmux kill-session -t retail_demo >/dev/null 2>&1

# Create new tmux session detached, name it 'retail_demo'
tmux new-session -d -s retail_demo -n concurrency

# Pane 0: Start the retail server
tmux send-keys -t retail_demo:concurrency "clear; echo '--- RETAIL SERVER ---'; ./retail_server" C-m

# Split the window into 4 panes
tmux split-window -h -t retail_demo:concurrency
tmux split-window -v -t retail_demo:concurrency.0
tmux split-window -v -t retail_demo:concurrency.2

# Wait 2 seconds for server to initialize
sleep 2

# Send client execution commands to the 3 new panes
tmux send-keys -t retail_demo:concurrency.1 "clear; echo '--- CLIENT 1 ---'; python3 auto_client.py 1" C-m
tmux send-keys -t retail_demo:concurrency.2 "clear; echo '--- CLIENT 2 ---'; python3 auto_client.py 2" C-m
tmux send-keys -t retail_demo:concurrency.3 "clear; echo '--- CLIENT 3 ---'; python3 auto_client.py 3" C-m

# Attach to the session so the user can see everything in one terminal!
tmux attach-session -t retail_demo
