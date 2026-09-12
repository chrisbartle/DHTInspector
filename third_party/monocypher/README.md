# Monocypher (vendored)

Ed25519 signature verification for BEP 44 mutable items. Qt provides no
Ed25519, and this project builds against a static Qt with no crypto library,
so the implementation is copied in rather than pulled from a package.

| | |
|---|---|
| Upstream | https://github.com/LoupVaillant/Monocypher |
| Version | 4.0.3 |
| Licence | Dual BSD-2-Clause / CC-0, see `LICENCE.md` |

Copied verbatim from the 4.0.3 release:

| File | Upstream path | SHA-256 (first 16) |
|---|---|---|
| `monocypher.h` | `src/monocypher.h` | `fcaf6ed771358bb4` |
| `monocypher.c` | `src/monocypher.c` | `f1f838cdd483bdeb` |
| `monocypher-ed25519.h` | `src/optional/monocypher-ed25519.h` | `3a3035181f991a15` |
| `monocypher-ed25519.c` | `src/optional/monocypher-ed25519.c` | `ce0d2f8e32ca8f66` |
| `LICENCE.md` | `LICENCE.md` | `a5781770269d2516` |

## Why the "optional" files

Monocypher's own EdDSA uses BLAKE2b, which is not what BEP 44 signs with.
The RFC 8032 variant, which uses SHA-512 and is what BitTorrent clients use,
lives in `monocypher-ed25519`. That is the one wired up in
`dhtcore/src/Bep44.cpp`, through `crypto_ed25519_check` and
`crypto_ed25519_sign`.

## Updating

Replace the files from a new release, update the version and hashes above,
and run the test suite: `TestBep44` checks the implementation against the
RFC 8032 test vectors.
