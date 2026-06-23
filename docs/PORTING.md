# Porting Guide — Integrating norn into Your Application

This guide explains how to integrate the norn DHT client library into your application.

## Overview

norn is an in-memory DHT client library with no configuration files or disk I/O.
All state is managed in memory and controlled by the application through the API.

**Key Characteristics:**
- Single-threaded, event-loop compatible
- Non-blocking operations (async callbacks)
- No heap allocations in hot paths (arena allocators)
- Bounded memory usage (configurable budgets)
- Security-first design (signed records, replay protection)

## Building

### Prerequisites

- C99 compiler (gcc or clang)
- libsodium >= 1.0.0
- autoconf, automake, libtool (build only)

### Install Dependencies

**macOS (Homebrew):**
```bash
brew install libsodium autoconf automake libtool
```

**macOS (MacPorts):**
```bash
port install libsodium autoconf automake libtool
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install libsodium-dev autoconf automake libtool
```

**Linux (Alpine):**
```bash
apk add libsodium-dev autoconf automake libtool
```

**Linux (Fedora/RHEL):**
```bash
sudo dnf install libsodium-devel autoconf automake libtool
```

**FreeBSD:**
```bash
pkg install libsodium autoconf automake libtool
```

### Build Steps

```bash
./autogen.sh
./configure
make
make check
sudo make install
```

### Build Options

**Debug build:**
```bash
./configure CFLAGS="-g -O0"
make
```

**Coverage build:**
```bash
./configure --enable-coverage CFLAGS="-O0 -g"
make
make check
make coverage
```

**Install prefix:**
```bash
./configure --prefix=/usr/local
make
sudo make install
```

## Linking

### pkg-config

```bash
pkg-config --cflags norn
pkg-config --libs norn
```

### Makefile

```makefile
CFLAGS += $(shell pkg-config --cflags norn)
LDLIBS += $(shell pkg-config --libs norn)

myapp: myapp.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
```

### CMake

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(NORN REQUIRED norn)

add_executable(myapp myapp.c)
target_include_directories(myapp PRIVATE ${NORN_INCLUDE_DIRS})
target_link_libraries(myapp ${NORN_LIBRARIES})
```

### Manual Linking

```bash
gcc -o myapp myapp.c -lnorn -lsodium
```

## Basic Integration

### Minimal Example

```c
#include "norn.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    // Initialize crypto (required)
    if (crypto_init() < 0) {
        fprintf(stderr, "Failed to initialize crypto\n");
        return 1;
    }
    
    // Generate keypair
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    // Create DHT client
    norn_config_t cfg = {.version = "myapp/1.0"};
    norn_client_t *client = norn_new(kp.public_key, kp.secret_key, &cfg);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    // Bootstrap to DHT
    if (norn_bootstrap(client) != 0) {
        fprintf(stderr, "Bootstrap failed\n");
        norn_free(client);
        return 1;
    }
    
    // Event loop
    while (1) {
        norn_tick(client);
        usleep(100000);  // 100ms
    }
    
    norn_free(client);
    return 0;
}
```

### Async Get Example

```c
void on_value(void *user_data, const unsigned char *value, size_t value_len) {
    printf("Got value: %.*s\n", (int)value_len, value);
}

int main(void) {
    // ... initialize client ...
    
    unsigned char pubkey[32];
    // ... fill pubkey ...
    
    // Async get
    if (norn_get_mutable(client, pubkey, on_value, NULL) != 0) {
        fprintf(stderr, "Get failed\n");
        return 1;
    }
    
    // Wait for callback
    while (1) {
        norn_tick(client);
        usleep(100000);
    }
    
    return 0;
}
```

### Event Loop Integration

#### poll()

```c
int fd = norn_get_fd(client);

while (running) {
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    
    if (poll(&pfd, 1, 100) > 0) {  // 100ms timeout
        if (pfd.revents & POLLIN) {
            norn_tick(client);
        }
    }
}
```

#### select()

```c
int fd = norn_get_fd(client);

