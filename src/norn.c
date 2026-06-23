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
#include "config.h"
#include "libnorn/norn.h"
#include "libnorn/crypto.h"
#include "libnorn/log.h"

#define DEFAULT_PORT 6881
#define DEFAULT_TIMEOUT 5000
#define MAX_VALUE_SIZE 1000

static const char *prog_name = "norn";

static char *key_file = NULL;
static int port = DEFAULT_PORT;
static int timeout_ms = DEFAULT_TIMEOUT;
static int log_level = LOG_INFO;
static int read_only = 0;

static void usage(FILE *out) {
    fprintf(out, "Usage: %s [OPTIONS] <command> [ARGS...]\n", prog_name);
    fprintf(out, "\n");
    fprintf(out, "Mainline DHT client for peer discovery and bootstrap.\n");
    fprintf(out, "\n");
    fprintf(out, "Commands:\n");
    fprintf(out, "  keygen              Generate ed25519 keypair\n");
    fprintf(out, "  get <key>           Retrieve record from DHT\n");
    fprintf(out, "  set <key> <value>   Store signed record to DHT\n");
    fprintf(out, "  daemon              Run as DHT daemon\n");
    fprintf(out, "  version             Print version\n");
    fprintf(out, "\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --key <path>         Key file (default: ~/.norn/key.pem)\n");
    fprintf(out, "  --port <port>        DHT port (default: %d)\n", DEFAULT_PORT);
    fprintf(out, "  --timeout <ms>       Query timeout (default: %d)\n", DEFAULT_TIMEOUT);
    fprintf(out, "  --log-level <level>  Log level: debug, info, warn, error (default: info)\n");
    fprintf(out, "  --read-only          Read-only mode (BEP-43)\n");
    fprintf(out, "  --help               Show this help\n");
    fprintf(out, "\n");
    fprintf(out, "Environment:\n");
    fprintf(out, "  NORN_KEY             Default key file path\n");
    fprintf(out, "  NORN_PORT             Default DHT port\n");
    fprintf(out, "\n");
    fprintf(out, "Exit codes:\n");
    fprintf(out, "  0  Success\n");
    fprintf(out, "  1  Error\n");
    fprintf(out, "  2  Network error\n");
}

static char *get_default_key_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "ERROR: HOME environment variable not set\n");
        return NULL;
    }
    
    char *path = malloc(strlen(home) + strlen("/.norn/key.pem") + 1);
    if (!path) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return NULL;
    }
    
    sprintf(path, "%s/.norn/key.pem", home);
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
    const char *path = key_file;
    if (!path) {
        path = getenv("NORN_KEY");
    }
    if (!path) {
        path = get_default_key_path();
        if (!path) return -1;
    }
    
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open key file %s: %s\n", path, strerror(errno));
        fprintf(stderr, "HINT: Run 'norn keygen' to create a keypair\n");
        if (!key_file) free((char*)path);
        return -1;
    }
    
    keypair_t kp;
    memset(&kp, 0, sizeof(kp));
    
    if (fread(&kp, sizeof(kp), 1, f) != 1) {
        fprintf(stderr, "ERROR: Failed to read key file %s\n", path);
        fclose(f);
        if (!key_file) free((char*)path);
        return -1;
    }
    
    fclose(f);
    
    memcpy(pubkey, kp.public_key, 32);
    memcpy(secret, kp.secret_key, 64);
    
    if (!key_file) free((char*)path);
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
    
    optind = 2;
    
    while ((opt = getopt_long(argc, argv, "+k:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'k':
                key_path = optarg;
                break;
            case 'h':
                fprintf(stdout, "Usage: %s keygen [OPTIONS]\n", prog_name);
                fprintf(stdout, "\n");
                fprintf(stdout, "Generate ed25519 keypair and save to ~/.norn/key.pem\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Options:\n");
                fprintf(stdout, "  --key <path>   Key file path (default: ~/.norn/key.pem)\n");
                fprintf(stdout, "  --help         Show this help\n");
                fprintf(stdout, "\n");
                fprintf(stdout, "Environment:\n");
                fprintf(stdout, "  NORN_KEY       Default key file path\n");
                return 0;
            default:
                return 1;
        }
    }
    
    if (!key_path) {
        key_path = getenv("NORN_KEY");
    }
    if (!key_path) {
        key_path = get_default_key_path();
        if (!key_path) return 1;
    }
    
    if (access(key_path, F_OK) == 0) {
        fprintf(stderr, "ERROR: Key file already exists: %s\n", key_path);
        fprintf(stderr, "HINT: Remove the file or use --key to specify a different path\n");
        if (!getenv("NORN_KEY") && !key_file) free(key_path);
        return 1;
    }
    
    if (create_key_dir(key_path) != 0) {
        if (!getenv("NORN_KEY") && !key_file) free(key_path);
        return 1;
    }
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    FILE *f = fopen(key_path, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to create key file %s: %s\n", key_path, strerror(errno));
        if (!getenv("NORN_KEY") && !key_file) free(key_path);
        return 1;
    }
    
    if (fwrite(&kp, sizeof(kp), 1, f) != 1) {
        fprintf(stderr, "ERROR: Failed to write key file %s\n", key_path);
        fclose(f);
        if (!getenv("NORN_KEY") && !key_file) free(key_path);
        return 1;
    }
    
    fclose(f);
    chmod(key_path, 0600);
    
    for (int i = 0; i < 32; i++) {
        printf("%02x", kp.public_key[i]);
    }
    printf("\n");
    
    if (!getenv("NORN_KEY") && !key_file) free(key_path);
    return 0;
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
        {"read-only", no_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    
    while ((opt = getopt_long(argc, argv, "+k:p:t:l:rh", long_options, NULL)) != -1) {
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
            case 'r':
                read_only = 1;
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
    } else if (strcmp(cmd, "get") == 0) {
        return do_get(argc, argv);
    } else if (strcmp(cmd, "set") == 0) {
        return do_set(argc, argv);
    } else if (strcmp(cmd, "daemon") == 0) {
        return do_daemon(argc, argv);
    }
    
    fprintf(stderr, "ERROR: Unknown command: %s\n", cmd);
    usage(stderr);
    return 1;
}