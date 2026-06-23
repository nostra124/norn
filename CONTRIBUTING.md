# Contributing to norn

Thank you for your interest in contributing to norn! This guide explains how to set up your development environment, follow our conventions, and submit pull requests.

## Development Setup

### Prerequisites

- **C compiler:** gcc or clang (C99 support required)
- **libsodium:** >= 1.0.0 (for Ed25519, X25519, crypto_box)
- **build tools:** autoconf, automake, libtool
- **make:** GNU make or BSD make
- **git:** For version control

### Platform-Specific Instructions

#### Linux (Debian/Ubuntu)

```bash
sudo apt-get update
sudo apt-get install build-essential autoconf automake libtool libsodium-dev
```

#### Linux (Alpine)

```bash
apk add gcc make autoconf automake libtool libsodium-dev
```

#### Linux (Fedora/RHEL/CentOS)

```bash
sudo dnf install gcc make autoconf automake libtool libsodium-devel
```

#### macOS (Homebrew)

```bash
brew install libsodium autoconf automake libtool
```

#### macOS (MacPorts)

```bash
port install libsodium autoconf automake libtool
```

#### FreeBSD

```bash
pkg install gcc gmake autoconf automake libtool libsodium
```

### Building

```bash
# Clone the repository
git clone https://github.com/anomalyco/norn.git
cd norn

# Generate build system
./autogen.sh

# Configure
./configure

# Build
make

# Run tests
make check

# Install (optional)
sudo make install
```

### Debug Build

```bash
./configure CFLAGS="-g -O0"
make
make check
```

### Coverage Build

```bash
./configure --enable-coverage CFLAGS="-O0 -g"
make
make check
make coverage
```

The coverage gate enforces **100% line AND branch coverage** for tracked files. See `tests/coverage-tracked.txt` for the list of tracked modules.

## Development Workflow

norn follows a **Test-Driven Development (TDD)** workflow. Every feature starts with a failing test.

### TDD Process

1. **Write an issue** describing the feature or bug fix
2. **Write a failing test** that demonstrates the desired behavior
3. **Implement** the minimum code to make the test pass
4. **Commit** with issue reference

### Example: Adding a New Function

**1. Create the issue:**

```markdown
FEAT-XXX: Add norn_version() function

As a norn user, I want to query the library version
So that I can check compatibility at runtime

Acceptance Criteria:
- Returns version string (e.g., "0.1.0")
- Thread-safe
- NULL-safe (no params)
```

**2. Write the test:**

```c
/* tests/test_norn.c */

static void test_version(void) {
    const char *v = norn_version();
    assert(v != NULL);
    assert(v[0] >= '0' && v[0] <= '9');  // Starts with digit
    printf("  test_version: OK\n");
}

int main(void) {
    printf("test_norn:\n");
    test_version();
    // ... other tests ...
    return 0;
}
```

**3. Run the test (should fail):**

```bash
make check
# test_norn.c: undefined reference to `norn_version'
```

**4. Implement the function:**

```c
/* src/libnorn/norn.h */

const char *norn_version(void);

/* src/libnorn/norn.c */

const char *norn_version(void) {
    return "0.1.0";
}
```

**5. Run the test (should pass):**

```bash
make check
# PASS: test_norn
```

**6. Commit:**

```bash
git add tests/test_norn.c src/libnorn/norn.h src/libnorn/norn.c
git commit -m "FEAT-XXX: Add norn_version() function"
```

### Test Naming Convention

Tests follow the **dvalin methodology**: `test_<function>_<scenario>`

```c
test_init                    // Basic init test
test_init_null               // Init with NULL param
test_put_mutable             // Basic put
test_put_mutable_large      // Put with large value
test_put_mutable_null       // Put with NULL param
```

### Test Structure

```c
static void test_<function>_<scenario>(void) {
    // Setup
    keypair_t kp;
    crypto_keypair_new(&kp);
    
    // Exercise
    int result = norn_put_mutable(client, kp.public_key, ...);
    
    // Verify
    assert(result == 0);
    assert(dhtstore_count() == 1);
    
    // Cleanup (if needed)
    
    printf("  test_<function>_<scenario>: OK\n");
}

