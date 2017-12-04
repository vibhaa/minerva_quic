#!/bin/bash 
cd tools/quic/certs
./generate-certs.sh
cd -

certutil -d sql:$HOME/.pki/nssdb -D -n example3rootquic
certutil -d sql:$HOME/.pki/nssdb -A -t "C,," -n example3rootquic -i tools/quic/certs/out/2048-sha256-root.pem