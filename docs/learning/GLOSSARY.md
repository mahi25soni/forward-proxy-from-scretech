# Glossary

Terms added as we encounter them. Keep each entry to 1–3 lines.

## absolute-form
Proxy request line uses full URL: `GET http://example.com/path HTTP/1.1`.

## origin-form
Direct/origin request line uses path only: `GET /path HTTP/1.1`. Proxy rewrites to this before talking to upstream.

## upstream fd
TCP socket from proxy to origin server. In M1, created per request inside `handle_client`, not registered with `select`.

## proxy-form
How clients talk to a forward proxy — absolute-form URL in the request line.
