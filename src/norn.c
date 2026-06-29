/* norn — Mainline DHT client CLI */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sodium.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "config.h"
#include "libnorn/norn.h"
#include "libnorn/crypto.h"
#include "libnorn/log.h"
#include "nornd/cli_cluster.h"
#include "nornd/cli_keys.h"
#include "nornd/cli_peer.h"
#include "nornd/ipc.h"

#define DEFAULT_PORT 6881
#define DEFAULT_TIMEOUT 5000
#define MAX_VALUE_SIZE 1000

static const char *prog_name = "norn";

static char *key_file = NULL;
static int port = DEFAULT_PORT;
static int timeout_ms = DEFAULT_TIMEOUT;
static int log_level = LOG_INFO;

static void usage(FILE *out) {
    fprintf(out, "Usage: %s [OPTIONS] <command> [ARGS...]\n", prog_name);
    fputc('\n', out);

    fprintf(out, "Options:\n");
    fprintf(out, "  --key <path>       Ed25519 keypair (default: ~/.config/norn/key.pem)\n");
    fprintf(out, "  --port <port>      DHT UDP port (default: 6881)\n");
    fprintf(out, "  --timeout <ms>     Query timeout (default: 5000)\n");
    fprintf(out, "  --log-level <lvl>  Log level: debug, info, warn, error\n");

    fprintf(out, "  --help             Show this help\n");
    fputc('\n', out);

    fprintf(out, "Commands:\n");
    static const char *cmds[][2] = {
        {"node",       "Manage the local nornd daemon"},
        {"peer",       "Interact with remote peers"},
        {"bep44",      "DHT signed records"},
        {"cluster",    "Cluster KV store"},
        {"version",    "Print version"},
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        fprintf(out, "  %-12s %s\n", cmds[i][0], cmds[i][1]);
    }
    fputc('\n', out);
}

static char *get_default_key_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "ERROR: HOME environment variable not set\n");
        return NULL;
    }
    
    char *path = malloc(strlen(home) + strlen("/.config/norn/key.pem") + 1);
    if (!path) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return NULL;
    }
    
    sprintf(path, "%s/.config/norn/key.pem", home);
    return path;
}

static int create_key_dir(const char *key_path) {
    char *dir = strdup(key_path);
    if (!dir) return -1;
    
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "ERROR: Failed to create directory %s: %s\n", dir, strerror(errno));
            free(dir);
            return -1;
        }
    }
    
    free(dir);
    return 0;
}

static int load_keypair(unsigned char pubkey[32], unsigned char secret[64]) {
    /* Only a path from get_default_key_path() is heap-owned here; `key_file`
     * and a NORN_KEY value from getenv() are borrowed and must not be freed. */
    const char *path = key_file;
    int owned = 0;
    if (!path) {
        path = getenv("NORN_KEY");
    }
    if (!path) {
        path = get_default_key_path();
        if (!path) return -1;
        owned = 1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open key file %s: %s\n", path, strerror(errno));
        fprintf(stderr, "HINT: Run 'norn keygen' to create a keypair\n");
        if (owned) free((char*)path);
        return -1;
    }

    keypair_t kp;
    memset(&kp, 0, sizeof(kp));

    if (fread(&kp, sizeof(kp), 1, f) != 1) {
        fprintf(stderr, "ERROR: Failed to read key file %s\n", path);
        fclose(f);
        if (owned) free((char*)path);
        return -1;
    }

    fclose(f);

    memcpy(pubkey, kp.public_key, 32);
    memcpy(secret, kp.secret_key, 64);

    if (owned) free((char*)path);
    return 0;
}

static int do_version(void) {
    printf("norn %s\n", NORN_VERSION);
    return 0;
}