while (running) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
    
    if (select(fd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
        if (FD_ISSET(fd, &read_fds)) {
            norn_tick(client);
        }
    }
}
```

#### libevent

```c
#include <event2/event.h>

void on_socket(evutil_socket_t fd, short what, void *arg) {
    norn_client_t *client = arg;
    norn_tick(client);
}

int main(void) {
    // ... initialize client ...
    
    struct event_base *base = event_base_new();
    struct event *ev = event_new(base, norn_get_fd(client),
                                  EV_READ | EV_PERSIST, on_socket, client);
    event_add(ev, NULL);
    
    event_base_dispatch(base);
    
    event_free(ev);
    event_base_free(base);
    norn_free(client);
    return 0;
}
```

#### libuv

```c
#include <uv.h>

void on_socket(uv_poll_t *handle, int status, int events) {
    norn_client_t *client = handle->data;
    norn_tick(client);
}

int main(void) {
    // ... initialize client ...
    
    uv_loop_t *loop = uv_default_loop();
    uv_poll_t poll_handle;
    uv_poll_init_socket(loop, &poll_handle, norn_get_fd(client));
    poll_handle.data = client;
    uv_poll_start(&poll_handle, UV_READABLE, on_socket);
    
    uv_run(loop, UV_RUN_DEFAULT);
    
    uv_poll_stop(&poll_handle);
    norn_free(client);
    return 0;
}
```

## Configuration

### Read-Only Mode

Client-only mode: participates in DHT for discovery but doesn't respond to queries.

```c
norn_config_t cfg = {
    .version = "myapp/1.0",
    .read_only = 1
};
```

**Use when:**
- Mobile devices (battery conservation)
- Low-bandwidth connections
- Privacy-focused applications

### Private Mode

Bootstrap only to specified peers, not the public DHT.

```c
uint32_t boot_ips[] = {0x01020304};  // 1.2.3.4
uint16_t boot_ports[] = {6881};

norn_config_t cfg = {
    .version = "myapp/1.0",
    .private_mode = 1,
    .boot_ips = boot_ips,
    .boot_ports = boot_ports,
    .boot_count = 1
};
```

**Use when:**
- Private DHT networks
- Testing with specific bootstrap nodes
- Firewall-restricted environments

### Logging

```c
void my_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

norn_config_t cfg = {
    .version = "myapp/1.0",
    .log_func = my_log
};
```

## DHT Operations

### Put Mutable (Signed Record)

```c
keypair_t kp;
crypto_keypair_new(&kp);

unsigned char value[] = "Hello, DHT!";
uint32_t seq = 1;

if (norn_put_mutable(client, kp.public_key, kp.secret_key,
                     value, sizeof(value) - 1, seq) != 0) {
    fprintf(stderr, "Put failed\n");
}
```

### Get Mutable (Async)

```c
void on_value(void *user_data, const unsigned char *value, size_t value_len) {
    printf("Received: %.*s\n", (int)value_len, value);
}

unsigned char pubkey[32];
// ... fill pubkey ...

if (norn_get_mutable(client, pubkey, on_value, NULL) != 0) {
    fprintf(stderr, "Get failed\n");
}
```

### Put Immutable (Content-Addressed)

```c
unsigned char value[] = "Immutable content";

if (norn_put_immutable(client, value, sizeof(value) - 1) != 0) {
    fprintf(stderr, "Put immutable failed\n");
}
```

### Get Immutable (Async)

```c
void on_value(void *user_data, const unsigned char *value, size_t value_len) {
    printf("Received: %.*s\n", (int)value_len, value);
}

unsigned char key[20];
// ... compute SHA1(value) into key ...

if (norn_get_immutable(client, key, on_value, NULL) != 0) {
    fprintf(stderr, "Get immutable failed\n");
}
```

### Announce Peer

```c
unsigned char info_hash[20];
// ... fill info_hash ...

if (norn_announce(client, info_hash) != 0) {
    fprintf(stderr, "Announce failed\n");
}
```

### Discover Peers

```c
void on_peer(void *user_data, const unsigned char *pubkey, uint32_t ip, uint16_t port) {
    printf("Peer: %u.%u.%u.%u:%u\n",
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
           (ip >> 8) & 0xFF, ip & 0xFF,
           ntohs(port));
}

