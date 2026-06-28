# norn — Mainline DHT, secure sessions and cluster KV (library + CLI + daemon)
#
# Homebrew formula (https://brew.sh).
#
# Install the released version from a tap:
#   brew tap nostra124/norn
#   brew install norn
#
# Or build the latest from git:
#   brew install --HEAD ./Formula/norn.rb

class Norn < Formula
  desc "Mainline DHT, secure sessions and cluster KV store (CLI + node daemon)"
  homepage "https://github.com/nostra124/norn"
  license "MIT"

  url "https://github.com/nostra124/norn/archive/refs/tags/v0.12.0.tar.gz"
  sha256 "8aa1cc3c10a7702b644a10f3f6118d20eb02a16a8e3c6d2e874c42518801a460"
  head "https://github.com/nostra124/norn.git", branch: "master"

  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build
  depends_on "pkg-config" => :build
  depends_on "libsodium"

  def install
    system "./autogen.sh"
    system "./configure", *std_configure_args, "--disable-silent-rules"
    system "make"
    system "make", "check"
    system "make", "install"
  end

  # Run nornd as a per-user daemon: `brew services start norn`.
  service do
    run [opt_bin/"nornd", "--foreground"]
    keep_alive true
    log_path var/"log/nornd.log"
    error_log_path var/"log/nornd.log"
  end

  test do
    # Library, headers and pkg-config metadata are installed.
    assert_predicate lib/"libnorn.dylib", :exist? if OS.mac?
    assert_predicate lib/"libnorn.so", :exist? if OS.linux?
    assert_predicate include/"norn.h", :exist?
    assert_predicate lib/"pkgconfig/norn.pc", :exist?

    # CLI works and reports a version.
    assert_match(/norn \d+\.\d+\.\d+/, shell_output("#{bin}/norn version"))

    # keygen writes a 0600 key and prints the 64-hex public key.
    output = shell_output("HOME=#{testpath} #{bin}/norn keygen").strip
    assert_match(/\A[0-9a-f]{64}\z/, output)
    assert_predicate testpath/".norn/key.pem", :exist?

    # The daemon binary is present and shows its usage.
    assert_match "nornd", shell_output("#{bin}/nornd --bogus 2>&1", 2)
  end

  def caveats
    <<~EOS
      norn installs:
        norn           CLI (DHT + a thin client to nornd)
        nornd          node daemon hosting the cluster KV store
        norn-forward   TCP-over-norn tunnel
        libnorn        shared/static library (#include <norn.h>, pkg-config norn)

      Run the node daemon as a per-user service:
        brew services start norn

      It uses your SSH key (~/.ssh/id_ed25519) as the node identity and serves
      the IPC socket at $XDG_RUNTIME_DIR/nornd.sock. Then:
        norn cluster put <k> <v>
        norn cluster get <k>
        norn keys <nodeid>
    EOS
  end
end