static int do_keygen(int argc, char **argv) {
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    char *key_path = NULL;
    int owned = 0; /* 1 only when key_path is heap-owned (get_default_key_path) */

    optind = 2;
    
    while ((opt = getopt_long(argc, argv, "+k:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_path = optarg;
                break;
            case 'h':
                fprintf(stdout, "Usage: %s keygen [--key <path>]\n", prog_name);
                fprintf(stdout, "\nGenerate an Ed25519 keypair and save to a file.\n");
                return 0;
            default:
                return 1;
        }
    }
    
    if (!key_path) {
        /* Honor a global `--key` given before the subcommand (the top-level
         * getopt consumed it into key_file). */
        key_path = key_file;
    }
    if (!key_path) {
        key_path = getenv("NORN_KEY");
    }
    if (!key_path) {
        key_path = get_default_key_path();
        if (!key_path) return 1;
        owned = 1;
    }

    if (access(key_path, F_OK) == 0) {
        fprintf(stderr, "ERROR: Key file already exists: %s\n", key_path);
        fprintf(stderr, "HINT: Remove the file or use --key to specify a different path\n");
        if (owned) free(key_path);
        return 1;
    }
    
    if (create_key_dir(key_path) != 0) {
        if (owned) free(key_path);
        return 1;
    }
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    FILE *f = fopen(key_path, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to create key file %s: %s\n", key_path, strerror(errno));
        if (owned) free(key_path);
        return 1;
    }
    
    if (fwrite(&kp, sizeof(kp), 1, f) != 1) {
        fprintf(stderr, "ERROR: Failed to write key file %s\n", key_path);
        fclose(f);
        if (owned) free(key_path);
        return 1;
    }
    
    fclose(f);
    chmod(key_path, 0600);
    
    for (int i = 0; i < 32; i++) {
        printf("%02x", kp.public_key[i]);
    }
    printf("\n");
    
    if (owned) free(key_path);
    return 0;
}

/* IPC helpers: socket path, connect, round-trip one request. */
static const char *nornd_socket_path(char *buf, size_t cap) {
    const char *env = getenv("NORN_SOCK");
    if (env && env[0]) return env;
    const char *run = getenv("XDG_RUNTIME_DIR");
    if (run && run[0]) {
        snprintf(buf, cap, "%s/nornd.sock", run);
        return buf;
    }
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, cap, "%s/.config/norn/nornd.sock", home);
    return buf;
}

static int ipc_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sa.sun_path)) { close(fd); return -1; }
    strcpy(sa.sun_path, path);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
    return fd;
}

static int ipc_round_trip(const char *op, unsigned char *val, size_t *vlen,
                          size_t cap) {
    char pbuf[512];
    const char *path = nornd_socket_path(pbuf, sizeof(pbuf));
    int fd = ipc_connect(path);
    if (fd < 0) {
        fprintf(stderr, "norn: cannot reach nornd at %s. Is nornd running?\n", path);
        return -1;
    }
    nornd_ipc_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.op, op);
    unsigned char wire[256];
    int wlen = nornd_ipc_encode_req(&req, wire, sizeof(wire));
    if (wlen < 0 || write(fd, wire, (size_t)wlen) != wlen) {
        close(fd);
        return -1;
    }
    unsigned char frame[NORND_IPC_MAX_BODY + 4];
    size_t got = 0;
    while (got < 4) {
        ssize_t n = read(fd, frame + got, 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    int64_t body = nornd_ipc_frame_len(frame, 4);
    if (body < 0 || (size_t)body + 4 > sizeof(frame)) { close(fd); return -1; }
    while (got < (size_t)body + 4) {
        ssize_t n = read(fd, frame + got, (size_t)body + 4 - got);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    nornd_ipc_resp_t resp;
    size_t consumed = 0;
    if (nornd_ipc_decode_resp(frame, got, &resp, &consumed) != 0) return -1;
    if (!resp.ok) return -1;
    size_t n = resp.vlen;
    if (n > cap) n = cap;
    memcpy(val, resp.val, n);
    *vlen = n;
    return 0;
}

static int do_node_secret(int argc, char **argv) {
    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 3;
    while ((opt = getopt_long(argc, argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: norn node secret\n"
                       "\n"
                       "Print the nornd node's Ed25519 secret key (64 hex bytes).\n"
                       "Requires a running nornd daemon.\n");
                return 0;
            default:
                return 1;
        }
    }
    unsigned char val[64];
    size_t vlen = 0;
    if (ipc_round_trip("node-secret", val, &vlen, sizeof(val)) != 0) return 1;
    if (vlen != 64) {
        fprintf(stderr, "norn node secret: unexpected response length\n");
        return 1;
    }
    for (size_t i = 0; i < 64; i++) printf("%02x", val[i]);
    printf("\n");
    return 0;
}

static int do_node_public(int argc, char **argv) {
    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 3;
    while ((opt = getopt_long(argc, argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: norn node public\n"
                       "\n"
                       "Print the nornd node's Ed25519 public key (32 hex bytes).\n"
                       "Requires a running nornd daemon.\n");
                return 0;
            default:
                return 1;
        }
    }
    unsigned char val[32];
    size_t vlen = 0;
    if (ipc_round_trip("node-public", val, &vlen, sizeof(val)) != 0) return 1;
    if (vlen != 32) {
        fprintf(stderr, "norn node public: unexpected response length\n");
        return 1;
    }
    for (size_t i = 0; i < 32; i++) printf("%02x", val[i]);
    printf("\n");
    return 0;
}

static int do_node_start(int argc, char **argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "norn node start: not implemented (use systemd: systemctl --user start nornd)\n");
    return 1;
}

static int do_node_restart(int argc, char **argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "norn node restart: not implemented (use systemd: systemctl --user restart nornd)\n");
    return 1;
}

