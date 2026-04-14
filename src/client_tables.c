#include "common.h"
#include "fort.h"
#include "client_tables.h"

#include <strings.h>

#define MAX_LINE 2048

int send_linef(int fd, const char *fmt, ...);
int recv_line(int fd, char *buffer, size_t size);
void read_line_prompt(const char *prompt, char *buffer, size_t size);
void normalize_token(char *text);
void print_ok_banner_line(const char *line);
void print_header_local(const char *title);

typedef struct {
    int id;
    int stock;
    float price;
    char name[100];
    char category[50];
} ClientProduct;

static int compare_category_tokens(const void *a, const void *b) {
    const char *left = (const char *)a;
    const char *right = (const char *)b;
    return strcasecmp(left, right);
}

static void token_to_label(const char *token, char *label, size_t label_size) {
    size_t i;
    if (label_size == 0) {
        return;
    }

    for (i = 0; token[i] != '\0' && i + 1 < label_size; i++) {
        label[i] = (token[i] == '_') ? ' ' : token[i];
    }
    label[i] = '\0';
}

static void truncate_label(char *label, size_t max_len) {
    size_t len = strlen(label);
    if (max_len < 4 || len <= max_len) {
        return;
    }

    label[max_len - 3] = '.';
    label[max_len - 2] = '.';
    label[max_len - 1] = '.';
    label[max_len] = '\0';
}

void handle_list_products(int fd) {
    char line[MAX_LINE];
    ClientProduct products[2048];
    char categories[64][50];
    int count = 0;
    int category_count = 0;
    int i;

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

    print_ok_banner_line(line);

    while (1) {
        int rc = recv_line(fd, line, sizeof(line));
        if (rc <= 0) {
            break;
        }
        if (strcmp(line, "END") == 0) {
            break;
        }

        if (strncmp(line, "DATA ", 5) == 0) {
            if (count < 2048) {
                if (sscanf(line + 5, "%d,%99[^,],%49[^,],%f,%d",
                           &products[count].id,
                           products[count].name,
                           products[count].category,
                           &products[count].price,
                           &products[count].stock) == 5) {
                    int exists = 0;
                    int c;
                    for (c = 0; c < category_count; c++) {
                        if (strcasecmp(categories[c], products[count].category) == 0) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists && category_count < 64) {
                        snprintf(categories[category_count], sizeof(categories[category_count]), "%s", products[count].category);
                        category_count++;
                    }
                    count++;
                }
            }
        }
    }

    if (count == 0) {
        printf("%s%sNo products available right now.%s\n", BOLD, RED, RESET);
        return;
    }

    if (category_count > 1) {
        qsort(categories, (size_t)category_count, sizeof(categories[0]), compare_category_tokens);
    }

    if (category_count > 0) {
        printf("\n%s%sAvailable categories:%s ", BOLD, CYAN, RESET);
        for (i = 0; i < category_count; i++) {
            char category_label[64];
            token_to_label(categories[i], category_label, sizeof(category_label));
            printf("%s%s%s", YELLOW, category_label, RESET);
            if (i + 1 < category_count) {
                printf(", ");
            }
        }
        printf("\n%s%sType 'all' to show everything.%s\n", BOLD, BLUE, RESET);
    }

    {
        char filter_cat[50];
        int is_all = 0;

        while (1) {
            int valid = 0;

            read_line_prompt("Select category: ", filter_cat, sizeof(filter_cat));
            if (strlen(filter_cat) == 0) {
                printf("%s%sPlease enter a category name or 'all'.%s\n", BOLD, RED, RESET);
                continue;
            }

            normalize_token(filter_cat);
            if (strcasecmp(filter_cat, "all") == 0) {
                is_all = 1;
                break;
            }

            for (i = 0; i < category_count; i++) {
                if (strcasecmp(filter_cat, categories[i]) == 0) {
                    valid = 1;
                    break;
                }
            }

            if (valid) {
                break;
            }

            printf("%s%sUnknown category. Choose from the list above or type 'all'.%s\n", BOLD, RED, RESET);
        }

        {
            ClientProduct filtered[2048];
            int f_count = 0;
            int page_size = 10;
            int total_pages;
            int cur_page = 0;

            for (i = 0; i < count; i++) {
                if (is_all || strcasecmp(products[i].category, filter_cat) == 0) {
                    filtered[f_count++] = products[i];
                }
            }

            if (f_count == 0) {
                printf("\n%s%sNo products found in category: %s%s\n", BOLD, RED, filter_cat, RESET);
                return;
            }

            total_pages = (f_count + page_size - 1) / page_size;

            while (1) {
                int start_idx;
                int end_idx;
                ft_table_t *table = ft_create_table();
                ft_set_border_style(table, FT_SOLID_STYLE);
                ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
                ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
                ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
                ft_set_cell_prop(table, FT_ANY_ROW, 3, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
                ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
                ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_MIN_WIDTH, 4);
                ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_MIN_WIDTH, 28);
                ft_set_cell_prop(table, FT_ANY_ROW, 2, FT_CPROP_MIN_WIDTH, 14);
                ft_set_cell_prop(table, FT_ANY_ROW, 3, FT_CPROP_MIN_WIDTH, 11);
                ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_MIN_WIDTH, 7);

                ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
                ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_CONT_FG_COLOR, FT_COLOR_GREEN);
                ft_set_cell_prop(table, FT_ANY_ROW, 2, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
                ft_set_cell_prop(table, FT_ANY_ROW, 3, FT_CPROP_CONT_FG_COLOR, FT_COLOR_MAGENTA);
                ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_CONT_FG_COLOR, FT_COLOR_LIGHT_RED);
                ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CONT_FG_COLOR, FT_COLOR_DEFAULT);

                ft_u8write_ln(table, "ID", "NAME", "CATEGORY", "PRICE", "STOCK");

                start_idx = cur_page * page_size;
                end_idx = start_idx + page_size;
                if (end_idx > f_count) {
                    end_idx = f_count;
                }

                for (i = start_idx; i < end_idx; i++) {
                    char price_str[32], id_str[32], stock_str[32];
                    char name_label[64];
                    char category_label[32];

                    token_to_label(filtered[i].name, name_label, sizeof(name_label));
                    token_to_label(filtered[i].category, category_label, sizeof(category_label));
                    truncate_label(name_label, 28);
                    truncate_label(category_label, 14);

                    snprintf(price_str, sizeof(price_str), "₹%.2f", filtered[i].price);
                    snprintf(id_str, sizeof(id_str), "%d", filtered[i].id);
                    snprintf(stock_str, sizeof(stock_str), "%d", filtered[i].stock);
                    ft_u8write_ln(table, id_str, name_label, category_label, price_str, stock_str);
                }

                printf("\n%s%s--- Page %d of %d (Products %d-%d of %d) ---%s\n", BOLD, MAGENTA, cur_page + 1, total_pages, start_idx + 1, end_idx, f_count, RESET);
                printf("%s", (const char *)ft_to_u8string(table));
                printf("%s", RESET);
                ft_destroy_table(table);

                {
                    char cmd[16];
                    read_line_prompt("Enter 'n' (next 10), 'p' (prev 10), 'q' (exit to menu): ", cmd, sizeof(cmd));

                    if (cmd[0] == 'q' || cmd[0] == 'Q') {
                        break;
                    } else if (cmd[0] == 'n' || cmd[0] == 'N') {
                        if (cur_page < total_pages - 1) {
                            cur_page++;
                        } else {
                            printf("%s%sAlready on the last page!%s\n", BOLD, RED, RESET);
                        }
                    } else if (cmd[0] == 'p' || cmd[0] == 'P') {
                        if (cur_page > 0) {
                            cur_page--;
                        } else {
                            printf("%s%sAlready on the first page!%s\n", BOLD, RED, RESET);
                        }
                    } else {
                        printf("%s%sInvalid choice. Use n / p / q.%s\n", BOLD, RED, RESET);
                    }
                }
            }
        }
    }
}

