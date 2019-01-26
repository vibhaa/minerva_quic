#!/bin/sh

# For mahimahi: set sudo ...net.sysctl = 1
echo "Configuring Mahimahi..."
sudo sysctl -w net.ipv4.ip_forward=1

# Remount efs
echo "Mounting NFS on ~/efs. This will take some time..."
sudo mount -t nfs -o nfsvers=4.1,rsize=1048576,wsize=1048576,hard,timeo=600,retrans=2 fs-042c304d.efs.us-east-1.amazonaws.com:/ ~/efs
sudo chown -R ubuntu ~/efs

echo "Creating important directories"
mkdir -p ~/value_func_cache
mkdir -p ~/value_funcs
mkdir -p /tmp/quic-data/www.example3.org

# Move two videos
echo "Copying QUIC server data from NFS"
if [ ! -d "~/all_quic_data" ];
then
    echo "Copying videos. This will take some more time..."
    cp -r ~/efs/all_quic_data ~/all_quic_data
fi

