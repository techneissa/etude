# Monocypher 4.0.2

Vendored unmodified from https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz (tarball SHA-256 `bc1ca30b1b2654e4e7daf2492c0d204200e55137f23fda6b7142fd7d523bd6b4`): `src/monocypher.c`, `src/monocypher.h`, `src/optional/monocypher-ed25519.c`, `src/optional/monocypher-ed25519.h` and `LICENCE.md`. BSD-2-Clause or CC0-1.0 (your choice), compatible with GPL-3.0. It is vendored, not fetched, because a GitHub-generated tag archive is not guaranteed to keep its bytes. It is the ed25519 and BLAKE2b behind the update signature check (`core/src/minisign.cpp`). To upgrade, copy the files from a new release, update this page and run the signature tests (`tests/update_test.cpp`).

SHA-256 of the files here:

```
02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d  monocypher.c
fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3  monocypher.h
97d581639dfa72be08a6d57deb7d79b736be001cb416819cab196d22559d242b  monocypher-ed25519.c
3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce  monocypher-ed25519.h
a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0  LICENCE.md
```
