#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SERVER_PORT 9090
#define SERVER_BACKLOG 32
#define MAX_LINE 2048
#define MAX_USERS 1024
#define MAX_PRODUCTS 2048
#define MAX_ORDERS 4096
#define ROLE_ADMIN 1
#define ROLE_CUSTOMER 2

typedef struct {
    char username[50];
    unsigned long password_hash;
    int role;
    int active;
} User;

typedef struct {
    int id;
    char name[100];
    char category[50];
    float price;
    int stock;
} Product;

typedef struct {
    int id;
    char username[50];
    int product_id;
    char product_name[100];
    int quantity;
    float unit_price;
    float total_amount;
    char payment_method[16];
    char payment_status[24];
    char order_status[24];
    char timestamp[32];
} Order;

typedef struct {
    int fd;
    int authenticated;
    int role;
    char username[50];
} ClientSession;

static const char *USERS_FILE = "data/users.csv";
static const char *PRODUCTS_FILE = "data/products.csv";
static const char *ORDERS_FILE = "data/sales.csv";
static const char *LOG_FILE = "data/admin_logs.log";

static User g_users[MAX_USERS];
static Product g_products[MAX_PRODUCTS];
static Order g_orders[MAX_ORDERS];

static int g_user_count = 0;
static int g_product_count = 0;
static int g_order_count = 0;

static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile sig_atomic_t g_running = 1;
static int g_listen_fd = -1;

static void now_timestamp(char *out, size_t out_size) {
    time_t now;
    struct tm *tm_info;

    time(&now);
    tm_info = localtime(&now);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static unsigned long hash_password(const char *password) {
    const char *salt = "oslab-retail-v2";
    unsigned long hash = 5381;
    const unsigned char *cursor = (const unsigned char *)salt;

    while (*cursor != '\0') {
        hash = ((hash << 5) + hash) + *cursor;
        cursor++;
    }

    cursor = (const unsigned char *)password;
    while (*cursor != '\0') {
        hash = ((hash << 5) + hash) + *cursor;
        cursor++;
    }

    return hash;
}

static int send_all(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t written = send(fd, buffer + sent, length - sent, 0);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (written == 0) {
            return 0;
        }
        sent += (size_t)written;
    }

    return 1;
}

static int send_linef(int fd, const char *fmt, ...) {
    char buffer[MAX_LINE];
    size_t len;
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    len = strlen(buffer);
    if (len == 0 || buffer[len - 1] != '\n') {
        if (len + 1 >= sizeof(buffer)) {
            return 0;
        }
        buffer[len] = '\n';
        buffer[len + 1] = '\0';
        len++;
    }

    return send_all(fd, buffer, len);
}

static int recv_line(int fd, char *buffer, size_t size) {
    size_t idx = 0;

    while (idx < size - 1) {
        char ch;
        ssize_t result = recv(fd, &ch, 1, 0);

        if (result == 0) {
            if (idx == 0) {
                return 0;
            }
            break;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }

        buffer[idx++] = ch;
    }

    buffer[idx] = '\0';
    return 1;
}

static int is_safe_token(const char *value) {
    size_t i;
    size_t len = strlen(value);

    if (len == 0) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (value[i] == ',' || value[i] == '\n' || value[i] == '\r') {
            return 0;
        }
    }

    return 1;
}

static int is_valid_username(const char *username) {
    size_t i;
    size_t len = strlen(username);

    if (len < 3 || len > 24) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        char ch = username[i];
        int is_lower = (ch >= 'a' && ch <= 'z');
        int is_upper = (ch >= 'A' && ch <= 'Z');
        int is_digit = (ch >= '0' && ch <= '9');
        int is_allowed_symbol = (ch == '_' || ch == '-');

        if (!(is_lower || is_upper || is_digit || is_allowed_symbol)) {
            return 0;
        }
    }

    return 1;
}

static int parse_user_line(const char *line, User *user) {
    char hash_str[64];
    char role_str[16];
    char active_str[16];

    if (sscanf(
            line,
            "%49[^,],%63[^,],%15[^,],%15[^\n]",
            user->username,
            hash_str,
            role_str,
            active_str) != 4) {
        return 0;
    }

    user->password_hash = strtoul(hash_str, NULL, 10);
    user->role = atoi(role_str);
    user->active = atoi(active_str);
    return 1;
}

static int parse_product_line(const char *line, Product *product) {
    return sscanf(
               line,
               "%d,%99[^,],%49[^,],%f,%d",
               &product->id,
               product->name,
               product->category,
               &product->price,
               &product->stock) == 5;
}

static int parse_order_line(const char *line, Order *order) {
    return sscanf(
               line,
               "%d,%49[^,],%d,%99[^,],%d,%f,%f,%15[^,],%23[^,],%23[^,],%31[^\n]",
               &order->id,
               order->username,
               &order->product_id,
               order->product_name,
               &order->quantity,
               &order->unit_price,
               &order->total_amount,
               order->payment_method,
               order->payment_status,
               order->order_status,
               order->timestamp) == 11;
}

static void ensure_data_files(void) {
    FILE *file;

    mkdir("data", 0777);

    file = fopen(USERS_FILE, "a+");
    if (file != NULL) {
        fclose(file);
    }

    file = fopen(PRODUCTS_FILE, "a+");
    if (file != NULL) {
        fclose(file);
    }

    file = fopen(ORDERS_FILE, "a+");
    if (file != NULL) {
        fclose(file);
    }

    file = fopen(LOG_FILE, "a+");
    if (file != NULL) {
        fclose(file);
    }
}

