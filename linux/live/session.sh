#!/bin/sh
set -eu
export PATH=/usr/local/bin:/usr/bin:/bin
export XDG_CURRENT_DESKTOP=Bordify
export SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR=0
openbox &
exec /usr/local/bin/bordify-shell
