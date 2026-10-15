#!/bin/sh
# Source this only for the rootless development toolchain.
REWORD_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export CMAKE_PREFIX_PATH="$REWORD_ROOT/.deps/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export LD_LIBRARY_PATH="$REWORD_ROOT/.deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$REWORD_ROOT/.deps/usr/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CPATH="$REWORD_ROOT/.deps/usr/include${CPATH:+:$CPATH}"
export QML_IMPORT_PATH="$REWORD_ROOT/.deps/usr/lib/x86_64-linux-gnu/qt6/qml"
export QT_PLUGIN_PATH="$REWORD_ROOT/.deps/usr/lib/x86_64-linux-gnu/qt6/plugins"
exec "$@"
