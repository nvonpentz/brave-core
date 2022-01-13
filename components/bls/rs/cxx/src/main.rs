#[cxx::bridge(namespace = "org::bls")]
mod ffi {
  // Rust types and signatures exposed to C++.
  extern "Rust" {
      type fil_PrivateKeyPublicKeyResponse;

      unsafe fn fil_private_key_public_key(
          raw_private_key_ptr: *const u8,
      ) -> *mut fil_PrivateKeyPublicKeyResponse;
  }
}

use bls::types::fil_PrivateKeyPublicKeyResponse;
use bls::api::fil_private_key_public_key;
pub mod bls;


fn main() {
  println!("main");
}
