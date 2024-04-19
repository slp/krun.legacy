/*
 * This is an example implementing chroot-like functionality with libkrun.
 *
 * It executes the requested command (relative to NEWROOT) inside a fresh
 * Virtual Machine created and managed by libkrun.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <libkrun.h>
#include <limits.h>
#include <getopt.h>
#include <stdbool.h>
#include <assert.h>

enum net_mode {
    NET_MODE_PASST = 0,
    NET_MODE_TSI,
};

static void print_help(char *const name)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] COMMAND [COMMAND_ARGS...]\n"
        "OPTIONS: \n"
        "        -h    --help                Show help\n"
        "              --net=NET_MODE        Set network mode\n"
        "              --passt-socket=PATH   Instead of starting passt, connect to passt socket at PATH"
        "NET_MODE can be either TSI (default) or PASST\n"
        "\n"
        "COMMAND:      the command you want to execute in the vm\n"
        "COMMAND_ARGS: arguments of COMMAND\n",
        name
    );
}

static const struct option long_options[] = {
    { "help", no_argument, NULL, 'h' },
    { "net_mode", required_argument, NULL, 'N' },
    { "passt-socket", required_argument, NULL, 'P' },
    { NULL, 0, NULL, 0 }
};

struct cmdline {
    bool show_help;
    enum net_mode net_mode;
    char const *passt_socket_path;
    char *const *guest_argv;
};

bool parse_cmdline(int argc, char *const argv[], struct cmdline *cmdline)
{
    assert(cmdline != NULL);

    // set the defaults
    *cmdline = (struct cmdline){
        .show_help = false,
        .net_mode = NET_MODE_PASST,
        .passt_socket_path = NULL,
        .guest_argv = NULL,
    };

    int option_index = 0;
    int c;
    // the '+' in optstring is a GNU extension that disables permutating argv
    while ((c = getopt_long(argc, argv, "+h", long_options, &option_index)) != -1) {
        switch (c) {
        case 'h':
            cmdline->show_help = true;
            return true;
        case 'N':
            if (strcasecmp("TSI", optarg) == 0) {
                cmdline->net_mode = NET_MODE_TSI;
            } else if(strcasecmp("PASST", optarg) == 0) {
                cmdline->net_mode = NET_MODE_PASST;
            } else {
                fprintf(stderr, "Unknown mode %s\n", optarg);
                return false;
            }
            break;
        case 'P':
            cmdline->passt_socket_path = optarg;
            break;
        case '?':
            return false;
        default:
            fprintf(stderr, "internal argument parsing error (returned character code 0x%x)\n", c);
            return false;
        }
    }

    if (optind <= argc - 1) {
        cmdline->guest_argv = &argv[optind + 1];
        return true;
    }

    if (optind == argc) {
        fprintf(stderr, "Missing COMMAND argument\n");
    }

    return false;
}

int connect_to_passt()
{
    struct sockaddr_un addr;
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Failed to create passt socket fd");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/passt_1.socket", sizeof(addr.sun_path) - 1);

    if (connect(socket_fd, (const struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("Failed to bind passt socket");
        return -1;
    }

    return socket_fd;
}

int start_passt()
{
    int socket_fds[2];
    const int PARENT = 0;
    const int CHILD = 1;
    
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds) < 0) {
        perror("Failed to create passt socket fd");
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    
    if (pid == 0) { // child
        if (close(socket_fds[PARENT]) < 0) {
            perror("close PARENT");
        }

        char fd_as_str[16]; 
        snprintf(fd_as_str, sizeof(fd_as_str), "%d", socket_fds[CHILD]);

        printf("passing fd %s to passt", fd_as_str);

        if (execlp("passt", "passt", "-q", "-f", "--fd", fd_as_str, NULL) < 0) {
            perror("execlp");
            return -1;
        }

    } else { // parent
        if (close(socket_fds[CHILD]) < 0) {
            perror("close CHILD");
        }

        return socket_fds[PARENT];
    }
}


int main(int argc, char *const argv[])
{
    int ctx_id;
    int err;
    int i;
    char path[PATH_MAX];
    char *username;
    char *const empty_envp[] = { 0 };
    struct cmdline cmdline;
    struct rlimit rlim;

    if (getuid() == 0 || geteuid() == 0) {
        printf("Running as root is not supported as it may break your system\n");
        return -1;
    }

    if (!parse_cmdline(argc, argv, &cmdline)) {
        putchar('\n');
        print_help(argv[0]);
        return -1;
    }

    if (cmdline.show_help){
        print_help(argv[0]);
        return 0;
    }

    // Set the log level to "off".
    err = krun_set_log_level(0);
    if (err) {
        errno = -err;
        perror("Error configuring log level");
        return -1;
    }

    // Create the configuration context.
    ctx_id = krun_create_ctx();
    if (ctx_id < 0) {
        errno = -ctx_id;
        perror("Error creating configuration context");
        return -1;
    }

    // Configure the number of vCPUs (4) and the amount of RAM (4096 MiB).
    if (err = krun_set_vm_config(ctx_id, 4, 4096)) {
        errno = -err;
        perror("Error configuring the number of vCPUs and/or the amount of RAM");
        return -1;
    }

    // Raise RLIMIT_NOFILE to the maximum allowed to create some room for virtio-fs
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rlim);

    if (err = krun_set_root(ctx_id, "/")) {
        errno = -err;
        perror("Error configuring root path");
        return -1;
    }

    uint32_t virgl_flags = VIRGLRENDERER_USE_EGL | VIRGLRENDERER_DRM |
        VIRGLRENDERER_THREAD_SYNC | VIRGLRENDERER_USE_ASYNC_FENCE_CB;
    if (err = krun_set_gpu_options(ctx_id, virgl_flags)) {
        errno = -err;
        perror("Error configuring gpu");
        return -1;
    }

    // Configure PASST networking
    if (cmdline.net_mode == NET_MODE_PASST) {
        int passt_fd = cmdline.passt_socket_path ? connect_to_passt(cmdline.passt_socket_path) : start_passt();

        if (passt_fd < 0) {
            return -1;
        }

        if (err = krun_set_passt_fd(ctx_id, passt_fd)) {
            errno = -err;
            perror("Error configuring net mode");
            return -1;
        }
    }

    username = getenv("USER");
    if (username == NULL) {
        perror("Error getting username from environment");
        return -1;
    }

    snprintf(&path[0], PATH_MAX, "/home/%s", username);

    // Set the working directory to "/", just for the sake of completeness.
    if (err = krun_set_workdir(ctx_id, &path[0])) {
        errno = -err;
        perror("Error configuring \"/\" as working directory");
        return -1;
    }

    if (readlink("/proc/self/exe", &path[0], PATH_MAX) < 0) {
        perror("Error reading /proc/self/exe");
        return -1;
    }

    char **sargv = calloc(argc + 5, sizeof(char *));
    char str_uid[6];
    char str_gid[6];

    strcat(&path[0], "-guest");

    snprintf(&str_uid[0], 6, "%d", getuid());
    snprintf(&str_gid[0], 6, "%d", getgid());
    sargv[0] = &path[0];
    sargv[1] = username;
    sargv[2] = &str_uid[0];
    sargv[3] = &str_gid[0];

    for (int i = 1; i < argc; i++) {
        sargv[i + 3] = argv[i];
    }

    char *const *env = &empty_envp[0];

    char *ldenv = getenv("LD_LIBRARY_PATH");
    char *glenv = getenv("LIBGL_DRIVERS_PATH");
    if (ldenv != NULL && glenv != NULL) {
        char **envp = calloc(2, sizeof(char *));
        char ldvar[PATH_MAX];
        char glvar[PATH_MAX];

        snprintf(&ldvar[0], PATH_MAX, "LD_LIBRARY_PATH=%s", ldenv);
        snprintf(&glvar[0], PATH_MAX, "LIBGL_DRIVERS_PATH=%s", glenv);

        envp[0] = &ldvar[0];
        envp[1] = &glvar[0];

        env = envp;
    }

    // Specify the path of the binary to be executed in the isolated context, relative to the root path.
    if (err = krun_set_exec(ctx_id, sargv[0], &sargv[1], env)) {
        errno = -err;
        perror("Error configuring the parameters for the executable to be run");
        return -1;
    }

    // Start and enter the microVM. Unless there is some error while creating the microVM
    // this function never returns.
    if (err = krun_start_enter(ctx_id)) {
        errno = -err;
        perror("Error creating the microVM");
        return -1;
    }

    // Not reached.
    return 0;
}
