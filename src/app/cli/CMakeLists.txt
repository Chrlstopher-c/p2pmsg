p2p_module(NAME app/cli SOURCES cli_parse.c DEPS app/api core/log)
add_executable(p2pmsg main.c)
target_link_libraries(p2pmsg PRIVATE p2p_app_cli p2p_app_api)