static void load_users_locked(void) {
    FILE *file;
    char line[256];

    g_user_count = 0;
    file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        User user;
        if (parse_user_line(line, &user) && g_user_count < MAX_USERS) {
            g_users[g_user_count++] = user;
        }
    }

    fclose(file);
}

static void load_products_locked(void) {
    FILE *file;
    char line[512];

    g_product_count = 0;
    file = fopen(PRODUCTS_FILE, "r");
    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        Product product;
        if (parse_product_line(line, &product) && g_product_count < MAX_PRODUCTS) {
            g_products[g_product_count++] = product;
        }
    }

    fclose(file);
}

static void load_orders_locked(void) {
    FILE *file;
    char line[700];

    g_order_count = 0;
    file = fopen(ORDERS_FILE, "r");
    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        Order order;
        if (parse_order_line(line, &order) && g_order_count < MAX_ORDERS) {
            g_orders[g_order_count++] = order;
        }
    }

    fclose(file);
}

static int save_users_locked(void) {
    FILE *file;
    int i;

    file = fopen(USERS_FILE, "w");
    if (file == NULL) {
        return 0;
    }

    for (i = 0; i < g_user_count; i++) {
        fprintf(file, "%s,%lu,%d,%d\n", g_users[i].username, g_users[i].password_hash, g_users[i].role, g_users[i].active);
    }

    fclose(file);
    return 1;
}

static int save_products_locked(void) {
    FILE *file;
    int i;

    file = fopen(PRODUCTS_FILE, "w");
    if (file == NULL) {
        return 0;
    }

    for (i = 0; i < g_product_count; i++) {
        fprintf(
            file,
            "%d,%s,%s,%.2f,%d\n",
            g_products[i].id,
            g_products[i].name,
            g_products[i].category,
            g_products[i].price,
            g_products[i].stock);
    }

    fclose(file);
    return 1;
}

static int save_orders_locked(void) {
    FILE *file;
    int i;

    file = fopen(ORDERS_FILE, "w");
    if (file == NULL) {
        return 0;
    }

    for (i = 0; i < g_order_count; i++) {
        fprintf(
            file,
            "%d,%s,%d,%s,%d,%.2f,%.2f,%s,%s,%s,%s\n",
            g_orders[i].id,
            g_orders[i].username,
            g_orders[i].product_id,
            g_orders[i].product_name,
            g_orders[i].quantity,
            g_orders[i].unit_price,
            g_orders[i].total_amount,
            g_orders[i].payment_method,
            g_orders[i].payment_status,
            g_orders[i].order_status,
            g_orders[i].timestamp);
    }

    fclose(file);
    return 1;
}

