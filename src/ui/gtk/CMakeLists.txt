p2p_module(NAME ui/gtk
    SOURCES ui_model.c ui_options.c ui_bridge.c ui_css.c ui_conv.c ui_window.c
    DEPS app/api p2p::gtk4)
add_executable(p2pmsg-gtk main.c)
target_include_directories(p2pmsg-gtk PRIVATE "${P2P_SRC_ROOT}")
target_link_libraries(p2pmsg-gtk PRIVATE p2p_ui_gtk p2p_app_api p2p::gtk4)
