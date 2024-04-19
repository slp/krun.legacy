#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <termios.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/vm_sockets.h>

char path[PATH_MAX];

static int mount_filesystems()
{
    int fd;
    int ret;

    if (mount("tmpfs", "/var/run", "tmpfs",
              MS_NOEXEC | MS_NOSUID | MS_RELATIME, NULL) < 0) {
        perror("mount(/var/run)");
        return -1;
    }

    fd = open("/tmp/resolv.conf", O_CREAT);
    if (fd < 0) {
        perror("creating /tmp/resolv.conf");
        return -1;
    }
    close(fd);

    fd = open_tree(AT_FDCWD, "/tmp/resolv.conf",
                   OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
    if (fd < 0) {
        perror("open_tree /tmp/resolv.conf");
        return -1;
    }

    if (move_mount(fd, "", AT_FDCWD, "/etc/resolv.conf",
                   MOVE_MOUNT_F_EMPTY_PATH) != 0) {
        perror("move_mount /etc/resolv.conf");
        close(fd);
        return -1;
    }
    close(fd);

    if (mount("binfmt_misc", "/proc/sys/fs/binfmt_misc", "binfmt_misc",
              MS_NOEXEC | MS_NOSUID | MS_RELATIME, NULL) < 0) {
        perror("mount(binfmt_misc)");
        return -1;
    }

    return 0;
}

static void setup_fex()
{
    int ret;
    int fd;
    unsigned char fex_x86_magic[] = ":FEX-x86:M:0:\\x7fELF\\x01\\x01\\x01\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x02\\x00\\x03\\x00:\\xff\\xff\\xff\\xff\\xff\\xfe\\xfe\\x00\\x00\\x00\\x00\\xff\\xff\\xff\\xff\\xff\\xfe\\xff\\xff\\xff:/usr/bin/FEXInterpreter:POCF";
    unsigned char fex_x86_64_magic[] = ":FEX-x86_64:M:0:\\x7f""ELF\\x02\\x01\\x01\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x02\\x00\\x3e\\x00:\\xff\\xff\\xff\\xff\\xff\\xfe\\xfe\\x00\\x00\\x00\\x00\\xff\\xff\\xff\\xff\\xff\\xfe\\xff\\xff\\xff:/usr/bin/FEXInterpreter:POCF";

    ret = access("/usr/bin/FEXInterpreter", F_OK);
    if (ret != 0) {
        return;
    }

    fd = open("/proc/sys/fs/binfmt_misc/register", O_WRONLY);
    if (fd < 0) {
        perror("opening binfmt_misc/register");
        return;
    }

    ret = write(fd, &fex_x86_magic[0], strlen(&fex_x86_magic[0]));
    if (ret < 0) {
        perror("registering fex x86 magic");
        return;
    }

    ret = write(fd, &fex_x86_64_magic[0], strlen(&fex_x86_64_magic[0]));
    if (ret < 0) {
        perror("registering fex x86_64 magic");
        return;
    }
}

void configure_network()
{
    int fd;
    int ret;
    pid_t pid;
    char *nl;
    char hostname[254];

    pid = fork();
    if (pid == 0) {
        close(0);
        close(1);
        close(2);
        execvp("/sbin/dhclient", NULL);
    } else {
        fd = open("/etc/hostname", O_RDONLY);
        if (fd < 0) {
            perror("opening /etc/hostname");
            return;
        }

        memset(&hostname[0], 0, 254);
        ret = read(fd, &hostname[0], 254);
        if (ret <= 0) {
            perror("reading /etc/hostname");
            close(fd);
            return;
        }

        nl = strstr(&hostname[0], "\n");
        if (nl) {
            *nl = '\0';
        }

        ret = sethostname(&hostname[0], strlen(&hostname[0]));
        if (ret < 0) {
            perror("setting hostname");
            close(fd);
            return;
        }

        close(fd);
    }

    waitpid(pid, NULL, 0);
}

int setup_directories(uid_t uid, gid_t gid)
{
    DIR *d;
    struct dirent *entry;
    char *directories[] = {"/dev/dri", "/dev/snd"};


    for (int i = 0; i < 2; i++) {
        d = opendir(directories[i]);
        if (d == NULL) {
            perror("opening directory");
            return -1;
        }

        while (1) {
            entry = readdir(d);
            if (entry == NULL) {
                break;
            }

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            snprintf(&path[0], PATH_MAX, "%s/%s", directories[i], entry->d_name);
            chown(&path[0], uid, gid);
        }
    }
}

int setup_user(char *username, uid_t uid, gid_t gid)
{
    int ret;
    struct rlimit rlim;

    setup_directories(uid, gid);

    ret = setgid(gid);
    if (ret != 0) {
        perror("setgid");
        return -1;
    }

    ret = setuid(uid);
    if (ret != 0) {
        perror("setuid");
        return -1;
    }

    snprintf(&path[0], PATH_MAX, "/tmp/%d", uid);
    mkdir(&path[0], 0755);
    setenv("XDG_RUNTIME_DIR", &path[0], 1);

    snprintf(&path[0], PATH_MAX, "/home/%s", username);
    setenv("HOME", &path[0], 1);

    // TODO - only do this if running on Asahi Linux.
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "asahi", 1);

    return 0;
}

void exec_sommelier(int argc, char **argv)
{
    pid_t pid;
    int ret;
    int num_args = 4;
    char *glenv;
    char glarg[PATH_MAX];

    ret = access("/usr/bin/sommelier", F_OK);
    if (ret != 0) {
        return;
    }

    glenv = getenv("LIBGL_DRIVERS_PATH");
    if (glenv != NULL) {
        num_args++;
    }

    char **sargv = calloc(argc + num_args, sizeof(char *));
    sargv[0] = "/usr/bin/sommelier";
    sargv[1] = "--virtgpu-channel";
    sargv[2] = "-X";
    sargv[3] = "--glamor";

    if (glenv != NULL) {
        snprintf(&glarg[0], PATH_MAX, "--xwayland-gl-driver-path=%s", glenv);
        sargv[num_args - 1] = &glarg[0];
    }

    for (int i = 0; i <= argc; i++) {
        printf("arg: %s", argv[i]);
        sargv[i + num_args] = argv[i];
    }

    execvp(sargv[0], sargv);
}

int main(int argc, char **argv)
{
    char **exec_argv;
    uid_t uid;
    gid_t gid;
    char *username;

    if (argc < 5) {
        printf("Invalid arguments, bailing out\n");
        exit(-1);
    }

    username = argv[1];

    uid = strtol(argv[2], NULL, 10);
    if (uid == LONG_MIN || uid == LONG_MAX) {
        perror("strtol");
        return -1;
    }

    gid = strtol(argv[3], NULL, 10);
    if (gid == LONG_MIN || gid == LONG_MAX) {
        perror("strtol");
        return -1;
    }

    if (mount_filesystems() < 0) {
        printf("Couldn't mount filesystems, bailing out\n");
        exit(-2);
    }

    setup_fex();

    configure_network();

    if (setup_user(username, uid, gid) < 0) {
        printf("Couldn't set up user, bailing out\n");
        (exit -3);
    }

    // Will not return if successful.
    exec_sommelier(argc - 4, &argv[4]);

    // Fallback option if sommelier is not present.
    exec_argv = &argv[4];
    if (execvp(exec_argv[0], exec_argv) < 0) {
        printf("Couldn't execute '%s' inside the vm: %s\n", exec_argv[0], strerror(errno));
        exit(-4);
    }

    return 0;
}