static void log_action_server(const char *username, const char *action) {
    FILE *file;
    char ts[32];

    pthread_mutex_lock(&g_log_mutex);

    file = fopen(LOG_FILE, "a");
    if (file != NULL) {
        now_timestamp(ts, sizeof(ts));
        fprintf(file, "[%s] User: %s , Action: %s\n", ts, username, action);
        fclose(file);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

static int find_user_index_locked(const char *username) {
    int i;
    for (i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_product_index_locked(int product_id) {
    int i;
    for (i = 0; i < g_product_count; i++) {
        if (g_products[i].id == product_id) {
            return i;
        }
    }
    return -1;
}

static int find_order_index_locked(int order_id) {
    int i;
    for (i = 0; i < g_order_count; i++) {
        if (g_orders[i].id == order_id) {
            return i;
        }
    }
    return -1;
}

static int next_product_id_locked(void) {
    int i;
    int max_id = 0;

    for (i = 0; i < g_product_count; i++) {
        if (g_products[i].id > max_id) {
            max_id = g_products[i].id;
        }
    }

    return max_id + 1;
}

static int next_order_id_locked(void) {
    int i;
    int max_id = 0;

    for (i = 0; i < g_order_count; i++) {
        if (g_orders[i].id > max_id) {
            max_id = g_orders[i].id;
        }
    }

    return max_id + 1;
}

static int role_from_text(const char *role_text) {
    if (strcasecmp(role_text, "ADMIN") == 0) {
        return ROLE_ADMIN;
    }
    if (strcasecmp(role_text, "CUSTOMER") == 0) {
        return ROLE_CUSTOMER;
    }
    return 0;
}

static int is_valid_status_transition(const char *current_status, const char *next_status) {
    if (strcmp(current_status, "PLACED") == 0) {
        return strcmp(next_status, "CONFIRMED") == 0 || strcmp(next_status, "CANCELLED") == 0;
    }
    if (strcmp(current_status, "CONFIRMED") == 0) {
        return strcmp(next_status, "SHIPPED") == 0 || strcmp(next_status, "CANCELLED") == 0;
    }
    if (strcmp(current_status, "SHIPPED") == 0) {
        return strcmp(next_status, "DELIVERED") == 0;
    }
    return 0;
}

static int run_payment_process(const char *method, float amount, char *payment_status, size_t payment_status_size) {
    int request_pipe[2];
    int response_pipe[2];
    pid_t pid;

    if (pipe(request_pipe) < 0 || pipe(response_pipe) < 0) {
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        close(request_pipe[0]);
        close(request_pipe[1]);
        close(response_pipe[0]);
        close(response_pipe[1]);
        return 0;
    }

    if (pid == 0) {
        char req[128] = {0};
        char method_buf[32] = {0};
        float amount_value = 0.0f;
        const char *result = "FAILED";
        ssize_t read_bytes;

        close(request_pipe[1]);
        close(response_pipe[0]);

        read_bytes = read(request_pipe[0], req, sizeof(req) - 1);
        if (read_bytes > 0) {
            req[read_bytes] = '\0';
            if (sscanf(req, "%31s %f", method_buf, &amount_value) == 2 && amount_value >= 0.0f) {
                if (strcmp(method_buf, "COD") == 0) {
                    result = "PENDING";
                } else if (strcmp(method_buf, "UPI") == 0 || strcmp(method_buf, "CARD") == 0) {
                    result = "PAID";
                }
            }
        }

        write(response_pipe[1], result, strlen(result));
        close(request_pipe[0]);
        close(response_pipe[1]);
        _exit(0);
    }

    close(request_pipe[0]);
    close(response_pipe[1]);

    {
        char request[128];
        int status_code;
        ssize_t read_bytes;

        snprintf(request, sizeof(request), "%s %.2f", method, amount);
        write(request_pipe[1], request, strlen(request));
        close(request_pipe[1]);

        read_bytes = read(response_pipe[0], payment_status, payment_status_size - 1);
        close(response_pipe[0]);

        if (read_bytes <= 0) {
            waitpid(pid, &status_code, 0);
            return 0;
        }

        payment_status[read_bytes] = '\0';
        waitpid(pid, &status_code, 0);

        if (!WIFEXITED(status_code)) {
            return 0;
        }

        return strcmp(payment_status, "FAILED") != 0;
    }
}

static int ensure_default_admin_locked(void) {
    int i;

    for (i = 0; i < g_user_count; i++) {
        if (g_users[i].role == ROLE_ADMIN && g_users[i].active == 1) {
            return 1;
        }
    }

    if (g_user_count >= MAX_USERS) {
        return 0;
    }

    snprintf(g_users[g_user_count].username, sizeof(g_users[g_user_count].username), "admin");
    g_users[g_user_count].password_hash = hash_password("admin123");
    g_users[g_user_count].role = ROLE_ADMIN;
    g_users[g_user_count].active = 1;
    g_user_count++;

    return save_users_locked();
}

static int db_initialize(void) {
    pthread_mutex_lock(&g_db_mutex);

    ensure_data_files();
    load_users_locked();
    load_products_locked();
    load_orders_locked();

    if (!ensure_default_admin_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_db_mutex);
    return 1;
}

static int require_auth(ClientSession *session) {
    if (!session->authenticated) {
        send_linef(session->fd, "ERR Please login first");
        return 0;
    }
    return 1;
}

static int require_admin(ClientSession *session) {
    if (!require_auth(session)) {
        return 0;
    }
    if (session->role != ROLE_ADMIN) {
        send_linef(session->fd, "ERR Admin privilege required");
        return 0;
    }
    return 1;
}

static int require_customer(ClientSession *session) {
    if (!require_auth(session)) {
        return 0;
    }
    if (session->role != ROLE_CUSTOMER) {
        send_linef(session->fd, "ERR Customer privilege required");
        return 0;
    }
    return 1;
}

static void handle_register(ClientSession *session, const char *username, const char *password) {
    (void)session;

    if (username == NULL || password == NULL) {
        send_linef(session->fd, "ERR Usage: REGISTER <username> <password>");
        return;
    }
    if (!is_valid_username(username)) {
        send_linef(session->fd, "ERR Username must be 3-24 chars (alnum,_,-)");
        return;
    }
    if (strlen(password) < 6) {
        send_linef(session->fd, "ERR Password must be at least 6 chars");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    if (find_user_index_locked(username) >= 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Username already exists");
        return;
    }

    if (g_user_count >= MAX_USERS) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR User limit reached");
        return;
    }

    snprintf(g_users[g_user_count].username, sizeof(g_users[g_user_count].username), "%s", username);
    g_users[g_user_count].password_hash = hash_password(password);
    g_users[g_user_count].role = ROLE_CUSTOMER;
    g_users[g_user_count].active = 1;
    g_user_count++;

    if (!save_users_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save user data");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK Customer registered");
    log_action_server(username, "Registered new customer account");
}

static void handle_login(ClientSession *session, const char *role_text, const char *username, const char *password) {
    int role;

    if (role_text == NULL || username == NULL || password == NULL) {
        send_linef(session->fd, "ERR Usage: LOGIN <ADMIN,CUSTOMER> <username> <password>");
        return;
    }

    role = role_from_text(role_text);
    if (role == 0) {
        send_linef(session->fd, "ERR Invalid role. Use ADMIN or CUSTOMER");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    {
        int user_idx = find_user_index_locked(username);
        if (user_idx < 0) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR User not found");
            return;
        }

        if (g_users[user_idx].active != 1) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR User inactive");
            return;
        }

        if (g_users[user_idx].password_hash != hash_password(password)) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR Invalid password");
            return;
        }

        if (g_users[user_idx].role != role) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR Role mismatch");
            return;
        }
    }

    pthread_mutex_unlock(&g_db_mutex);

    session->authenticated = 1;
    session->role = role;
    snprintf(session->username, sizeof(session->username), "%s", username);

    send_linef(session->fd, "OK LOGIN %s", (role == ROLE_ADMIN) ? "ADMIN" : "CUSTOMER");
    log_action_server(session->username, (role == ROLE_ADMIN) ? "Admin logged in" : "Customer logged in");
}

static void handle_logout(ClientSession *session) {
    if (session->authenticated) {
        log_action_server(session->username, "Logged out");
    }
    session->authenticated = 0;
    session->role = 0;
    memset(session->username, 0, sizeof(session->username));
    send_linef(session->fd, "OK Logged out");
}

static void handle_add_product(ClientSession *session, const char *name, const char *category, const char *price_text, const char *stock_text) {
    float price;
    int stock;
    char *endptr;

    if (!require_admin(session)) {
        return;
    }

    if (name == NULL || category == NULL || price_text == NULL || stock_text == NULL) {
        send_linef(session->fd, "ERR Usage: ADD_PRODUCT <name> <category> <price> <stock>");
        return;
    }

    if (!is_safe_token(name) || !is_safe_token(category)) {
        send_linef(session->fd, "ERR Invalid field characters");
        return;
    }

    price = strtof(price_text, &endptr);
    if (*endptr != '\0' || price <= 0.0f) {
        send_linef(session->fd, "ERR Invalid price");
        return;
    }

    stock = (int)strtol(stock_text, &endptr, 10);
    if (*endptr != '\0' || stock < 0) {
        send_linef(session->fd, "ERR Invalid stock");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    if (g_product_count >= MAX_PRODUCTS) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Product capacity reached");
        return;
    }

    for (int i = 0; i < g_product_count; i++) {
        if (strcasecmp(g_products[i].name, name) == 0) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR Product with this name already exists");
            return;
        }
    }

    g_products[g_product_count].id = next_product_id_locked();
    snprintf(g_products[g_product_count].name, sizeof(g_products[g_product_count].name), "%s", name);
    snprintf(g_products[g_product_count].category, sizeof(g_products[g_product_count].category), "%s", category);
    g_products[g_product_count].price = price;
    g_products[g_product_count].stock = stock;
    g_product_count++;

    if (!save_products_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save products");
        return;
    }

    {
        int new_id = g_products[g_product_count - 1].id;
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "OK Product added id=%d", new_id);
    }

    {
        char action[200];
        snprintf(action, sizeof(action), "Added product name=%s category=%s", name, category);
        log_action_server(session->username, action);
    }
}

