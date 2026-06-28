#!/bin/bash
# PIT: bring up a random-sized (5-10 node) nornd fleet over loopback UDP — a full
# pubkey mesh on one port each — and assert the cluster converges: exactly one
# leader, every node sees all N members, and a write on one node replicates to
# all. More nodes => more election/replication randomness and scaling coverage
# than a fixed 2-node smoke.
#
# Usage: contrib/pit/cluster_fleet.sh [N]   (N optional; default random 5..10)
# Env:   NORND / NORN  (binary paths; default ./nornd ./norn)
set -u

NORND="${NORND:-./nornd}"
NORN="${NORN:-./norn}"
N="${1:-$(( (RANDOM % 6) + 5 ))}"          # random 5..10 unless given
BASE_PORT=$(( 40000 + (RANDOM % 2000) ))
DIR=$(mktemp -d /tmp/norn_pit_XXXXXX)
PIDS=()
PUBS=(); PORTS=(); SOCKS=()

cleanup() { for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT

echo "== PIT: $N-node nornd fleet (ports from $BASE_PORT) in $DIR =="

# Generate identities + pubkeys + port/socket assignments.
for ((i=0; i<N; i++)); do
    ssh-keygen -t ed25519 -N "" -f "$DIR/id$i" -C "node$i" >/dev/null 2>&1
    PUBS[$i]=$(python3 -c "import base64;print(base64.b64decode(open('$DIR/id$i.pub').read().split()[1])[-32:].hex())")
    PORTS[$i]=$(( BASE_PORT + i ))
    SOCKS[$i]="$DIR/n$i.sock"
done

# Launch each node peered to every other (full mesh).
for ((i=0; i<N; i++)); do
    peers=()
    for ((j=0; j<N; j++)); do
        [ "$j" -eq "$i" ] && continue
        peers+=( --peer "${PUBS[$j]}@127.0.0.1:${PORTS[$j]}" )
    done
    "$NORND" --identity "$DIR/id$i" --socket "${SOCKS[$i]}" \
        --listen-port "${PORTS[$i]}" --class server "${peers[@]}" \
        >"$DIR/n$i.log" 2>&1 &
    PIDS[$i]=$!
done

# Wait for sockets, then for convergence (election can take a few rounds).
for ((t=0; t<80; t++)); do
    up=0; for ((i=0; i<N; i++)); do [ -S "${SOCKS[$i]}" ] && up=$((up+1)); done
    [ "$up" -eq "$N" ] && break; sleep 0.1
done
sleep 8

# Assert: exactly one leader, and every node sees all N members.
leaders=0; bad=0
for ((i=0; i<N; i++)); do
    st=$(NORN_SOCK="${SOCKS[$i]}" "$NORN" cluster status 2>/dev/null)
    role=$(echo "$st" | awk '/^role:/{print $2}')
    mem=$(echo "$st" | awk '/^members:/{print $2}')
    echo "node$i: role=$role members=$mem"
    [ "$role" = "leader" ] && leaders=$((leaders+1))
    [ "$mem" = "$N" ] || bad=$((bad+1))
done
echo "leaders=$leaders members-correct-on=$((N-bad))/$N"
[ "$leaders" -eq 1 ] || { echo "FAIL: expected exactly 1 leader, got $leaders"; exit 1; }
[ "$bad" -eq 0 ] || { echo "FAIL: $bad nodes did not see all $N members"; exit 1; }

# Replication: write on a random node, read it back on every node.
w=$(( RANDOM % N ))
echo "== put fleet/key=hello on node$w =="
NORN_SOCK="${SOCKS[$w]}" "$NORN" cluster put fleet/key hello >/dev/null 2>&1
sleep 3
miss=0
for ((i=0; i<N; i++)); do
    v=$(NORN_SOCK="${SOCKS[$i]}" "$NORN" cluster get fleet/key 2>/dev/null)
    [ "$v" = "hello" ] || { echo "node$i: get=>'$v' (expected hello)"; miss=$((miss+1)); }
done
[ "$miss" -eq 0 ] || { echo "FAIL: $miss/$N nodes missing the replicated value"; exit 1; }

echo "PASS: $N-node fleet — 1 leader, all see $N members, value replicated to all $N"
