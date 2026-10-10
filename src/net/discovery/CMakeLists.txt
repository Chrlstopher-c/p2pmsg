p2p_module(NAME net/discovery DEPS core/loop core/log p2p::avahi)
# Sans avahi-daemon (CI, conteneur valgrind) le test mDNS sort avec 77 et est compte comme SKIP.
set_tests_properties("net/discovery/test_mdns" PROPERTIES SKIP_RETURN_CODE 77)
