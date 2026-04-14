#include "common.h"
#include "fort.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/socket.h>

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 9090
#define MAX_LINE 2048
#define ROLE_ADMIN 1
#define ROLE_CUSTOMER 2

typedef struct {
    int fd;
    int authenticated;
    int role;
    char username[50];
} ClientState;

static void trim_newline_local(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
}

static void print_header_local(const char *title) {
    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_DOUBLE2_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_write_ln(table, title);
    printf("\n%s%s%s%s\n", BOLD, BLUE, ft_to_string(table), RESET);
    ft_destroy_table(table);
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
        ssize_t rc = recv(fd, &ch, 1, 0);

        if (rc == 0) {
            if (idx == 0) {
                return 0;
            }
            break;
        }
        if (rc < 0) {
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

static void read_line_prompt(const char *prompt, char *buffer, size_t size) {
    printf("%s", prompt);
    if (fgets(buffer, (int)size, stdin) == NULL) {
        buffer[0] = '\0';
        return;
    }
    trim_newline_local(buffer);
}

static int read_int_prompt(const char *prompt, int min, int max) {
    char input[64];
    char *endptr;
    long value;

    while (1) {
        read_line_prompt(prompt, input, sizeof(input));
        if (strlen(input) == 0) {
            printf("%s%sInput cannot be empty.%s\n", BOLD, RED, RESET);
            continue;
        }

        value = strtol(input, &endptr, 10);
        if (*endptr != '\0') {
            printf("%s%sInvalid input. Enter a number.%s\n", BOLD, RED, RESET);
            continue;
        }

        if (value < min || value > max) {
            printf("%s%sEnter a value between %d and %d.%s\n", BOLD, RED, min, max, RESET);
            continue;
        }

        return (int)value;
    }
}

static float read_float_prompt(const char *prompt, float min, float max) {
    char input[64];
    char *endptr;
    double value;

    while (1) {
        read_line_prompt(prompt, input, sizeof(input));
        if (strlen(input) == 0) {
            printf("%s%sInput cannot be empty.%s\n", BOLD, RED, RESET);
            continue;
        }

        value = strtod(input, &endptr);
        if (*endptr != '\0') {
            printf("%s%sInvalid input. Enter a decimal value.%s\n", BOLD, RED, RESET);
            continue;
        }

        if (value < min || value > max) {
            printf("%s%sEnter value between %.2f and %.2f.%s\n", BOLD, RED, min, max, RESET);
            continue;
        }

        return (float)value;
    }
}

static void normalize_token(char *text) {
    size_t i;
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == ' ') {
            text[i] = '_';
        }
    }
}

