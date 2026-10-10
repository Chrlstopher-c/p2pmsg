find_package(Threads REQUIRED)
p2p_module(NAME core/loop DEPS core/log Threads::Threads)
