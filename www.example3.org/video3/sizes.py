HTTP/1.0 200 OK
Server: cloudflare-quic
Date: Wed, 08 Nov 2017 21:19:58 GMT
Content-type: text/plain
Content-Length: 149
X-Original-Url: http://www.example3.org/video3/sizes.py

import os, sys

chunks = os.listdir('.')
sizes = []

for i in range(1,98):
	size = os.path.getsize(str(i) + '.m4s')
	sizes.append(size)
print sizes