static int do_node_status(int argc, char **argv) {
    (void)argc; (void)argv;
    unsigned char buf[4096];
    size_t vlen = 0;
    if (ipc_round_trip("node-stats", buf, &vlen, sizeof(buf)) != 0) {
        fprintf(stderr, "nornd is not running\n");
        return 1;
    }
    fwrite(buf, 1, vlen, stdout);
    return 0;
}

static int do_node_log(int argc, char **argv) {
    /* Default: 50 lines from the user nornd. Use --system for the system node. */
    int system_node = 0;
    int follow = 0;
    int lines = 50;
    /* Parse optional flags. */
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--system") == 0)
            system_node = 1;
        else if (strcmp(argv[i], "--follow") == 0 || strcmp(argv[i], "-f") == 0)
            follow = 1;
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            lines = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: norn node log [OPTIONS]\n"
                   "\n"
                   "Show nornd daemon logs.\n"
                   "\n"
                   "Options:\n"
                   "  --system          System nornd (default: user)\n"
                   "  -n <lines>        Number of lines (default: 50)\n"
                   "  -f, --follow      Follow new log entries\n"
                   "  --help            Show this help\n");
            return 0;
        } else {
            fprintf(stderr, "norn node log: unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    char lines_buf[16];
    snprintf(lines_buf, sizeof(lines_buf), "%d", lines);
    const char *journalctl_args[16];
    int na = 0;
    journalctl_args[na++] = "journalctl";
    if (system_node)
        journalctl_args[na++] = "-u";
    else {
        journalctl_args[na++] = "--user";
        journalctl_args[na++] = "-u";
    }
    journalctl_args[na++] = "nornd";
    journalctl_args[na++] = "-n";
    journalctl_args[na++] = lines_buf;
    if (follow)
        journalctl_args[na++] = "--follow";
    journalctl_args[na] = NULL;
    execvp("journalctl", (char *const *)journalctl_args);
    /* If execvp returns, an error occurred. */
    fprintf(stderr, "norn node log: failed to run journalctl: %s\n", strerror(errno));
    return 1;
}

static int do_get(int argc, char **argv) {
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"timeout", required_argument, 0, 't'},
        {"json", no_argument, 0, 'j'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    
    optind = 2;
    
    while ((opt = getopt_long(argc, argv, "+k:t:jh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_file = optarg;
                break;
            case 't':
                timeout_ms = atoi(optarg);
                if (timeout_ms <= 0) {
                    fprintf(stderr, "ERROR: Invalid timeout: %s\n", optarg);
                    return 1;
                }
                break;
            case 'j':
                /* JSON output - would format output as JSON */
                break;
            case 'h':
                fprintf(stdout, "Usage: %s get [OPTIONS] <key>\n", prog_name);
                fprintf(stdout, "\n");
                fprintf(stdout, "Retrieve record from DHT\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Arguments:\n");
                fprintf(stdout, "  <key>           64-character hex public key\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Options:\n");
                fprintf(stdout, "  --key <path>      Key file path\n");
                fprintf(stdout, "  --timeout <ms>    Query timeout (default: %d)\n", DEFAULT_TIMEOUT);
                fprintf(stdout, "  --json            Output as JSON\n");
                fprintf(stdout, "  --help            Show this help\n");
                return 0;
            default:
                return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "ERROR: Missing key argument\n");
        fprintf(stderr, "Usage: %s get <key>\n", prog_name);
        return 1;
    }
    
    const char *key_str = argv[optind];
    
    if (strlen(key_str) != 64) {
        fprintf(stderr, "ERROR: Key must be 64 hex characters (got %zu)\n", strlen(key_str));
        return 1;
    }
    
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        if (sscanf(key_str + i*2, "%2x", &byte) != 1) {
            fprintf(stderr, "ERROR: Invalid hex in key at position %d\n", i*2);
            return 1;
        }
    }
    
    unsigned char pubkey[32], secret[64];
    if (load_keypair(pubkey, secret) != 0) {
        return 1;
    }
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = NORN_VERSION;
    
    norn_client_t *client = norn_new(pubkey, secret, &cfg);
    if (!client) {
        fprintf(stderr, "ERROR: Failed to create DHT client\n");
        return 1;
    }
    
    printf("Key: %s\n", key_str);
    printf("(DHT get not fully implemented - requires running DHT network)\n");
    
    norn_free(client);
    return 0;
}

static int do_set(int argc, char **argv) {
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"seq", required_argument, 0, 's'},
        {"salt", required_argument, 0, 'S'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    uint32_t seq = 1;
    const char *salt = NULL;
    
    optind = 2;
    
    while ((opt = getopt_long(argc, argv, "+k:s:S:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_file = optarg;
                break;
            case 's':
                seq = (uint32_t)atoi(optarg);
                break;
            case 'S':
                salt = optarg;
                break;
            case 'h':
                fprintf(stdout, "Usage: %s set [OPTIONS] <key> <value>\n", prog_name);
                fprintf(stdout, "\n");
                fprintf(stdout, "Store signed record to DHT\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Arguments:\n");
                fprintf(stdout, "  <key>           Key identifier\n");
                fprintf(stdout, "  <value>         Value to store\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Options:\n");
                fprintf(stdout, "  --key <path>    Key file path\n");
                fprintf(stdout, "  --seq <n>       Sequence number (default: auto)\n");
                fprintf(stdout, "  --salt <string> Salt for salted items\n");
                fprintf(stdout, "  --help          Show this help\n");
                return 0;
            default:
                return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "ERROR: Missing key argument\n");
        fprintf(stderr, "Usage: %s set <key> <value>\n", prog_name);
        return 1;
    }
    
    const char *key_str = argv[optind];
    optind++;
    
    if (optind >= argc) {
        fprintf(stderr, "ERROR: Missing value argument\n");
        fprintf(stderr, "Usage: %s set <key> <value>\n", prog_name);
        return 1;
    }
    
    const char *value = argv[optind];
    size_t value_len = strlen(value);
    (void)value_len;
    (void)seq;
    (void)salt;
    
    unsigned char pubkey[32], secret[64];
    if (load_keypair(pubkey, secret) != 0) {
        return 1;
    }
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = NORN_VERSION;
    
    norn_client_t *client = norn_new(pubkey, secret, &cfg);
    if (!client) {
        fprintf(stderr, "ERROR: Failed to create DHT client\n");
        return 1;
    }
    
    printf("Key: %s\n", key_str);
    printf("Value: %s\n", value);
    printf("Seq: %u\n", seq);
    if (salt) printf("Salt: %s\n", salt);
    printf("(DHT put not fully implemented - requires running DHT network)\n");
    
    norn_free(client);
    return 0;
}

/* `norn bep44 <get|set> …` — namespaced direct-DHT BEP-44 verbs. Reuses the
 * get/set handlers, which expect the verb at argv[1]; present them a shifted
 * view that drops the "bep44" token. `argv[sub_idx]` is the sub-verb. */
static void bep44_help(FILE *out, const char *prog) {
    fprintf(out, "Usage: %s bep44 <get|set> [ARGS...]\n", prog);
    fprintf(out, "\nSubcommands:\n"
            "  get <key>          Retrieve a signed record from the DHT\n"
            "  set <key> <value>  Store a signed record on the DHT\n");
}

static int do_bep44(int argc, char **argv, int sub_idx) {
    if (sub_idx >= argc) {
        bep44_help(stderr, prog_name);
        return 1;
    }
    const char *sub = argv[sub_idx];
    if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
        bep44_help(stdout, prog_name);
        return 0;
    }
    int n = argc - sub_idx + 1; /* prog + (verb..end) */
    char **view = malloc(sizeof(char *) * (size_t)(n + 1));
    if (!view) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return 1;
    }
    view[0] = argv[0];
    for (int i = sub_idx; i < argc; i++) view[1 + (i - sub_idx)] = argv[i];
    view[n] = NULL;

    int rc;
    if (strcmp(sub, "get") == 0) {
        rc = do_get(n, view);
    } else if (strcmp(sub, "set") == 0) {
        rc = do_set(n, view);
    } else {
        fprintf(stderr, "ERROR: Unknown bep44 subcommand: %s\n", sub);
        fprintf(stderr, "Usage: %s bep44 <get|set> ...\n", prog_name);
        rc = 1;
    }
    free(view);
    return rc;
}

