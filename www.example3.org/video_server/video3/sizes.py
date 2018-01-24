HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/2.7.13
Date: Tue, 23 Jan 2018 23:27:38 GMT
Content-type: text/plain
Content-Length: 149
Last-Modified: Fri, 14 Jul 2017 19:28:36 GMT
X-Original-URL: https://www.example3.org/video_server/video3/sizes.py

import os, sys

chunks = os.listdir('.')
sizes = []

for i in range(1,98):
	size = os.path.getsize(str(i) + '.m4s')
	sizes.append(size)
print sizes

