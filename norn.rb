# norn — Mainline DHT client library

class Norn < Formula
  desc "Mainline DHT client library for P2P peer discovery"
  homepage "https://github.com/anomalyco/norn"
  license "MIT"
  head "https://github.com/anomalyco/norn.git", branch: "main"

  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build
  depends_on "libsodium"

  def install
    system "./autogen.sh"
    system "./configure", "--prefix=#{prefix}"
    system "make"
    system "make", "install"
  end

  test do
    (testpath/"test.c").write <<~EOS
      #include "norn.h"
      #include <stdio.h>
      #include <assert.h>
      
      int main(void) {
          unsigned char pubkey[32], secret[64];
          assert(crypto_keypair_new(&(keypair_t){.public_key = pubkey, .secret_key = secret}) == 0);
          printf("norn initialized successfully\\n");
          return 0;
      }
    EOS
    
    system ENV.cc, "test.c", "-I#{include}", "-L#{lib}", "-lnorn", "-lsodium", "-o", "test"
    system "./test"
  end
end