void handle_my_orders(int fd) {
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

    print_ok_banner_line(line);

    {
        ft_table_t *table = ft_create_table();
        ft_set_border_style(table, FT_SOLID_STYLE);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
        ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(table, FT_ANY_ROW, 3, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);

        ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
        ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
        ft_set_cell_prop(table, FT_ANY_ROW, 2, FT_CPROP_CONT_FG_COLOR, FT_COLOR_GREEN);
        ft_set_cell_prop(table, FT_ANY_ROW, 3, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
        ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_CONT_FG_COLOR, FT_COLOR_MAGENTA);
        ft_set_cell_prop(table, FT_ANY_ROW, 5, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
        ft_set_cell_prop(table, FT_ANY_ROW, 6, FT_CPROP_CONT_FG_COLOR, FT_COLOR_LIGHT_RED);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CONT_FG_COLOR, FT_COLOR_DEFAULT);

        ft_u8write_ln(table, "ID", "PID", "PRODUCT", "QTY", "TOTAL", "PAYMENT", "STATUS");

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
                    snprintf(total_str, sizeof(total_str), "₹%.2f", total);
                    ft_u8write_ln(table, id_str, pid_str, product, qty_str, total_str, payment, status);
                }
            }
        }

        printf("%s%s%s\n", BOLD, CYAN, (const char *)ft_to_u8string(table));
        printf("%s", RESET);
        ft_destroy_table(table);
    }
}

