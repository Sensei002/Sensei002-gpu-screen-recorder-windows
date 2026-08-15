#ifndef GSR_KMS_CLIENT_H
#define GSR_KMS_CLIENT_H

#include "../kms_shared.h"
#include <sys/types.h>
#include <limits.h>

typedef struct gsr_kms_client gsr_kms_client;

struct gsr_kms_client {
    pid_t kms_server_pid;
    int initial_socket_fd;
    int initial_client_fd;
    char initial_socket_path[PATH_MAX];
    int socket_pair[2];
};

/* |card_path| should be a path to card, for example /dev/dri/card0 */
int gsr_kms_client_init(gsr_kms_client *self, const char *card_path);
void gsr_kms_client_deinit(gsr_kms_client *self);

int gsr_kms_client_get_kms(gsr_kms_client *self, gsr_kms_response *response);

#endif /* #define GSR_KMS_CLIENT_H */
