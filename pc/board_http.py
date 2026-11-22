"""Read-only, bounded SD downloads. Preserve bytes across chunk boundaries."""
import json
import http.client
import time
import urllib.parse
import urllib.request

OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def files(board):
    with OPENER.open(board.rstrip('/')+'/files', timeout=20) as response:
        return json.load(response)['files']

def read_file(board, name, size=None):
    if size is None:
        size = next(int(x['size']) for x in files(board) if x['name'] == name)
    chunks = []
    offset = 0
    while offset < size:
        count = min(8192, size-offset)
        url = board.rstrip('/')+'/full?'+urllib.parse.urlencode({'file':name,'from':offset,'len':count})
        for attempt in range(4):
            try:
                with OPENER.open(url, timeout=15) as response:
                    block = response.read()
                if len(block) != count:
                    raise IOError('Incomplete SD chunk at '+str(offset))
                break
            except (OSError, ValueError, http.client.HTTPException):
                if attempt == 3:
                    raise
                time.sleep(.5*(attempt+1))
        chunks.append(block)
        offset += count
    return b''.join(chunks)