unsigned char info_hash[20];
// ... fill info_hash ...

if (norn_discover(client, info_hash, on_peer, NULL) != 0) {
    fprintf(stderr, "Discover failed\n");
}
```

## Memory Management

### Buffer Ownership

| Function | Ownership | Notes |
|----------|-----------|-------|
| `norn_new` | Caller owns returned handle | Free with `norn_free` |
| `norn_put_*` | norn copies value | Caller retains ownership |
| `norn_get_*` callback | norn owns value | Valid only during callback; copy if needed |
| `norn_encode_*` | Caller owns output | Provide buffer |
| `norn_decode_*` | Caller owns output struct | Input buffer still owned by caller |

### Memory Budgets

```c
// DHT storage (untrusted items from network)
// Default: RAM/512, max 64MB, min 2MB
size_t budget = dhtstore_init(-1, 0);  // Auto-detect

// Fixed budget: 10 MB
dhtstore_init(10, 0);

// Client-only: no storage
dhtstore_init(0, 1);
```

### Arena Allocators

norn uses arena allocators in hot paths:

```c
// No malloc/free per packet
// All allocations from pre-allocated arena

typedef struct {
    unsigned char arena[ARENA_SIZE];
    size_t arena_used;
} stream_t;

void *stream_alloc(stream_t *s, size_t len) {
    if (s->arena_used + len > sizeof(s->arena)) return NULL;
    void *ptr = s->arena + s->arena_used;
    s->arena_used += len;
    return ptr;
}
```

**Benefits:**
- No fragmentation
- Cache-friendly (contiguous memory)
- Bounded memory usage
- Deterministic performance

## Security

### Key Generation

```c
keypair_t kp;
crypto_keypair_new(&kp);

// Save to file
crypto_keypair_save(&kp, "/path/to/key");

// Load from file
crypto_keypair_load(&kp, "/path/to/key");
```

### Record Signing

```c
// Mutable item
unsigned char value[] = "Hello";
unsigned char buf[300], sig[64];
int len = bep44_signbuf(1, value, sizeof(value) - 1, buf, sizeof(buf));
bf_sign(sig, buf, len, kp.secret_key);

// Verify
if (bf_verify(sig, buf, len, kp.public_key) == 0) {
    printf("Signature valid\n");
}
```

### Replay Protection

```c
// Sequence numbers must increase monotonically
dhtstore_put(target, k, seq, v, vlen, sig, NULL, 0, src_ip);
// Internally checks: seq > existing_seq
```

## Threading

### Single-Threaded Design

norn is **single-threaded**. All operations are non-blocking:

```c
// Wrong: will block
norn_get_mutable(client, pubkey, callback, data);  // Returns immediately
// ... callback invoked later in norn_tick ...

// Right: poll regularly
while (running) {
    norn_tick(client);  // Process pending callbacks
    usleep(100000);
}
```

### Multi-Threading

For multi-threaded applications, use a mutex:

```c
pthread_mutex_t norn_lock = PTHREAD_MUTEX_INITIALIZER;

void tick_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&norn_lock);
        norn_tick(client);
        pthread_mutex_unlock(&norn_lock);
        usleep(100000);
    }
}

void api_thread(void *arg) {
    pthread_mutex_lock(&norn_lock);
    norn_put_mutable(client, pubkey, secret, value, len, seq);
    pthread_mutex_unlock(&norn_lock);
}
```

## Error Handling

### Return Values

| Type | Success | Error |
|------|----------|-------|
| `int` | `0` | `-1` |
| `pointer` | Valid pointer | `NULL` |
| `size_t` | Bytes/Count | `0` |

### NULL Safety

All public functions are NULL-safe:

```c
// Safe: returns -1
norn_get_id(NULL, id);

// Safe: does nothing
norn_free(NULL);
```

### Error Messages

Enable logging for debugging:

```c
void my_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