static int connect_server(const char *ip, int port) {
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

static int expect_single_response(int fd, char *line_out, size_t line_out_size) {
    int rc = recv_line(fd, line_out, line_out_size);
    if (rc <= 0) {
        printf("%s%sConnection closed by server.%s\n", BOLD, RED, RESET);
        return 0;
    }

    if (strncmp(line_out, "OK ", 3) == 0) {
        printf("%s%s%s%s\n", BOLD, GREEN, line_out + 3, RESET);
    } else if (strncmp(line_out, "ERR ", 4) == 0) {
        printf("%s%s%s%s\n", BOLD, RED, line_out + 4, RESET);
    } else {
        printf("%s%s%s%s\n", BOLD, YELLOW, line_out, RESET);
    }

    return 1;
}

static void show_admin_menu(void) {
    print_header_local("Retail Client (Admin)");
    printf("\n%s%s[1]%s Add Product\n", BOLD, MAGENTA, RESET);
    printf("%s%s[2]%s Modify Product\n", BOLD, YELLOW, RESET);
    printf("%s%s[3]%s Delete Product\n", BOLD, RED, RESET);
    printf("%s%s[4]%s List Products\n", BOLD, CYAN, RESET);
    printf("%s%s[5]%s View All Orders\n", BOLD, BRIGHT_BLUE, RESET);
    printf("%s%s[6]%s Update Order Status\n", BOLD, GREEN, RESET);
    printf("%s%s[7]%s View Sales Report\n", BOLD, YELLOW, RESET);
    printf("%s%s[8]%s Create Admin\n", BOLD, CYAN, RESET);
    printf("%s%s[9]%s View Logs\n", BOLD, MAGENTA, RESET);
    printf("%s%s[0]%s Logout\n", BOLD, RED, RESET);
}

static void show_customer_menu(void) {
    print_header_local("Retail Client (Customer)");
    printf("\n%s%s[1]%s List Products\n", BOLD, CYAN, RESET);
    printf("%s%s[2]%s Place Order\n", BOLD, GREEN, RESET);
    printf("%s%s[3]%s View My Orders\n", BOLD, MAGENTA, RESET);
    printf("%s%s[4]%s Cancel Order\n", BOLD, YELLOW, RESET);
    printf("%s%s[0]%s Logout\n", BOLD, RED, RESET);
}

static void handle_list_products(int fd) {
    char line[MAX_LINE];

    if (!send_linef(fd, "LIST_PRODUCTS")) {
        return;
    }

    if (recv_line(fd, line, sizeof(line)) <= 0) {
        return;
    }

    if (strncmp(line, "ERR", 3) == 0) {
        printf("%s%s%s%s\n", BOLD, RED, line, RESET);
        return;
    }

    if (strncmp(line, "OK ", 3) == 0) {
        printf("\n%s%s%s%s\n", BOLD, GREEN, line + 3, RESET);
    } else {
        printf("\n%s%s%s%s\n", BOLD, GREEN, line, RESET);
    }
    
    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_write_ln(table, "ID", "NAME", "CATEGORY", "PRICE", "STOCK");

    while (1) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc <= 0) {
            break;
        }
        if (strcmp(line, "END") == 0) {
            break;
        }

        if (strncmp(line, "DATA ", 5) == 0) {
            int id;
            int stock;
            float price;
            char name[100];
            char category[50];

            if (sscanf(line + 5, "%d,%99[^,],%49[^,],%f,%d", &id, name, category, &price, &stock) == 5) {
                char price_str[32];
                char id_str[32];
                char stock_str[32];
                snprintf(price_str, sizeof(price_str), "%.2f", price);
                snprintf(id_str, sizeof(id_str), "%d", id);
                snprintf(stock_str, sizeof(stock_str), "%d", stock);
                ft_write_ln(table, id_str, name, category, price_str, stock_str);
            }
        }
    }
    
    printf("%s%s%s\n", BOLD, CYAN, ft_to_string(table));
    printf("%s", RESET);
    ft_destroy_table(table);
}

static void handle_my_orders(int fd) {
    char line[MAX_LINE];

    if (!send_linef(fd, "MY_ORDERS")) {
        return;
    }

    if (recv_line(fd, line, sizeof(line)) <= 0) {
        return;
    }

    if (strncmp(line, "ERR", 3) == 0) {
        printf("%s%s%s%s\n", BOLD, RED, line, RESET);
        return;
    }

    if (strncmp(line, "OK ", 3) == 0) {
        printf("\n%s%s%s%s\n", BOLD, GREEN, line + 3, RESET);
    } else {
        printf("\n%s%s%s%s\n", BOLD, GREEN, line, RESET);
    }
    
    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_write_ln(table, "ID", "PID", "PRODUCT", "QTY", "TOTAL", "PAYMENT", "STATUS");

    while (1) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc <= 0) {
            break;
        }
        if (strcmp(line, "END") == 0) {
            break;
        }

        if (strncmp(line, "DATA ", 5) == 0) {
            int id;
            int pid;
            int qty;
            float total;
            char product[100];
            char payment[24];
            char status[24];
            char ts[32];

            if (sscanf(line + 5, "%d,%d,%99[^,],%d,%f,%23[^,],%23[^,],%31[^\n]", &id, &pid, product, &qty, &total, payment, status, ts) == 8) {
                char id_str[32], pid_str[32], qty_str[32], total_str[32];
                snprintf(id_str, sizeof(id_str), "%d", id);
                snprintf(pid_str, sizeof(pid_str), "%d", pid);
                snprintf(qty_str, sizeof(qty_str), "%d", qty);
                snprintf(total_str, sizeof(total_str), "%.2f", total);
                ft_write_ln(table, id_str, pid_str, product, qty_str, total_str, payment, status);
            }
        }
    }
    
    printf("%s%s%s\n", BOLD, CYAN, ft_to_string(table));
    printf("%s", RESET);
    ft_destroy_table(table);
}

