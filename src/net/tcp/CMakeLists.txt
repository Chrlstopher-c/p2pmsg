find_package(Threads REQUIRED)
p2p_module(NAME net/tcp DEPS core/loop core/buf core/log Threads::Threads)
