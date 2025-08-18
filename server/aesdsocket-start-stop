#!/bin/sh

case $1 in
    start)
        echo "Starting the aesdsocket"
        start-stop-daemon -S -n aesdsocket --exec /usr/bin/aesdsocket -- -d
        ;;
    stop)
        echo "Stopping the aesdsocket"
        start-stop-daemon -K -n aesdsocket
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0