int main(void) {
    crypto_init();
    printf("test_<module>:\n");
    
    test_<function>_<scenario>();
    // ... more tests ...
    
    printf("test_<module>: OK\n");
    return 0;
}
```

## Code Style

### C99

norn uses C99 features:

- `//` comments
- Variable declarations anywhere
- `stdint.h` types (`uint32_t`, `size_t`, etc.)
- Designated initializers
- Compound literals

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Functions | `module_action_object` | `norn_get_mutable`, `dhtstore_put` |
| Types | `module_object_t` | `norn_client_t`, `keypair_t` |
| Constants | `MODULE_CONSTANT` | `DHTSTORE_VMAX`, `NORN_ID_BYTES` |
| Struct fields | `snake_case` | `node_id`, `value_len` |
| Local variables | `snake_case` | `seq`, `target`, `vlen` |

### Memory Rules

1. **No heap allocations in hot paths** — Use arena/pool allocators
2. **Caller owns output buffers** — Functions don't allocate, caller provides
3. **Return int or pointer** — `0` success / `-1` error, or valid pointer / `NULL`
4. **All public functions NULL-safe** — Handle NULL inputs gracefully

```c
// Good: Caller owns buffer
int norn_get_id(const norn_client_t *client, unsigned char out[NORN_ID_BYTES]) {
    if (!client || !out) return -1;  // NULL-safe
    memcpy(out, client->id, 20);
    return 0;
}

// Bad: Function allocates
unsigned char *norn_get_id(const norn_client_t *client) {
    unsigned char *out = malloc(20);  // Don't do this
    // ...
}
```

### Comments

```c
/* One-line comment for simple things */
int count = 0;

/* Multi-line comment for complex logic:
 * - Explain WHY, not WHAT
 * - Reference BEPs or issues
 * - Document invariants
 * 
 * FEAT-XXX: This implements BEP-44 mutable items.
 * The sequence number must be monotonically increasing.
 */
int dhtstore_put(...) {
    // ...
}
```

### Header Documentation

Every public function has Doxygen-style documentation:

```c
/**
 * @brief Brief one-line description
 * 
 * Detailed description of what the function does.
 * 
 * @param param_name Description of this parameter
 * @return Description of return value
 * 
 * @note Thread safety: Is this function thread-safe?
 * @note Ownership: Who owns allocated memory?
 * 
 * Example:
 *   unsigned char id[20];
 *   if (norn_get_id(client, id) == 0) {
 *       printf("Node ID: %02x%02x...\n", id[0], id[1]);
 *   }
 */
```

## Testing Guidelines

### What to Test

1. **Normal operation** — Happy path
2. **NULL inputs** — All public functions must handle NULL
3. **Edge cases** — Empty strings, max sizes, boundary values
4. **Error conditions** — Invalid inputs, resource exhaustion

### Test Structure

```c
static void test_<function>_<scenario>(void) {
    // 1. Setup
    // 2. Exercise
    // 3. Verify
    // 4. Cleanup
    printf("  test_<function>_<scenario>: OK\n");
}
```

### Coverage Requirements

- **100% line coverage** for all tracked modules
- **100% branch coverage** for all tracked modules
- Tracked modules listed in `tests/coverage-tracked.txt`

```bash
make coverage
# PASS: src/libnorn/norn.c - Lines: 100%, Branches: 100%
# PASS: src/libnorn/bep44.c - Lines: 100%, Branches: 100%
# ...
# Coverage gate PASSED
```

### Excluding Code from Coverage

Only exclude genuinely untestable code:

```c
/* LCOV_EXCL_LINE: unreachable in libsodium */
if (crypto_sign_detached(sig, NULL, msg, len, sk) != 0) {
    return -1;  /* This line excluded */
}
```

### Adding New Tests

1. Create `tests/test_<module>.c`
2. Add to `Makefile.am`:
   ```makefile
   test_<module>_LDADD = libnorn.la
   test_<module>_SOURCES = tests/test_<module>.c
   check_PROGRAMS += test_<module>
   TESTS += test_<module>
   ```
