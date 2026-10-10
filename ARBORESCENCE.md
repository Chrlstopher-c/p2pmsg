# Arborescence (fichiers S0)

```
CMakeLists.txt            racine : options, flags, modules SPEC §4.1, enable_testing
CMakePresets.json         presets asan, tsan, release, debug (build/<preset>)
LICENSE                   MIT, p2pmsg contributors
cmake/P2PModule.cmake     fonction p2p_module (lib, tests CTest, valgrind)
cmake/P2PDeps.cmake       pkg-config : p2p::sqlite3, p2p::avahi, p2p::gtk4
cmake/NoiseC.cmake        noise-c ExternalProject, cible noise::protocol
scripts/check.sh          check complet ou par label, code retour 0 si vert
scripts/package.sh        release Linux : build, install staging, dist/p2pmsg-<v>-linux-x86_64.tar.gz + SHA256SUMS
scripts/grep-todo.sh      TODO/FIXME sans ISSUES.md#
start.sh stop.sh restart.sh  PID et logs (binaire release)
tests/test.h              mini framework de test header-only
.github/workflows/ci.yml  CI ubuntu-latest : deps, clone noise-c, check.sh, grep-todo.sh
.github/workflows/release.yml  tag v* : check.sh, package.sh, gh release create (ubuntu-24.04)
.gitignore                build/, dist/, logs/, PID, secrets, third_party/noise-c
.env.example              variables non sensibles
README.md STATE.md TODO.md ARBORESCENCE.md RESULTS.md ISSUES.md METRICS.md EPICS.md   documentation
```
