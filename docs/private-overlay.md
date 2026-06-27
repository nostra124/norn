# Private overlay vs. public-mainline bootstrap (FEAT-020)

norn supports two ways to join the network. Pick by whether your nodes should
be discoverable on the global BitTorrent mainline DHT, or only by each other.

## Public-mainline bootstrap (default)

The node bootstraps against the public mainline DHT (well-known routers), and
announces/looks up records there. Use this for nodes that want global,
permissionless reachability.

```c
norn_config_t cfg = { .version = "app/1.0" };   /* private_mode = 0 */
norn_client_t *c = norn_new(pub, sec, &cfg);
norn_bootstrap(c);
```

## Private overlay (closed fleet)

A closed fleet (e.g. one org's `regin` + `dvalin` + `raven` agents, or wyrd's
private packs/clans) forms a **private pubkey-mesh from its own bootstrap
node(s)** and never touches the public mainline:

- `private_mode = 1` — bootstrap only to the fleet's `boot_*` peers; no public
  announce or public lookups.
- members resolve each other **by public key** over the fleet's own overlay;
- NAT'd / containerised members stay reachable via the harmonised rendezvous +
  relay path (FEAT-017) *inside* the overlay.

### Building the config

`norn_overlay` (`norn_overlay.h`) is the first-class, validated way to describe
a fleet's bootstrap set — each peer is a **pubkey + endpoint** — and project it
into a private-mode `norn_config_t`:

```c
norn_overlay_t *ov = norn_overlay_new();
norn_overlay_add_peer(ov, seed_pubkey, seed_ip /*nbo*/, seed_port /*nbo*/);
/* ... more fleet bootstrap nodes ... */

norn_config_t cfg;
uint32_t ips[8]; uint16_t ports[8];
norn_overlay_to_config(ov, "fleet/1.0", &cfg, ips, ports, 8);
/* cfg.private_mode == 1, cfg.boot_* point at the fleet peers */

norn_client_t *c = norn_new(pub, sec, &cfg);
norn_bootstrap(c);   /* joins the private overlay only */
norn_overlay_free(ov);
```

The peer set is also the natural seed for a cluster's membership — the same
pubkeys feed `norn_cluster_bootstrap` (FEAT-025), so a fleet forms a private
overlay *and* a replicated KV store from one descriptor:

```c
unsigned char keys[8][32];
int n = norn_overlay_pubkeys(ov, keys, 8);
for (int i = 0; i < n; i++) norn_cluster_add_member(cl, keys[i], NORN_NODE_SERVER, 1);
```

### Guarantees

1. A fleet of ≥3 nodes forms a private overlay from a single bootstrap node and
   resolves each other by pubkey, **never touching the public DHT**
   (`private_mode = 1`).
2. A NAT'd member is reachable via rendezvous/relay **inside** the overlay
   (FEAT-017).
3. `norn_overlay` validates the peer set (no zero keys/endpoints, no
   duplicates, bounded size) before it ever reaches the network.

## Choosing

| | Public-mainline | Private overlay |
|---|---|---|
| Discoverability | global, permissionless | fleet-only, by pubkey |
| Bootstrap | public routers | your `boot_*` nodes |
| Announces on mainline | yes | **no** |
| Use for | open apps, wide reach | closed fleets, packs/clans |

## Verification

`tests/test_overlay_net.c` forms a 3-node private overlay over loopback (no
public DHT) from a single bootstrap node and checks that the fleet converges,
that members resolve each other by their pubkey-derived DHT ids, and that every
routing table stays fleet-only (public routers are never contacted under
`private_mode`). The remaining acceptance — a NAT'd member reachable via
rendezvous/relay *inside* the overlay — rides FEAT-022 (open) and is a real
network (PIT) check.
