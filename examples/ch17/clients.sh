#!/bin/bash
# clients.sh PORT [N] - run N echo clients at the same time and check that
# each one gets its own lines back. Each client sends a line and waits for
# the echo, sleeps half a second, then does it again.
port=$1
n=${2:-50}

client() {
    exec 3<>"/dev/tcp/127.0.0.1/$port" || return   # bash connects for us
    echo "hello $1" >&3
    read -r a <&3
    sleep 0.5
    echo "bye $1" >&3
    read -r b <&3
    exec 3>&-                                       # close: the server sees EOF
    [ "$a" = "hello $1" ] && [ "$b" = "bye $1" ] && echo ok
}

start=$(date +%s.%N)
for i in $(seq "$n"); do
    client "$i" > "out.$i" &
done
wait                                                # for all the clients
end=$(date +%s.%N)
ok=$(cat $(seq -f 'out.%g' "$n") | grep -c ok)
rm -f $(seq -f 'out.%g' "$n")
echo "$ok of $n clients OK in $(awk "BEGIN { printf \"%.1f\", $end - $start }") s"