static int do_daemon(int argc, char **argv) {
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"port", required_argument, 0, 'p'},
        {"read-only", no_argument, 0, 'r'},
        {"private", no_argument, 0, 'P'},
        {"bootstrap", required_argument, 0, 'b'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int daemon_port = port;
    int daemon_read_only = 0;
    int private_mode = 0;
    
    optind = 2;
    
    while ((opt = getopt_long(argc, argv, "+k:p:rPb:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_file = optarg;
                break;
            case 'p':
                daemon_port = atoi(optarg);
                if (daemon_port <= 0 || daemon_port > 65535) {
                    fprintf(stderr, "ERROR: Invalid port: %s\n", optarg);
                    return 1;
                }
                break;
            case 'r':
                daemon_read_only = 1;
                break;
            case 'P':
                private_mode = 1;
                break;
            case 'b':
                /* Bootstrap peer - would be parsed and added */
                break;
            case 'h':
                fprintf(stdout, "Usage: %s daemon [OPTIONS]\n", prog_name);
                fprintf(stdout, "\n");
                fprintf(stdout, "Run as DHT daemon\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Options:\n");
                fprintf(stdout, "  --key <path>        Key file path\n");
                fprintf(stdout, "  --port <port>       DHT port (default: %d)\n", DEFAULT_PORT);
                fprintf(stdout, "  --read-only         Read-only mode (BEP-43)\n");
                fprintf(stdout, "  --private           Private overlay mode\n");
                fprintf(stdout, "  --bootstrap <ip:port> Add bootstrap peer\n");
                fprintf(stdout, "  --help              Show this help\n");
                return 0;
            default:
                return 1;
        }
    }
    
    (void)daemon_port;
    (void)daemon_read_only;
    (void)private_mode;
    
    unsigned char pubkey[32], secret[64];
    if (load_keypair(pubkey, secret) != 0) {
        return 1;
    }
    
    norn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = NORN_VERSION;
    cfg.read_only = daemon_read_only;
    cfg.private_mode = private_mode;
    
    norn_client_t *client = norn_new(pubkey, secret, &cfg);
    if (!client) {
        fprintf(stderr, "ERROR: Failed to create DHT client\n");
        return 1;
    }
    
    printf("Starting DHT daemon on port %d...\n", daemon_port);
    if (daemon_read_only) printf("  (read-only mode)\n");
    if (private_mode) printf("  (private mode)\n");
    printf("(Daemon mode not fully implemented - requires event loop integration)\n");
    
    norn_free(client);
    return 0;
}

/* `norn peer <list|connect|get|cat> …` */
static int do_peer(int argc, char **argv, int optind) {
    if (optind >= argc)
        goto peer_help;
    const char *sub = argv[optind];
    int sub_argc = argc - optind;
    char **sub_argv = argv + optind;

    if (strcmp(sub, "list") == 0) {
        unsigned char buf[4096];
        size_t vlen = 0;
        if (ipc_round_trip("peers", buf, &vlen, sizeof(buf)) != 0) {
            fprintf(stderr, "nornd is not running\n");
            return 1;
        }
        fwrite(buf, 1, vlen, stdout);
        return 0;
    } else if (strcmp(sub, "connect") == 0) {
        if (sub_argc < 2) {
            fprintf(stderr, "Usage: %s peer connect <pubkey[@host:port]>\n", prog_name);
            return 1;
        }
        /* Delegate to nornd_cli_peer with a dummy "get" to exercise the dial. */
        char *pv[] = {sub_argv[1], (char *)"get", (char *)""};
        return nornd_cli_peer(3, pv);
    } else if (strcmp(sub, "get") == 0 || strcmp(sub, "cat") == 0) {
        if (sub_argc < 3) {
            fprintf(stderr, "Usage: %s peer %s <pubkey[@host:port]> <arg>\n",
                    prog_name, sub);
            return 1;
        }
        char *pv[] = {sub_argv[1], (char *)sub, sub_argv[2]};
        return nornd_cli_peer(3, pv);
    } else if (strcmp(sub, "keys") == 0) {
        return nornd_cli_keys(sub_argc - 1, sub_argv + 1);
    } else if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
        goto peer_help;
    }
    fprintf(stderr, "ERROR: Unknown peer subcommand: %s\n", sub);
    return 1;

peer_help:
    fprintf(stdout, "Usage: %s peer <subcommand> [ARGS...]\n", prog_name);
    fprintf(stdout, "\nSubcommands:\n");
    fprintf(stdout, "  list                   List known peers\n");
    fprintf(stdout, "  connect <pubkey[@h]>   Dial a remote peer\n");
    fprintf(stdout, "  get <pubkey[@h]> <k>   Fetch a served-KV value\n");
    fprintf(stdout, "  cat <pubkey[@h]> <h>   Fetch a served-KV blob\n");
    fprintf(stdout, "  keys <nodeid>          Resolve peer's SSH + GPG keys\n");
    fprintf(stdout, "\nSee `%s --help` for top-level options.\n", prog_name);
    return optind < argc ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    
    /* Check for --help or -h before command */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }
    
    /* Parse global options first */
    static struct option long_options[] = {
        {"key", required_argument, 0, 'k'},
        {"port", required_argument, 0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"log-level", required_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    
    while ((opt = getopt_long(argc, argv, "+k:p:t:l:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_file = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "ERROR: Invalid port: %s\n", optarg);
                    return 1;
                }
                break;
            case 't':
                timeout_ms = atoi(optarg);
                if (timeout_ms <= 0) {
                    fprintf(stderr, "ERROR: Invalid timeout: %s\n", optarg);
                    return 1;
                }
                break;
            case 'l':
                if (strcmp(optarg, "debug") == 0) {
                    log_level = LOG_DEBUG;
                } else if (strcmp(optarg, "info") == 0) {
                    log_level = LOG_INFO;
                } else if (strcmp(optarg, "warn") == 0) {
                    log_level = LOG_WARN;
                } else if (strcmp(optarg, "error") == 0) {
                    log_level = LOG_ERROR;
                } else {
                    fprintf(stderr, "ERROR: Invalid log level: %s\n", optarg);
                    fprintf(stderr, "Valid levels: debug, info, warn, error\n");
                    return 1;
                }
                break;
            case 'h':
                usage(stdout);
                return 0;
            case '?':
            default:
                usage(stderr);
                return 1;
        }
    }
    
    if (optind >= argc) {
        usage(stderr);
        return 1;
    }
    
    if (sodium_init() < 0) {
        fprintf(stderr, "ERROR: Failed to initialize libsodium\n");
        return 1;
    }
    
    const char *cmd = argv[optind];
    
    if (strcmp(cmd, "version") == 0) {
        return do_version();
    } else if (strcmp(cmd, "keygen") == 0) {
        return do_keygen(argc, argv);
    } else if (strcmp(cmd, "bep44") == 0) {
        /* Namespaced direct-DHT verbs: norn bep44 <get|set> ... */
        return do_bep44(argc, argv, optind + 1);
    } else if (strcmp(cmd, "get") == 0) {
        return do_get(argc, argv); /* deprecated alias for `bep44 get` */
    } else if (strcmp(cmd, "set") == 0) {
        return do_set(argc, argv); /* deprecated alias for `bep44 set` */
    } else if (strcmp(cmd, "daemon") == 0) {
        return do_daemon(argc, argv);
    } else if (strcmp(cmd, "cluster") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stdout, "Usage: %s cluster <subcommand> [ARGS...]\n", prog_name);
            fprintf(stdout, "\nSubcommands:\n"
                    "  put <key> <val>    Store a key-value pair\n"
                    "  get <key>          Retrieve a value\n"
                    "  del <key>          Delete a key\n"
                    "  cas <k> <o> <n>    Compare-and-swap\n"
                    "  watch <prefix>     Stream changes\n"
                    "  members            List cluster members\n"
                    "  leader             Show current leader\n"
                    "  status             Cluster health\n");
            return optind + 1 < argc ? 0 : 1;
        }
        return nornd_cli_cluster(argc - optind - 1, argv + optind + 1);
    } else if (strcmp(cmd, "keys") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stdout, "Usage: %s keys <nodeid-hex>\n", prog_name);
            fprintf(stdout, "\nResolve a peer's SSH + GPG public keys.\n");
            return 1;
        }
        return nornd_cli_keys(argc - optind - 1, argv + optind + 1);
    } else if (strcmp(cmd, "node") == 0) {
        if (optind + 1 < argc) {
            const char *sub = argv[optind + 1];
            if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0 || strcmp(sub, "help") == 0) {
                goto node_help;
            }
            if (strcmp(sub, "start") == 0)
                return do_node_start(argc, argv);
            if (strcmp(sub, "restart") == 0)
                return do_node_restart(argc, argv);
            if (strcmp(sub, "status") == 0)
                return do_node_status(argc, argv);
            if (strcmp(sub, "log") == 0)
                return do_node_log(argc, argv);
            if (strcmp(sub, "public") == 0)
                return do_node_public(argc, argv);
            if (strcmp(sub, "secret") == 0)
                return do_node_secret(argc, argv);
            if (strcmp(sub, "keygen") == 0)
                return do_keygen(argc, argv);
        }
node_help:
        fprintf(stdout, "Usage: %s node <subcommand> [ARGS...]\n", prog_name);
        fprintf(stdout, "\nSubcommands:\n"
                "  start          Start the nornd daemon\n"
                "  restart        Restart the nornd daemon\n"
                "  status         Node status (recfile)\n"
                "  log            View daemon logs\n"
                "  public         Print node's Ed25519 public key\n"
                "  secret         Print node's Ed25519 secret key\n"
                "  keygen         Generate an Ed25519 keypair\n");
        fprintf(stdout, "\nSee `%s --help` for top-level options.\n", prog_name);
        return optind + 1 < argc ? 0 : 1;
    } else if (strcmp(cmd, "peer") == 0) {
        return do_peer(argc, argv, optind + 1);
    } else if (strcmp(cmd, "authorized-keys") == 0) {
        /* Enumerate every fleet member's published SSH key as an
         * authorized_keys file. Reuses the cluster IPC client (op "authkeys"). */
        char *av[] = {(char *)"authkeys"};
        return nornd_cli_cluster(1, av);
    }

    fprintf(stderr, "ERROR: Unknown command: %s\n", cmd);
    usage(stderr);
    return 1;
}