static void handle_all_orders(int fd) {
    char line[MAX_LINE];

    if (!send_linef(fd, "ALL_ORDERS")) {
        return;
    }

    if (recv_line(fd, line, sizeof(line)) <= 0) {
        return;
    }

    if (strncmp(line, "ERR", 3) == 0) {
        printf("%s%s%s%s\n", BOLD, RED, line, RESET);
        return;
    }

    if (strncmp(line, "OK ", 3) == 0) {
        printf("\n%s%s%s%s\n", BOLD, GREEN, line + 3, RESET);
    } else {
        printf("\n%s%s%s%s\n", BOLD, GREEN, line, RESET);
    }
    
    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_write_ln(table, "ID", "USER", "PID", "PRODUCT", "QTY", "TOTAL", "PAYMENT", "STATUS");

    while (1) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc <= 0) {
            break;
        }
        if (strcmp(line, "END") == 0) {
            break;
        }

        if (strncmp(line, "DATA ", 5) == 0) {
            int id;
            int pid;
            int qty;
            float total;
            char user[50];
            char product[100];
            char payment[24];
            char status[24];
            char ts[32];

            if (sscanf(line + 5, "%d,%49[^,],%d,%99[^,],%d,%f,%23[^,],%23[^,],%31[^\n]", &id, user, &pid, product, &qty, &total, payment, status, ts) == 9) {
                char id_str[32], pid_str[32], qty_str[32], total_str[32];
                snprintf(id_str, sizeof(id_str), "%d", id);
                snprintf(pid_str, sizeof(pid_str), "%d", pid);
                snprintf(qty_str, sizeof(qty_str), "%d", qty);
                snprintf(total_str, sizeof(total_str), "%.2f", total);
                ft_write_ln(table, id_str, user, pid_str, product, qty_str, total_str, payment, status);
            }
        }
    }
    
    printf("%s%s%s\n", BOLD, CYAN, ft_to_string(table));
    printf("%s", RESET);
    ft_destroy_table(table);
}

static void handle_view_logs(int fd) {
    char line[MAX_LINE];

    if (!send_linef(fd, "VIEW_LOGS")) {
        return;
    }

    if (recv_line(fd, line, sizeof(line)) <= 0) {
        return;
    }

    if (strncmp(line, "ERR", 3) == 0) {
        printf("%s%s%s%s\n", BOLD, RED, line, RESET);
        return;
    }

    print_header_local("System Action Logs");

    ft_table_t *table = ft_create_table();
    ft_set_border_style(table, FT_SOLID_STYLE);
    ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_write_ln(table, "TIMESTAMP", "USERNAME", "ACTION");

    while (1) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc <= 0) {
            break;
        }
        if (strcmp(line, "END") == 0) {
            break;
        }

        if (strncmp(line, "DATA ", 5) == 0) {
            char ts[32] = "";
            char user[64] = "";
            char action[256] = "";

            if (sscanf(line + 5, "[%19[^]]] User: %49s , Action: %255[^\n]", ts, user, action) == 3) {
                if (strlen(action) > 50) {
                    action[47] = '.'; action[48] = '.'; action[49] = '.'; action[50] = '\0';
                }
                ft_write_ln(table, ts, user, action);
            } else {
                char fallback[90] = "";
                snprintf(fallback, sizeof(fallback), "%s", line + 5);
                ft_write_ln(table, "", "", fallback);
            }
        }
    }
    
    printf("%s%s\n", BOLD, MAGENTA);
    printf("%s", ft_to_string(table));
    printf("%s", RESET);
    ft_destroy_table(table);
}

