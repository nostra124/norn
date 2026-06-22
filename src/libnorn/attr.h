#ifndef NORN_ATTR_H
#define NORN_ATTR_H

/* Node attributes — a simple key-value store for self-served metadata.
 * Attributes are NOT published to the DHT; they're shared directly with peers
 * during handshake/identity exchange or queried on-demand.
 *
 * Examples:
 *   "version" -> "0.20.1"
 *   "type" -> "client" / "relay" / "exit"
 *   "caps" -> bitmask of capabilities
 *   "name" -> human-readable node name
 */

#include <stddef.h>

/* Set a node attribute (copying key and value). Returns 0 on success, -1 on error.
 * If key already exists, value is replaced. Both key and value are copied. */
int norn_set_attr(const char *key, const void *value, size_t value_len);

/* Get a node attribute. Copies up to cap bytes into out, returns actual length
 * or -1 if key not found. Returns -1 if cap < value_len (caller can retry). */
int norn_get_attr(const char *key, void *out, size_t cap);

/* Delete a node attribute. Returns 0 on success, -1 if not found. */
int norn_del_attr(const char *key);

/* Count of attributes. */
size_t norn_attr_count(void);

/* Clear all attributes (for testing). */
void norn_attr_clear(void);

/* Built-in attributes (automatically set on init) */
const char *norn_version(void);      /* Returns library version string */
const char *norn_node_type(void);    /* Returns "norn" (can be overridden) */

#endif /* NORN_ATTR_H */