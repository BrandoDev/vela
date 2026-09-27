# La sessione: voce in SDDM, avvio, collegamento a systemd e ai portali.

configure_file(vela-session.in vela-session @ONLY)
configure_file(vela.desktop.in vela.desktop @ONLY)
# Anche accanto al compositor nella cartella di build, dove lo cerca per primo.
configure_file(vela-session-env ${CMAKE_BINARY_DIR}/compositor/vela-session-env COPYONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/vela-session DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS vela-session-env DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vela.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
install(FILES vela-portals.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal)
install(FILES vela-session.target DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
# Configurazione di xdg-desktop-portal-wlr quando il desktop è Vela.
install(FILES xdpw-vela.conf DESTINATION /etc/xdg/xdg-desktop-portal-wlr RENAME Vela)
