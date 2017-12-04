TODO: Add network stack documentation. :)

run ```./add_cert.sh``` to update your certificates

to copy over data to serve from the quic server, run the following commands
```
mkdir /tmp/quic-data
cp -r ~/www.example3.org/ /tmp/quic-data/
``` 

If you make any changes to the code, run the following command to recompile:
```ninja -C out/Debug quic_server quic_client```

To run the quic server:
```
./out/Debug/quic_server \
--quic_response_cache_dir=/tmp/quic-data/www.example3.org \
--certificate_file=net/tools/quic/certs/out/leaf_cert.pem \
--key_file=net/tools/quic/certs/out/leaf_cert.pkcs8
```

To request a given file ```myindex_fastMPC.html``` via a quic client on a different terminal:
```./out/Debug/quic_client --host=127.0.0.1 --port=6121 https://www.example3.org/myindex_fastMPC.html```

To load the same via Chrome, run:
```google-chrome --user-data-dir=/tmp/chrome-profile   --no-proxy-server   --enable-quic   \
--origin-to-force-quic-on=www.example3.org:443   --host-resolver-rules='MAP www.example3.org:443 52.91.174.8:6121' \
https://www.example3.org/myindex_fastMPC.html```

To play the actual video in the above scenario, also run an ABR server on port 8333 by running
```cd ~/video_transport_simulator/implementation/rl_server/
python mpc_server.py```

To just get logs for the requested video without viewing it, run:
```cd ~/video_transport_simulator/implementation/run_exp/
python run_video.py 172.31.5.129 fastMPC 10 2 blah 5``` 
which runs the video for 10 seconds

To get logs for a number of network traces, by running the quic server and a chrome client on different sides of a mahimahi link (not yet tested):
```python run_traces.py ../cooked_traces/ fastMPC 2 172.31.5.129```