static void handle_report(int fd) {
    char line[MAX_LINE];
    int t_orders, d_orders, c_orders, items, low;
    float gross, coll, pend, value;

    if (!send_linef(fd, "REPORT")) {
        return;
    }

    if (recv_line(fd, line, sizeof(line)) <= 0) {
        return;
    }

    if (strncmp(line, "ERR ", 4) == 0) {
        printf("%s%s%s%s\n", BOLD, RED, line, RESET);
        return;
    }

    if (sscanf(line, "OK REPORT total_orders=%d delivered=%d cancelled=%d gross=%f collected=%f pending=%f inventory_items=%d low_stock=%d inventory_value=%f",
               &t_orders, &d_orders, &c_orders, &gross, &coll, &pend, &items, &low, &value) == 9) {
        print_header_local("Sales & Inventory Report");
        
        ft_table_t *table = ft_create_table();
        ft_set_border_style(table, FT_SOLID_STYLE);
        
        char buf1[64], buf2[64], buf3[64], buf4[64], buf5[64], buf6[64], buf7[64], buf8[64], buf9[64];
        snprintf(buf1, sizeof(buf1), "%d", t_orders);
        snprintf(buf2, sizeof(buf2), "%d", d_orders);
        snprintf(buf3, sizeof(buf3), "%d", c_orders);
        snprintf(buf4, sizeof(buf4), "$%.2f", gross);
        snprintf(buf5, sizeof(buf5), "$%.2f", coll);
        snprintf(buf6, sizeof(buf6), "$%.2f", pend);
        snprintf(buf7, sizeof(buf7), "%d", items);
        snprintf(buf8, sizeof(buf8), "%d", low);
        snprintf(buf9, sizeof(buf9), "$%.2f", value);

        ft_write_ln(table, "Total Orders Placed", buf1);
        ft_write_ln(table, "Total Orders Delivered", buf2);
        ft_write_ln(table, "Total Orders Cancelled", buf3);
        
        ft_add_separator(table);
        ft_write_ln(table, "Gross Expected Revenue", buf4);
        ft_write_ln(table, "Total Revenue Collected", buf5);
        ft_write_ln(table, "Total Payment Pending", buf6);
        
        ft_add_separator(table);
        ft_write_ln(table, "Unique Inventory Items", buf7);
        ft_write_ln(table, "Items with Low Stock (0)", buf8);
        ft_write_ln(table, "Total Inventory Value", buf9);
        
        printf("\n%s%s%s\n", BOLD, YELLOW, ft_to_string(table));
        printf("%s", RESET);
        ft_destroy_table(table);

    } else {
        printf("\n%s%sSales Report%s\n", BOLD, BRIGHT_BLUE, RESET);
        printf("%s\n", line);
    }
}