void handle_all_orders(int fd) {
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

    print_ok_banner_line(line);

    {
        ft_table_t *table = ft_create_table();
        ft_set_border_style(table, FT_SOLID_STYLE);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
        ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(table, FT_ANY_ROW, 2, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
        ft_set_cell_prop(table, FT_ANY_ROW, 5, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);

        ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
        ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_CONT_FG_COLOR, FT_COLOR_GREEN);
        ft_set_cell_prop(table, FT_ANY_ROW, 2, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
        ft_set_cell_prop(table, FT_ANY_ROW, 3, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
        ft_set_cell_prop(table, FT_ANY_ROW, 4, FT_CPROP_CONT_FG_COLOR, FT_COLOR_MAGENTA);
        ft_set_cell_prop(table, FT_ANY_ROW, 5, FT_CPROP_CONT_FG_COLOR, FT_COLOR_MAGENTA);
        ft_set_cell_prop(table, FT_ANY_ROW, 6, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
        ft_set_cell_prop(table, FT_ANY_ROW, 7, FT_CPROP_CONT_FG_COLOR, FT_COLOR_LIGHT_RED);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CONT_FG_COLOR, FT_COLOR_DEFAULT);

        ft_u8write_ln(table, "ID", "USER", "PID", "PRODUCT", "QTY", "TOTAL", "PAYMENT", "STATUS");

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
                    snprintf(total_str, sizeof(total_str), "₹%.2f", total);
                    ft_u8write_ln(table, id_str, user, pid_str, product, qty_str, total_str, payment, status);
                }
            }
        }

        printf("%s%s%s\n", BOLD, CYAN, (const char *)ft_to_u8string(table));
        printf("%s", RESET);
        ft_destroy_table(table);
    }
}

void handle_view_logs(int fd) {
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

    {
        ft_table_t *table = ft_create_table();
        ft_set_border_style(table, FT_SOLID_STYLE);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);

        ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
        ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
        ft_set_cell_prop(table, FT_ANY_ROW, 2, FT_CPROP_CONT_FG_COLOR, FT_COLOR_GREEN);
        ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CONT_FG_COLOR, FT_COLOR_DEFAULT);

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
                        action[47] = '.';
                        action[48] = '.';
                        action[49] = '.';
                        action[50] = '\0';
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
}

void handle_report(int fd) {
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
        char buf1[64], buf2[64], buf3[64], buf4[64], buf5[64], buf6[64], buf7[64], buf8[64], buf9[64];

        print_header_local("Sales & Inventory Report");

        snprintf(buf1, sizeof(buf1), "%d", t_orders);
        snprintf(buf2, sizeof(buf2), "%d", d_orders);
        snprintf(buf3, sizeof(buf3), "%d", c_orders);
        snprintf(buf4, sizeof(buf4), "₹%.2f", gross);
        snprintf(buf5, sizeof(buf5), "₹%.2f", coll);
        snprintf(buf6, sizeof(buf6), "₹%.2f", pend);
        snprintf(buf7, sizeof(buf7), "%d", items);
        snprintf(buf8, sizeof(buf8), "%d", low);
        snprintf(buf9, sizeof(buf9), "₹%.2f", value);

        {
            ft_table_t *table = ft_create_table();
            ft_set_border_style(table, FT_SOLID_STYLE);
            ft_set_cell_prop(table, FT_ANY_ROW, 0, FT_CPROP_CONT_FG_COLOR, FT_COLOR_CYAN);
            ft_set_cell_prop(table, FT_ANY_ROW, 1, FT_CPROP_CONT_FG_COLOR, FT_COLOR_YELLOW);
            ft_set_cell_prop(table, 0, FT_ANY_COLUMN, FT_CPROP_CONT_FG_COLOR, FT_COLOR_DEFAULT);

            ft_u8write_ln(table, "Total Orders Placed", buf1);
            ft_u8write_ln(table, "Total Orders Delivered", buf2);
            ft_u8write_ln(table, "Total Orders Cancelled", buf3);

            ft_add_separator(table);
            ft_u8write_ln(table, "Gross Expected Revenue", buf4);
            ft_u8write_ln(table, "Total Revenue Collected", buf5);
            ft_u8write_ln(table, "Total Payment Pending", buf6);

            ft_add_separator(table);
            ft_u8write_ln(table, "Unique Inventory Items", buf7);
            ft_u8write_ln(table, "Items with Low Stock (<=5)", buf8);
            ft_u8write_ln(table, "Total Inventory Value", buf9);

            printf("\n%s%s%s\n", BOLD, YELLOW, (const char *)ft_to_u8string(table));
            printf("%s", RESET);
            ft_destroy_table(table);
        }
    } else {
        printf("\n%s%sSales Report%s\n", BOLD, BRIGHT_BLUE, RESET);
        printf("%s\n", line);
    }
}
