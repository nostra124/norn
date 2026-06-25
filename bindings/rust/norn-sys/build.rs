//! Locate and link libnorn.
//!
//! Resolution order:
//!   1. `NORN_LIB_DIR` (and optional `NORN_INCLUDE_DIR`) — explicit, used for
//!      building against an in-tree checkout (point it at `<repo>/.libs`).
//!   2. `pkg-config --libs norn` — for an installed libnorn (norn.pc).
//!   3. Fall back to a bare `-lnorn -lsodium` and hope the linker finds them.
//!
//! No crates.io build-dependencies, so this builds offline.

use std::process::Command;

fn main() {
    println!("cargo:rerun-if-env-changed=NORN_LIB_DIR");
    println!("cargo:rerun-if-env-changed=SODIUM_LIB_DIR");

    if let Ok(dir) = std::env::var("NORN_LIB_DIR") {
        println!("cargo:rustc-link-search=native={dir}");
        if let Ok(sodium_dir) = std::env::var("SODIUM_LIB_DIR") {
            println!("cargo:rustc-link-search=native={sodium_dir}");
        }
        println!("cargo:rustc-link-lib=dylib=norn");
        println!("cargo:rustc-link-lib=dylib=sodium");
        return;
    }

    // Try pkg-config for an installed libnorn.
    if let Ok(out) = Command::new("pkg-config").args(["--libs", "norn"]).output() {
        if out.status.success() {
            let flags = String::from_utf8_lossy(&out.stdout);
            for tok in flags.split_whitespace() {
                if let Some(path) = tok.strip_prefix("-L") {
                    println!("cargo:rustc-link-search=native={path}");
                } else if let Some(lib) = tok.strip_prefix("-l") {
                    println!("cargo:rustc-link-lib=dylib={lib}");
                }
            }
            // pkg-config Requires: libsodium already pulls sodium in via --libs.
            return;
        }
    }

    // Last resort.
    println!("cargo:rustc-link-lib=dylib=norn");
    println!("cargo:rustc-link-lib=dylib=sodium");
}
