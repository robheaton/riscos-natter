#!/bin/bash
# run.sh: the tests of net.c against local servers (Linux, needs openssl and python3).  Run from tests/net or anywhere.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); APP=$HERE/../../app
W=$(mktemp -d); trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$W"' EXIT
cd "$W"
mk_ca() { openssl req -x509 -newkey rsa:2048 -nodes -keyout $1.key -out $1.pem -days 2 -subj "/CN=$1" >/dev/null 2>&1; }
mk_cert() {	# mk_cert NAME CA SAN
  openssl req -newkey rsa:2048 -nodes -keyout $1.key -out $1.csr -subj "/CN=$1" >/dev/null 2>&1
  printf 'subjectAltName=%s\n' "$3" > $1.ext
  openssl x509 -req -in $1.csr -CA $2.pem -CAkey $2.key -CAcreateserial -out $1.pem -days 2 -extfile $1.ext >/dev/null 2>&1
}
mk_ca goodca; mk_ca otherca
mk_cert server goodca "DNS:localhost,IP:127.0.0.1"
mk_cert wrongname goodca "DNS:example.invalid"
CC=${CC:-cc}
$CC -std=gnu99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$APP" "$APP/net.c" "$HERE/../test_net.c" -lssl -lcrypto -o test_net || { echo "build failed"; exit 1; }
python3 -I "$HERE/tlsserver.py" 17001 server.pem server.key > /dev/null & 
python3 -I "$HERE/tlsserver.py" 17002 wrongname.pem wrongname.key > /dev/null &
python3 -I "$HERE/tlsserver.py" 17003 > /dev/null &
sleep 1
fails=0
t() { local what=$1; shift; printf '%-58s' "$what"; if out=$("$@" 2>/dev/null); then echo "$out"; else echo "$out"; fails=$((fails + 1)); fi; }
t "TLS, name and certificate right"                  ./test_net greet localhost 17001 1 goodca.pem 1
t "TLS, the server is given as an address (IP SAN)"  ./test_net greet 127.0.0.1 17001 1 goodca.pem 1
t "TLS, 500000 bytes in and 300000 out"             ./test_net bulk localhost 17001 1 goodca.pem 1
t "TLS, certificate from another CA is refused"      ./test_net fail localhost 17001 1 otherca.pem 1 "certificate"
t "TLS, certificate for another name is refused"     ./test_net fail localhost 17002 1 goodca.pem 1 "certificate"
t "TLS, no check when asked not to"                  ./test_net greet localhost 17001 1 otherca.pem 0
t "plain TCP"                                        ./test_net greet localhost 17003 0 "" 1
t "plain TCP, 500000 bytes in and 300000 out"        ./test_net bulk localhost 17003 0 "" 1
t "TLS to a server that speaks plain TCP"            ./test_net fail localhost 17003 1 goodca.pem 1 "TLS"
t "connection refused"                               ./test_net fail localhost 17009 0 "" 1 "connect"
t "a name that does not exist"                       ./test_net fail nonexistent.invalid 6697 1 goodca.pem 1 "find"
echo "failures: $fails"
exit $fails
