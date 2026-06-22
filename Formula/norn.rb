# norn — Mainline DHT client library
#
# Formula for Homebrew package manager (https://brew.sh)
#
# Install with:
#   brew install ./Formula/norn.rb
#
# Or from a tap:
#   brew tap your-org/norn
#   brew install norn

class Norn < Formula
  desc "Mainline DHT client library for P2P peer discovery"
  homepage "https://github.com/your-org/norn"
  version "0.1.0"
  license "MIT"

  # Adjust the URL and sha256 for your release
  url "https://github.com/your-org/norn/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"

  head "https://github.com/your-org/norn.git", branch: "main"

  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build
  depends_on "libsodium"

  def install
    # Bootstrap autotools
    system "./autogen.sh" if File.exist?("autogen.sh")
    
    # Configure with coverage disabled for production builds
    system "./configure", "--prefix=#{prefix}",
                          "--disable-coverage",
                          "--disable-silent-rules"
    
    # Build
    system "make"
    
    # Run tests
    system "make", "check"
    
    # Install
    system "make", "install"
  end

  test do
    # Test that library is installed
    assert_predicate lib/"libnorn.dylib", :exist? if OS.mac?
    assert_predicate lib/"libnorn.so", :exist? if OS.linux?
    
    # Test that header is installed
    assert_predicate include/"norn.h", :exist?
    
    # Test that pkg-config file exists
    assert_predicate lib/"pkgconfig/norn.pc", :exist?
    
    # Test CLI
    system bin/"norn", "version"
    
    # Test keygen
    output = shell_output("#{bin}/norn keygen 2>&1")
    assert_match(/Generated keypair/, output)
    assert_match(/Public:/, output)
  end

  def caveats
    <<~EOS
      norn provides a mainline DHT client library (libnorn) and CLI tool (norn).
      
      Library usage:
        #include <norn.h>
        -lnorn
      
      CLI usage:
        norn keygen              Generate ed25519 keypair
        norn get <pubkey>        Retrieve record from DHT
        norn set <value>         Store signed record to DHT
        norn daemon              Run DHT node
        
      For async/event-loop integration, use:
        int fd = norn_get_fd(client);
        norn_tick(client);  // Process pending transactions
    EOS
  end
end