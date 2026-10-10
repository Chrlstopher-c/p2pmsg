find_package(Threads REQUIRED)
# Dépendances système (pkg-config) exposées sous p2p::<nom>.
find_package(PkgConfig REQUIRED)

pkg_check_modules(P2P_PC_SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(P2P_PC_AVAHI REQUIRED IMPORTED_TARGET avahi-client)
pkg_check_modules(P2P_PC_GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_library(p2p::sqlite3 ALIAS PkgConfig::P2P_PC_SQLITE3)
add_library(p2p::avahi ALIAS PkgConfig::P2P_PC_AVAHI)
add_library(p2p::gtk4 ALIAS PkgConfig::P2P_PC_GTK4)