3. Add module to `tests/coverage-tracked.txt`
4. Write tests
5. Run `make check && make coverage`

## Pull Request Process

### Before Submitting

1. **Issue exists** — PR must reference an existing issue
2. **Branch from main** — Create feature branch: `git checkout -b FEAT-XXX-description`
3. **Tests pass** — `make check` passes
4. **Coverage 100%** — `make coverage` passes
5. **No compiler warnings** — `make` produces no warnings
6. **Documentation updated** — Public APIs have Doxygen comments
7. **Commit messages** — Reference issue: `FEAT-XXX: Description`

### Submitting

1. Push to your fork:
   ```bash
   git push origin FEAT-XXX-description
   ```
2. Create PR on GitHub
3. Fill in PR template:
   - Issue reference
   - Summary of changes
   - Testing performed
   - Coverage achieved

### Code Review Checklist

Reviewers check:

- [ ] Issue exists and is referenced
- [ ] Tests exist and pass (`make check`)
- [ ] Coverage 100% for affected modules (`make coverage`)
- [ ] No compiler warnings
- [ ] Code follows style guide
- [ ] Public APIs documented with Doxygen
- [ ] No heap allocations in hot paths
- [ ] All public functions NULL-safe
- [ ] Error conditions handled
- [ ] Commit messages reference issue

### After Merge

- Delete feature branch
- Close issue
- Update CHANGELOG (if applicable)

## Code Review

### What Reviewers Look For

#### Correctness

- Logic is correct
- Edge cases handled
- Error conditions checked
- Invariants maintained

#### Security

- Input validation
- Buffer overflow prevention
- Integer overflow prevention
- No sensitive data in logs

#### Performance

- No heap allocations in hot paths
- No unnecessary copies
- Efficient algorithms
- Bounded memory usage

#### Maintainability

- Clear variable names
- Well-commented complex logic
- Consistent style
- DRY (Don't Repeat Yourself)

### Common Issues

| Issue | Fix |
|-------|-----|
| NULL not checked | Add `if (!param) return -1;` |
| Buffer overflow | Check buffer size before write |
| Missing test | Add test for new code path |
| Heap allocation | Use arena or make caller-owned |
| No documentation | Add Doxygen comment |

## Project Structure

```
norn/
├── src/
│   ├── libnorn/          # Library implementation
│   │   ├── norn.c        # Public API
│   │   ├── bep44.c       # BEP-44 implementation
│   │   ├── dhtstore.c    # DHT storage
│   │   └── ...
│   └── norn.c            # CLI (thin wrapper)
├── tests/
│   ├── test_norn.c       # Unit tests
│   ├── test_bep44.c
│   ├── coverage-tracked.txt
│   └── ...
├── docs/
│   ├── architecture.md   # Architecture documentation
│   ├── BEP-REFERENCES.md # BEP summaries
│   └── PORTING.md        # Integration guide
├── .repo/
│   └── project/
│       └── issues/       # Milestones and issues
├── autogen.sh
├── configure.ac
├── Makefile.am
└── CONTRIBUTING.md
```

## Getting Help

### Documentation

- [Architecture](docs/architecture.md) — Module relationships, data flow
- [API Reference](src/libnorn/norn.h) — Public API documentation
- [BEP References](docs/BEP-REFERENCES.md) — BEP-5, BEP-44, BEP-43
- [Porting Guide](docs/PORTING.md) — Integration examples

### Issues

- [GitHub Issues](https://github.com/anomalyco/norn/issues) — Bug reports, feature requests
- Issue template guides you through required information

### Discussions

- [GitHub Discussions](https://github.com/anomalyco/norn/discussions) — Questions, ideas

### Email

- norn@example.com — Private security issues

## Release Process

1. Update version in `configure.ac`
2. Update `CHANGELOG.md`
3. Create tag: `git tag -a v0.X.Y -m "Release v0.X.Y"`
4. Push tag: `git push origin v0.X.Y`
5. CI builds and publishes

## License

norn is licensed under the MIT License. By contributing, you agree that your contributions will be licensed under the same license.

---

Thank you for contributing to norn! Your help makes the project better for everyone.