norn_config_t cfg = {
    .version = "myapp/1.0",
    .log_func = my_log
};
```

Set log level:

```c
bf_log_set_level(LOG_DEBUG);  // Most verbose
bf_log_set_level(LOG_INFO);   // Default
bf_log_set_level(LOG_WARN);   // Warnings and errors only
bf_log_set_level(LOG_ERROR);  // Errors only
```

## Platform Support

### Supported Platforms

- Linux: Alpine, Debian, Ubuntu, Fedora, RHEL, CentOS, Arch, OpenSUSE
- macOS: Homebrew, MacPorts
- FreeBSD: Ports

### Tested Compilers

- gcc >= 9.0
- clang >= 10.0

### Dependencies

- libsodium >= 1.0.0
- POSIX (for socket operations)

### Install Location

Default install prefix is `/usr/local`:

```
/usr/local/include/norn.h      # Public header
/usr/local/lib/libnorn.so       # Shared library
/usr/local/lib/libnorn.a        # Static library
/usr/local/lib/pkgconfig/norn.pc # pkg-config
```

## Troubleshooting

### Bootstrap Fails

```c
// Check firewall allows UDP 6881
// Check internet connectivity
// Try custom bootstrap nodes:

uint32_t boot_ips[] = {
    0x6A28287A,  // router.bittorrent.com (106.162.222.234)
    0x4A5DEE0F   // router.utorrent.com (74.222.238.47)
};
uint16_t boot_ports[] = {6881, 6881};

norn_config_t cfg = {
    .boot_ips = boot_ips,
    .boot_ports = boot_ports,
    .boot_count = 2
};
```

### Put Fails

```c
// Check value size (max 1000 bytes for BEP-44)
if (value_len > 1000) {
    fprintf(stderr, "Value too large\n");
    return -1;
}

// Check signature
unsigned char sig[64];
bf_sign(sig, buf, len, secret_key);
// Verify signature matches public key
```

### Get Never Calls Back

```c
// Make sure to call norn_tick regularly
while (running) {
    norn_tick(client);  // This processes callbacks
    usleep(100000);
}

// Check if bootstrap succeeded
int nodes = kad_routing_table_size(state);
if (nodes == 0) {
    fprintf(stderr, "No nodes in routing table\n");
}
```

### Memory Usage High

```c
// Reduce DHT storage budget
dhtstore_init(1, 0);  // 1 MB budget

// Enable read-only mode
norn_config_t cfg = {.read_only = 1};

// Use client-only mode (no storage)
dhtstore_init(0, 1);
```

## Examples

### Complete Example: Put and Get

```c
#include "norn.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int done = 0;

void on_get(void *user_data, const unsigned char *value, size_t value_len) {
    printf("Received: %.*s\n", (int)value_len, value);
    done = 1;
}

int main(void) {
    crypto_init();
    
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    norn_config_t cfg = {.version = "example/1.0"};
    norn_client_t *client = norn_new(kp.public_key, kp.secret_key, &cfg);
    if (!client) return 1;
    
    if (norn_bootstrap(client) != 0) return 1;
    
    // Wait for bootstrap
    for (int i = 0; i < 100; i++) {
        norn_tick(client);
        usleep(100000);
    }
    
    // Put
    const char *value = "Hello, DHT!";
    if (norn_put_mutable(client, kp.public_key, kp.secret_key,
                         (const unsigned char *)value, strlen(value), 1) != 0) {
        fprintf(stderr, "Put failed\n");
        return 1;
    }
    
    // Wait for put to propagate
    sleep(1);
    
    // Get
    if (norn_get_mutable(client, kp.public_key, on_get, NULL) != 0) {
        fprintf(stderr, "Get failed\n");
        return 1;
    }
    
    // Wait for callback
    while (!done) {
        norn_tick(client);
        usleep(100000);
    }
    
    norn_free(client);
    return 0;
}
```

## License

norn is licensed under the MIT License. See LICENSE file for details.

## Contact

- Issues: <https://github.com/anomalyco/norn/issues>
- Documentation: <https://github.com/anomalyco/norn>
- Email: norn@example.com