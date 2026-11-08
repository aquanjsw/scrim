cmake_minimum_required(VERSION 3.10)

project(scrim C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED IMPORTED_TARGET wayland-protocols)
pkg_check_modules(WAYLAND_SCANNER REQUIRED IMPORTED_TARGET wayland-scanner)
pkg_check_modules(WLR_PROTOCOLS REQUIRED IMPORTED_TARGET wlr-protocols)

pkg_get_variable(WAYLAND_SCANNER_BIN wayland-scanner wayland_scanner)
pkg_get_variable(WLR_PROTOCOLS_PKGDATADIR wlr-protocols pkgdatadir)
pkg_get_variable(WAYLAND_PROTOCOLS_PKGDATADIR wayland-protocols pkgdatadir)

set(LAYER_SHELL_XML
    "${WLR_PROTOCOLS_PKGDATADIR}/unstable/wlr-layer-shell-unstable-v1.xml")
set(LAYER_SHELL_CLIENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client.h")
set(LAYER_SHELL_PRIVATE_CODE
    "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1.c")

set(XDG_SHELL_XML
    "${WAYLAND_PROTOCOLS_PKGDATADIR}/stable/xdg-shell/xdg-shell.xml"
)
set(XDG_SHELL_CLIENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client.h")
set(XDG_SHELL_PRIVATE_CODE
    "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell.c")

add_custom_command(
  OUTPUT ${LAYER_SHELL_CLIENT_HEADER} ${LAYER_SHELL_PRIVATE_CODE}
  COMMAND ${WAYLAND_SCANNER_BIN}
  ARGS client-header ${LAYER_SHELL_XML} ${LAYER_SHELL_CLIENT_HEADER}
  COMMAND ${WAYLAND_SCANNER_BIN}
  ARGS private-code ${LAYER_SHELL_XML} ${LAYER_SHELL_PRIVATE_CODE}
  DEPENDS ${LAYER_SHELL_XML}
  COMMENT "Generating wlr-layer-shell headers and source"
)

add_custom_command(
  OUTPUT ${XDG_SHELL_CLIENT_HEADER} ${XDG_SHELL_PRIVATE_CODE}
  COMMAND ${WAYLAND_SCANNER_BIN}
  ARGS client-header ${XDG_SHELL_XML} ${XDG_SHELL_CLIENT_HEADER}
  COMMAND ${WAYLAND_SCANNER_BIN}
  ARGS private-code ${XDG_SHELL_XML} ${XDG_SHELL_PRIVATE_CODE}
  DEPENDS ${XDG_SHELL_XML}
  COMMENT "Generating xdg-shell headers and source"
)

add_executable(scrim
  src/scrim.c
  ${LAYER_SHELL_CLIENT_HEADER}
  ${LAYER_SHELL_PRIVATE_CODE}
  ${XDG_SHELL_CLIENT_HEADER}
  ${XDG_SHELL_PRIVATE_CODE}
)

target_include_directories(scrim PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
  ${WAYLAND_PROTOCOLS_INCLUDE_DIRS}
)

target_link_libraries(scrim PRIVATE
  ${WAYLAND_CLIENT_LIBRARIES}
  ${WAYLAND_PROTOCOLS_LIBRARIES}
)

install(TARGETS scrim DESTINATION bin)