static void admin_session_loop(ClientState *state) {
    while (state->authenticated && state->role == ROLE_ADMIN) {
        int choice;
        char line[MAX_LINE];

        show_admin_menu();
        choice = read_int_prompt("\nEnter choice: ", 0, 9);

        if (choice == 1) {
            char name[100];
            char category[50];
            float price;
            int stock;

            read_line_prompt("Product name (no spaces; use _): ", name, sizeof(name));
            read_line_prompt("Category (no spaces; use _): ", category, sizeof(category));
            if (strlen(name) == 0 || strlen(category) == 0) {
                printf("%s%sName/category cannot be empty.%s\n", BOLD, RED, RESET);
                continue;
            }
            normalize_token(name);
            normalize_token(category);
            price = read_float_prompt("Price: ", 0.01f, 1000000.0f);
            stock = read_int_prompt("Stock: ", 0, 1000000);

            send_linef(state->fd, "ADD_PRODUCT %s %s %.2f %d", name, category, price, stock);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else if (choice == 2) {
            int id;
            char name[100];
            char category[50];
            float price;
            int stock;

            id = read_int_prompt("Product ID: ", 1, 1000000000);
            read_line_prompt("New name (. to keep): ", name, sizeof(name));
            read_line_prompt("New category (. to keep): ", category, sizeof(category));
            if (strlen(name) == 0 || strlen(category) == 0) {
                printf("%s%sName/category cannot be empty.%s\n", BOLD, RED, RESET);
                continue;
            }
            normalize_token(name);
            normalize_token(category);
            price = read_float_prompt("New price (-1 to keep): ", -1.0f, 1000000.0f);
            stock = read_int_prompt("New stock (-1 to keep): ", -1, 1000000);

            send_linef(state->fd, "MODIFY_PRODUCT %d %s %s %.2f %d", id, name, category, price, stock);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else if (choice == 3) {
            int id = read_int_prompt("Product ID to delete: ", 1, 1000000000);
            send_linef(state->fd, "DELETE_PRODUCT %d", id);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else if (choice == 4) {
            handle_list_products(state->fd);
        } else if (choice == 5) {
            handle_all_orders(state->fd);
        } else if (choice == 6) {
            int order_id;
            int status_choice;
            const char *new_status;

            order_id = read_int_prompt("Order ID: ", 1, 1000000000);
            printf("1. CONFIRMED\n2. SHIPPED\n3. DELIVERED\n4. CANCELLED\n");
            status_choice = read_int_prompt("Choose status: ", 1, 4);

            if (status_choice == 1) {
                new_status = "CONFIRMED";
            } else if (status_choice == 2) {
                new_status = "SHIPPED";
            } else if (status_choice == 3) {
                new_status = "DELIVERED";
            } else {
                new_status = "CANCELLED";
            }

            send_linef(state->fd, "UPDATE_ORDER %d %s", order_id, new_status);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else if (choice == 7) {
            handle_report(state->fd);
        } else if (choice == 8) {
            char username[50];
            char password[100];

            read_line_prompt("New admin username: ", username, sizeof(username));
            read_line_prompt("New admin password: ", password, sizeof(password));
            if (strlen(username) == 0 || strlen(password) == 0) {
                printf("%s%sUsername/password cannot be empty.%s\n", BOLD, RED, RESET);
                continue;
            }
            normalize_token(username);
            send_linef(state->fd, "CREATE_ADMIN %s %s", username, password);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else if (choice == 9) {
            handle_view_logs(state->fd);
        } else {
            send_linef(state->fd, "LOGOUT");
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
            state->authenticated = 0;
            state->role = 0;
            memset(state->username, 0, sizeof(state->username));
            break;
        }
    }
}

static void customer_session_loop(ClientState *state) {
    while (state->authenticated && state->role == ROLE_CUSTOMER) {
        int choice;
        char line[MAX_LINE];

        show_customer_menu();
        choice = read_int_prompt("\nEnter choice: ", 0, 4);

        if (choice == 1) {
            handle_list_products(state->fd);
        } else if (choice == 2) {
            int product_id;
            int quantity;
            int method_choice;
            const char *method;

            product_id = read_int_prompt("Product ID: ", 1, 1000000000);
            quantity = read_int_prompt("Quantity: ", 1, 1000000);
            printf("1. UPI\n2. CARD\n3. COD\n");
            method_choice = read_int_prompt("Payment method: ", 1, 3);

            if (method_choice == 1) {
                method = "UPI";
            } else if (method_choice == 2) {
                method = "CARD";
            } else {
                method = "COD";
            }

            send_linef(state->fd, "PLACE_ORDER %d %d %s", product_id, quantity, method);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else if (choice == 3) {
            handle_my_orders(state->fd);
        } else if (choice == 4) {
            int order_id = read_int_prompt("Order ID to cancel: ", 1, 1000000000);
            send_linef(state->fd, "CANCEL_ORDER %d", order_id);
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
        } else {
            send_linef(state->fd, "LOGOUT");
            if (!expect_single_response(state->fd, line, sizeof(line))) {
                state->authenticated = 0;
                break;
            }
            state->authenticated = 0;
            state->role = 0;
            memset(state->username, 0, sizeof(state->username));
            break;
        }
    }
}

int main(void) {
    ClientState state;
    char line[MAX_LINE];

    memset(&state, 0, sizeof(state));

    state.fd = connect_server(DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT);
    if (state.fd < 0) {
        fprintf(stderr, "Unable to connect to %s:%d\n", DEFAULT_SERVER_IP, DEFAULT_SERVER_PORT);
        return 1;
    }

    if (recv_line(state.fd, line, sizeof(line)) > 0) {
        if (strncmp(line, "OK ", 3) == 0) {
            printf("\n%s%sConnected: %s%s\n", BOLD, BRIGHT_BLUE, line + 3, RESET);
        } else {
            printf("%s%s%s%s\n", BOLD, GREEN, line, RESET);
        }
    }

    while (1) {
        if (!state.authenticated) {
            int choice;
            char username[50];
            char password[100];

            print_header_local("Retail Socket Client");
            printf("\n%s%s[1]%s Admin Login\n", BOLD, CYAN, RESET);
            printf("%s%s[2]%s Customer Login\n", BOLD, GREEN, RESET);
            printf("%s%s[3]%s Register Customer\n", BOLD, MAGENTA, RESET);
            printf("%s%s[0]%s Exit\n", BOLD, RED, RESET);

            choice = read_int_prompt("Enter choice: ", 0, 3);

            if (choice == 0) {
                send_linef(state.fd, "QUIT");
                expect_single_response(state.fd, line, sizeof(line));
                close(state.fd);
                return 0;
            }

            if (choice == 3) {
                read_line_prompt("New username: ", username, sizeof(username));
                read_line_prompt("New password: ", password, sizeof(password));
                if (strlen(username) == 0 || strlen(password) == 0) {
                    printf("%s%sUsername/password cannot be empty.%s\n", BOLD, RED, RESET);
                    continue;
                }
                normalize_token(username);
                send_linef(state.fd, "REGISTER %s %s", username, password);
                if (!expect_single_response(state.fd, line, sizeof(line))) {
                    close(state.fd);
                    return 1;
                }
                continue;
            }

            read_line_prompt("Username: ", username, sizeof(username));
            read_line_prompt("Password: ", password, sizeof(password));
            if (strlen(username) == 0 || strlen(password) == 0) {
                printf("%s%sUsername/password cannot be empty.%s\n", BOLD, RED, RESET);
                continue;
            }
            normalize_token(username);

            if (choice == 1) {
                send_linef(state.fd, "LOGIN ADMIN %s %s", username, password);
            } else {
                send_linef(state.fd, "LOGIN CUSTOMER %s %s", username, password);
            }

            if (!expect_single_response(state.fd, line, sizeof(line))) {
                close(state.fd);
                return 1;
            }

            if (strncmp(line, "OK LOGIN", 8) == 0) {
                state.authenticated = 1;
                state.role = (choice == 1) ? ROLE_ADMIN : ROLE_CUSTOMER;
                snprintf(state.username, sizeof(state.username), "%s", username);
            }
        } else {
            if (state.role == ROLE_ADMIN) {
                admin_session_loop(&state);
            } else {
                customer_session_loop(&state);
            }
        }
    }
}