static void handle_modify_product(ClientSession *session, const char *id_text, const char *name, const char *category, const char *price_text, const char *stock_text) {
    int id;
    int stock;
    float price;
    int product_idx;
    char *endptr;

    if (!require_admin(session)) {
        return;
    }

    if (id_text == NULL || name == NULL || category == NULL || price_text == NULL || stock_text == NULL) {
        send_linef(session->fd, "ERR Usage: MODIFY_PRODUCT <id> <name> <category> <price> <stock>");
        return;
    }

    if (!is_safe_token(name) || !is_safe_token(category)) {
        send_linef(session->fd, "ERR Invalid field characters");
        return;
    }

    id = (int)strtol(id_text, &endptr, 10);
    if (*endptr != '\0' || id <= 0) {
        send_linef(session->fd, "ERR Invalid product id");
        return;
    }

    price = strtof(price_text, &endptr);
    if (*endptr != '\0' || (price <= 0.0f && price != -1.0f)) {
        send_linef(session->fd, "ERR Invalid price");
        return;
    }

    stock = (int)strtol(stock_text, &endptr, 10);
    if (*endptr != '\0' || stock < -1) {
        send_linef(session->fd, "ERR Invalid stock");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    product_idx = find_product_index_locked(id);
    if (product_idx < 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Product not found");
        return;
    }

    if (strcmp(name, ".") != 0) {
        snprintf(g_products[product_idx].name, sizeof(g_products[product_idx].name), "%s", name);
    }
    if (strcmp(category, ".") != 0) {
        snprintf(g_products[product_idx].category, sizeof(g_products[product_idx].category), "%s", category);
    }
    if (price != -1.0f) {
        g_products[product_idx].price = price;
    }
    if (stock != -1) {
        g_products[product_idx].stock = stock;
    }

    if (!save_products_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save products");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK Product updated");
    {
        char action[180];
        snprintf(action, sizeof(action), "Modified product id=%d", id);
        log_action_server(session->username, action);
    }
}

static void handle_delete_product(ClientSession *session, const char *id_text) {
    int id;
    int idx;
    int i;
    char *endptr;

    if (!require_admin(session)) {
        return;
    }

    if (id_text == NULL) {
        send_linef(session->fd, "ERR Usage: DELETE_PRODUCT <id>");
        return;
    }

    id = (int)strtol(id_text, &endptr, 10);
    if (*endptr != '\0' || id <= 0) {
        send_linef(session->fd, "ERR Invalid product id");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    idx = find_product_index_locked(id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Product not found");
        return;
    }

    for (i = idx; i < g_product_count - 1; i++) {
        g_products[i] = g_products[i + 1];
    }
    g_product_count--;

    /* Rearrange IDs numerically from 1, 2, ... */
    for (i = 0; i < g_product_count; i++) {
        g_products[i].id = i + 1;
    }

    if (!save_products_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save products");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK Product deleted");
    {
        char action[160];
        snprintf(action, sizeof(action), "Deleted product id=%d", id);
        log_action_server(session->username, action);
    }
}

static void handle_list_products(ClientSession *session) {
    int i;

    if (!require_auth(session)) {
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    send_linef(session->fd, "OK PRODUCTS %d", g_product_count);
    for (i = 0; i < g_product_count; i++) {
        send_linef(
            session->fd,
            "DATA %d,%s,%s,%.2f,%d",
            g_products[i].id,
            g_products[i].name,
            g_products[i].category,
            g_products[i].price,
            g_products[i].stock);
    }
    send_linef(session->fd, "END");

    pthread_mutex_unlock(&g_db_mutex);
}

static void handle_place_order(ClientSession *session, const char *product_id_text, const char *qty_text, const char *payment_method) {
    int product_id;
    int quantity;
    int product_idx;
    int order_id;
    float total_amount;
    float unit_price;
    char product_name[100];
    char payment_status[24] = {0};
    char *endptr;

    if (!require_customer(session)) {
        return;
    }

    if (product_id_text == NULL || qty_text == NULL || payment_method == NULL) {
        send_linef(session->fd, "ERR Usage: PLACE_ORDER <product_id> <quantity> <UPI,CARD,COD>");
        return;
    }

    product_id = (int)strtol(product_id_text, &endptr, 10);
    if (*endptr != '\0' || product_id <= 0) {
        send_linef(session->fd, "ERR Invalid product id");
        return;
    }

    quantity = (int)strtol(qty_text, &endptr, 10);
    if (*endptr != '\0' || quantity <= 0) {
        send_linef(session->fd, "ERR Invalid quantity");
        return;
    }

    if (!(strcmp(payment_method, "UPI") == 0 || strcmp(payment_method, "CARD") == 0 || strcmp(payment_method, "COD") == 0)) {
        send_linef(session->fd, "ERR Invalid payment method");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    product_idx = find_product_index_locked(product_id);
    if (product_idx < 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Product not found");
        return;
    }

    if (g_products[product_idx].stock < quantity) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Insufficient stock (available=%d)", g_products[product_idx].stock);
        return;
    }

    if (g_order_count >= MAX_ORDERS) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Order capacity reached");
        return;
    }

    g_products[product_idx].stock -= quantity;
    unit_price = g_products[product_idx].price;
    total_amount = unit_price * (float)quantity;
    snprintf(product_name, sizeof(product_name), "%s", g_products[product_idx].name);
    order_id = next_order_id_locked();

    if (!save_products_locked()) {
        g_products[product_idx].stock += quantity;
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to reserve stock");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    if (!run_payment_process(payment_method, total_amount, payment_status, sizeof(payment_status))) {
        pthread_mutex_lock(&g_db_mutex);
        product_idx = find_product_index_locked(product_id);
        if (product_idx >= 0) {
            g_products[product_idx].stock += quantity;
            save_products_locked();
        }
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Payment processing failed");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    if (g_order_count >= MAX_ORDERS) {
        product_idx = find_product_index_locked(product_id);
        if (product_idx >= 0) {
            g_products[product_idx].stock += quantity;
            save_products_locked();
        }
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Order capacity reached");
        return;
    }

    g_orders[g_order_count].id = order_id;
    snprintf(g_orders[g_order_count].username, sizeof(g_orders[g_order_count].username), "%s", session->username);
    g_orders[g_order_count].product_id = product_id;
    snprintf(g_orders[g_order_count].product_name, sizeof(g_orders[g_order_count].product_name), "%s", product_name);
    g_orders[g_order_count].quantity = quantity;
    g_orders[g_order_count].unit_price = unit_price;
    g_orders[g_order_count].total_amount = total_amount;
    snprintf(g_orders[g_order_count].payment_method, sizeof(g_orders[g_order_count].payment_method), "%s", payment_method);
    snprintf(g_orders[g_order_count].payment_status, sizeof(g_orders[g_order_count].payment_status), "%s", payment_status);
    snprintf(g_orders[g_order_count].order_status, sizeof(g_orders[g_order_count].order_status), "PLACED");
    now_timestamp(g_orders[g_order_count].timestamp, sizeof(g_orders[g_order_count].timestamp));
    g_order_count++;

    if (!save_orders_locked()) {
        product_idx = find_product_index_locked(product_id);
        if (product_idx >= 0) {
            g_products[product_idx].stock += quantity;
            save_products_locked();
        }
        g_order_count--;
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save order");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK ORDER_PLACED id=%d payment=%s status=PLACED", order_id, payment_status);
    {
        char action[220];
        snprintf(action, sizeof(action), "Placed order id=%d product=%d qty=%d", order_id, product_id, quantity);
        log_action_server(session->username, action);
    }
}

static void handle_my_orders(ClientSession *session) {
    int i;
    int count = 0;

    if (!require_customer(session)) {
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    for (i = 0; i < g_order_count; i++) {
        if (strcmp(g_orders[i].username, session->username) == 0) {
            count++;
        }
    }

    send_linef(session->fd, "OK MY_ORDERS %d", count);

    for (i = 0; i < g_order_count; i++) {
        if (strcmp(g_orders[i].username, session->username) == 0) {
            send_linef(
                session->fd,
                "DATA %d,%d,%s,%d,%.2f,%s,%s,%s",
                g_orders[i].id,
                g_orders[i].product_id,
                g_orders[i].product_name,
                g_orders[i].quantity,
                g_orders[i].total_amount,
                g_orders[i].payment_status,
                g_orders[i].order_status,
                g_orders[i].timestamp);
        }
    }

    send_linef(session->fd, "END");
    pthread_mutex_unlock(&g_db_mutex);
}

static void handle_cancel_order(ClientSession *session, const char *order_id_text) {
    int order_id;
    int order_idx;
    int product_idx;
    char *endptr;

    if (!require_customer(session)) {
        return;
    }

    if (order_id_text == NULL) {
        send_linef(session->fd, "ERR Usage: CANCEL_ORDER <order_id>");
        return;
    }

    order_id = (int)strtol(order_id_text, &endptr, 10);
    if (*endptr != '\0' || order_id <= 0) {
        send_linef(session->fd, "ERR Invalid order id");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    order_idx = find_order_index_locked(order_id);
    if (order_idx < 0 || strcmp(g_orders[order_idx].username, session->username) != 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Order not found for this user");
        return;
    }

    if (strcmp(g_orders[order_idx].order_status, "DELIVERED") == 0 ||
        strcmp(g_orders[order_idx].order_status, "SHIPPED") == 0 ||
        strcmp(g_orders[order_idx].order_status, "CANCELLED") == 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Cannot cancel order at current status=%s", g_orders[order_idx].order_status);
        return;
    }

    snprintf(g_orders[order_idx].order_status, sizeof(g_orders[order_idx].order_status), "CANCELLED");
    if (strcmp(g_orders[order_idx].payment_status, "PAID") == 0) {
        snprintf(g_orders[order_idx].payment_status, sizeof(g_orders[order_idx].payment_status), "REFUND_INITIATED");
    } else {
        snprintf(g_orders[order_idx].payment_status, sizeof(g_orders[order_idx].payment_status), "CANCELLED");
    }

    product_idx = find_product_index_locked(g_orders[order_idx].product_id);
    if (product_idx >= 0) {
        g_products[product_idx].stock += g_orders[order_idx].quantity;
    }

    if (!save_products_locked() || !save_orders_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to persist cancellation");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK Order cancelled");
    {
        char action[160];
        snprintf(action, sizeof(action), "Cancelled order id=%d", order_id);
        log_action_server(session->username, action);
    }
}

static void handle_all_orders(ClientSession *session) {
    int i;

    if (!require_admin(session)) {
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    send_linef(session->fd, "OK ALL_ORDERS %d", g_order_count);
    for (i = 0; i < g_order_count; i++) {
        send_linef(
            session->fd,
            "DATA %d,%s,%d,%s,%d,%.2f,%s,%s,%s",
            g_orders[i].id,
            g_orders[i].username,
            g_orders[i].product_id,
            g_orders[i].product_name,
            g_orders[i].quantity,
            g_orders[i].total_amount,
            g_orders[i].payment_status,
            g_orders[i].order_status,
            g_orders[i].timestamp);
    }
    send_linef(session->fd, "END");

    pthread_mutex_unlock(&g_db_mutex);
}

static void handle_update_order(ClientSession *session, const char *order_id_text, const char *new_status) {
    int order_id;
    int order_idx;
    int product_idx;
    char *endptr;

    if (!require_admin(session)) {
        return;
    }

    if (order_id_text == NULL || new_status == NULL) {
        send_linef(session->fd, "ERR Usage: UPDATE_ORDER <order_id> <CONFIRMED,SHIPPED,DELIVERED,CANCELLED>");
        return;
    }

    if (!(strcmp(new_status, "CONFIRMED") == 0 || strcmp(new_status, "SHIPPED") == 0 ||
          strcmp(new_status, "DELIVERED") == 0 || strcmp(new_status, "CANCELLED") == 0)) {
        send_linef(session->fd, "ERR Invalid status");
        return;
    }

    order_id = (int)strtol(order_id_text, &endptr, 10);
    if (*endptr != '\0' || order_id <= 0) {
        send_linef(session->fd, "ERR Invalid order id");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    order_idx = find_order_index_locked(order_id);
    if (order_idx < 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Order not found");
        return;
    }

    if (strcmp(new_status, "CANCELLED") == 0) {
        if (strcmp(g_orders[order_idx].order_status, "PLACED") != 0 &&
            strcmp(g_orders[order_idx].order_status, "CONFIRMED") != 0) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR Only PLACED/CONFIRMED orders can be cancelled");
            return;
        }

        snprintf(g_orders[order_idx].order_status, sizeof(g_orders[order_idx].order_status), "CANCELLED");
        if (strcmp(g_orders[order_idx].payment_status, "PAID") == 0) {
            snprintf(g_orders[order_idx].payment_status, sizeof(g_orders[order_idx].payment_status), "REFUND_INITIATED");
        } else {
            snprintf(g_orders[order_idx].payment_status, sizeof(g_orders[order_idx].payment_status), "CANCELLED");
        }

        product_idx = find_product_index_locked(g_orders[order_idx].product_id);
        if (product_idx >= 0) {
            g_products[product_idx].stock += g_orders[order_idx].quantity;
        }
    } else {
        if (!is_valid_status_transition(g_orders[order_idx].order_status, new_status)) {
            pthread_mutex_unlock(&g_db_mutex);
            send_linef(session->fd, "ERR Invalid status transition %s->%s", g_orders[order_idx].order_status, new_status);
            return;
        }

        snprintf(g_orders[order_idx].order_status, sizeof(g_orders[order_idx].order_status), "%s", new_status);

        if (strcmp(new_status, "DELIVERED") == 0 &&
            strcmp(g_orders[order_idx].payment_method, "COD") == 0 &&
            strcmp(g_orders[order_idx].payment_status, "PENDING") == 0) {
            snprintf(g_orders[order_idx].payment_status, sizeof(g_orders[order_idx].payment_status), "PAID");
        }
    }

    if (!save_products_locked() || !save_orders_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save order updates");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK Order updated");
    {
        char action[180];
        snprintf(action, sizeof(action), "Updated order id=%d new_status=%s", order_id, new_status);
        log_action_server(session->username, action);
    }
}

static void handle_report(ClientSession *session) {
    int i;
    int total_orders = 0;
    int delivered_orders = 0;
    int cancelled_orders = 0;
    int low_stock_items = 0;
    double gross_revenue = 0.0;
    double collected_revenue = 0.0;
    double pending_payments = 0.0;
    double inventory_value = 0.0;

    if (!require_admin(session)) {
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    for (i = 0; i < g_order_count; i++) {
        total_orders++;
        if (strcmp(g_orders[i].order_status, "CANCELLED") == 0) {
            cancelled_orders++;
            continue;
        }

        gross_revenue += g_orders[i].total_amount;

        if (strcmp(g_orders[i].order_status, "DELIVERED") == 0) {
            delivered_orders++;
        }

        if (strcmp(g_orders[i].payment_status, "PAID") == 0) {
            collected_revenue += g_orders[i].total_amount;
        } else if (strcmp(g_orders[i].payment_status, "PENDING") == 0) {
            pending_payments += g_orders[i].total_amount;
        }
    }

    for (i = 0; i < g_product_count; i++) {
        inventory_value += (double)g_products[i].price * (double)g_products[i].stock;
        if (g_products[i].stock <= 5) {
            low_stock_items++;
        }
    }

    send_linef(
        session->fd,
        "OK REPORT total_orders=%d delivered=%d cancelled=%d gross=%.2f collected=%.2f pending=%.2f inventory_items=%d low_stock=%d inventory_value=%.2f",
        total_orders,
        delivered_orders,
        cancelled_orders,
        gross_revenue,
        collected_revenue,
        pending_payments,
        g_product_count,
        low_stock_items,
        inventory_value);

    pthread_mutex_unlock(&g_db_mutex);
}

static void handle_create_admin(ClientSession *session, const char *username, const char *password) {
    if (!require_admin(session)) {
        return;
    }

    if (username == NULL || password == NULL) {
        send_linef(session->fd, "ERR Usage: CREATE_ADMIN <username> <password>");
        return;
    }

    if (!is_valid_username(username)) {
        send_linef(session->fd, "ERR Invalid username format");
        return;
    }

    if (strlen(password) < 6) {
        send_linef(session->fd, "ERR Password must be at least 6 chars");
        return;
    }

    pthread_mutex_lock(&g_db_mutex);

    if (find_user_index_locked(username) >= 0) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Username already exists");
        return;
    }

    if (g_user_count >= MAX_USERS) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR User capacity reached");
        return;
    }

    snprintf(g_users[g_user_count].username, sizeof(g_users[g_user_count].username), "%s", username);
    g_users[g_user_count].password_hash = hash_password(password);
    g_users[g_user_count].role = ROLE_ADMIN;
    g_users[g_user_count].active = 1;
    g_user_count++;

    if (!save_users_locked()) {
        pthread_mutex_unlock(&g_db_mutex);
        send_linef(session->fd, "ERR Failed to save users");
        return;
    }

    pthread_mutex_unlock(&g_db_mutex);

    send_linef(session->fd, "OK Admin created");
    {
        char action[180];
        snprintf(action, sizeof(action), "Created admin account=%s", username);
        log_action_server(session->username, action);
    }
}

static void handle_view_logs(ClientSession *session) {
    FILE *file;
    char line[256];

    if (!require_admin(session)) {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    file = fopen(LOG_FILE, "r");
    if (file == NULL) {
        pthread_mutex_unlock(&g_log_mutex);
        send_linef(session->fd, "ERR Unable to open logs");
        return;
    }

    send_linef(session->fd, "OK LOGS");
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        send_linef(session->fd, "DATA %s", line);
    }
    send_linef(session->fd, "END");

    fclose(file);
    pthread_mutex_unlock(&g_log_mutex);
}

static int handle_client_command(ClientSession *session, char *line) {
    char *saveptr = NULL;
    char *cmd = strtok_r(line, " \t", &saveptr);

    if (cmd == NULL) {
        send_linef(session->fd, "ERR Empty command");
        return 0;
    }

    if (strcasecmp(cmd, "PING") == 0) {
        send_linef(session->fd, "OK PONG");
        return 0;
    }

    if (strcasecmp(cmd, "REGISTER") == 0) {
        char *username = strtok_r(NULL, " \t", &saveptr);
        char *password = strtok_r(NULL, " \t", &saveptr);
        handle_register(session, username, password);
        return 0;
    }

    if (strcasecmp(cmd, "LOGIN") == 0) {
        char *role = strtok_r(NULL, " \t", &saveptr);
        char *username = strtok_r(NULL, " \t", &saveptr);
        char *password = strtok_r(NULL, " \t", &saveptr);
        handle_login(session, role, username, password);
        return 0;
    }

    if (strcasecmp(cmd, "LOGOUT") == 0) {
        handle_logout(session);
        return 0;
    }

    if (strcasecmp(cmd, "ADD_PRODUCT") == 0) {
        char *name = strtok_r(NULL, " \t", &saveptr);
        char *category = strtok_r(NULL, " \t", &saveptr);
        char *price = strtok_r(NULL, " \t", &saveptr);
        char *stock = strtok_r(NULL, " \t", &saveptr);
        handle_add_product(session, name, category, price, stock);
        return 0;
    }

    if (strcasecmp(cmd, "MODIFY_PRODUCT") == 0) {
        char *id = strtok_r(NULL, " \t", &saveptr);
        char *name = strtok_r(NULL, " \t", &saveptr);
        char *category = strtok_r(NULL, " \t", &saveptr);
        char *price = strtok_r(NULL, " \t", &saveptr);
        char *stock = strtok_r(NULL, " \t", &saveptr);
        handle_modify_product(session, id, name, category, price, stock);
        return 0;
    }

    if (strcasecmp(cmd, "DELETE_PRODUCT") == 0) {
        handle_delete_product(session, strtok_r(NULL, " \t", &saveptr));
        return 0;
    }

    if (strcasecmp(cmd, "LIST_PRODUCTS") == 0) {
        handle_list_products(session);
        return 0;
    }

    if (strcasecmp(cmd, "PLACE_ORDER") == 0) {
        char *product_id = strtok_r(NULL, " \t", &saveptr);
        char *quantity = strtok_r(NULL, " \t", &saveptr);
        char *payment_method = strtok_r(NULL, " \t", &saveptr);
        handle_place_order(session, product_id, quantity, payment_method);
        return 0;
    }

    if (strcasecmp(cmd, "MY_ORDERS") == 0) {
        handle_my_orders(session);
        return 0;
    }

    if (strcasecmp(cmd, "CANCEL_ORDER") == 0) {
        handle_cancel_order(session, strtok_r(NULL, " \t", &saveptr));
        return 0;
    }

    if (strcasecmp(cmd, "ALL_ORDERS") == 0) {
        handle_all_orders(session);
        return 0;
    }

    if (strcasecmp(cmd, "UPDATE_ORDER") == 0) {
        char *order_id = strtok_r(NULL, " \t", &saveptr);
        char *status = strtok_r(NULL, " \t", &saveptr);
        handle_update_order(session, order_id, status);
        return 0;
    }

    if (strcasecmp(cmd, "REPORT") == 0) {
        handle_report(session);
        return 0;
    }

    if (strcasecmp(cmd, "CREATE_ADMIN") == 0) {
        char *username = strtok_r(NULL, " \t", &saveptr);
        char *password = strtok_r(NULL, " \t", &saveptr);
        handle_create_admin(session, username, password);
        return 0;
    }

    if (strcasecmp(cmd, "VIEW_LOGS") == 0) {
        handle_view_logs(session);
        return 0;
    }

    if (strcasecmp(cmd, "QUIT") == 0) {
        send_linef(session->fd, "OK BYE");
        return 1;
    }

    send_linef(session->fd, "ERR Unknown command");
    return 0;
}

static void *client_worker(void *arg) {
    int fd = *((int *)arg);
    char line[MAX_LINE];
    ClientSession session;

    free(arg);

    session.fd = fd;
    session.authenticated = 0;
    session.role = 0;
    memset(session.username, 0, sizeof(session.username));

    send_linef(fd, "OK WELCOME RetailServer");

    while (g_running) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc <= 0) {
            break;
        }

        if (strlen(line) == 0) {
            continue;
        }

        if (handle_client_command(&session, line)) {
            break;
        }
    }

    close(fd);
    return NULL;
}

static void signal_handler(int signo) {
    (void)signo;
    g_running = 0;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

int main(void) {
    struct sockaddr_in addr;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    if (!db_initialize()) {
        fprintf(stderr, "Failed to initialize data store.\n");
        return 1;
    }

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("socket");
        return 1;
    }

    {
        int reuse = 1;
        setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_listen_fd);
        return 1;
    }

    if (listen(g_listen_fd, SERVER_BACKLOG) < 0) {
        perror("listen");
        close(g_listen_fd);
        return 1;
    }

    printf("Retail server listening on port %d\n", SERVER_PORT);
    printf("Press Ctrl+C to stop gracefully.\n");

    while (g_running) {
        int *client_fd = (int *)malloc(sizeof(int));
        pthread_t thread_id;

        if (client_fd == NULL) {
            continue;
        }

        *client_fd = accept(g_listen_fd, NULL, NULL);
        if (*client_fd < 0) {
            free(client_fd);
            if (!g_running) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        if (pthread_create(&thread_id, NULL, client_worker, client_fd) != 0) {
            close(*client_fd);
            free(client_fd);
            continue;
        }

        pthread_detach(thread_id);
    }

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
    }

    printf("\nRetail server stopped.\n");
    return 0;
}
