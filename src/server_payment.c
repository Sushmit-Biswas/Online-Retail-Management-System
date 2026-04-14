#include "common.h"
#include "server_payment.h"

#include <sys/types.h>
#include <sys/wait.h>

int run_payment_process(const char *method, float amount, char *payment_status, size_t payment_status_size) {
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
