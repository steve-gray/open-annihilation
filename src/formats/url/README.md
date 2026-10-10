# Web addresses

The strict reader for the web addresses a registry and a catalogue name.
Target `oa-formats-url`, header `oa/formats/url.hpp`, namespace
`oa::formats::url`. The HTTP client, the registry rules and the catalogue
all read an address here, so they agree on what an address is and which
host it names.

`parse_web_url` reads an absolute `http` or `https` address. `parse_http_url`
is the same reader for a fetch, and refuses `https`: OA fetches over plain
HTTP and checks what it gets with signatures. `https` is only a page the
system's browser opens. `url_text` writes the address back in one form.
`resolve_reference` resolves a reference against a base the way RFC 3986
section 5.2 does, then applies the same rules to the result. `host_within`
tells whether a host is a domain or lies under it, and `same_server` tells
whether two addresses name the same scheme, host and port.

## Entry points

- `parse_web_url` and `parse_http_url` read one address and, on a refusal,
  set a `UrlError`. `url_error_text` describes that refusal.
- `url_text` writes `<scheme>://<host>[:<port>]<target>`, leaving out the
  scheme's default port. `host_header` is the host and, when the port is
  not that default, the port. `has_query` reports a query on the target.
- `resolve_reference` resolves a reference. A reference with its own scheme
  is read whole; `//` takes a new authority; `/` a new path; `?` a new
  query; anything else merges with the base path. A fragment is dropped.
- `is_ipv4_literal` reports four plain decimal numbers. `host_within` and
  `same_server` compare hosts after that canonical form.

## State

None. Each call reads its arguments and returns a value.

## Invariants

- One canonical form. The host is lower case with one trailing dot removed,
  the path is `/` when the address gave none, `.` and `..` segments are
  removed, a fragment is dropped, and a percent-escape is kept as written.
- An IPv4 host is four decimal numbers from 0 to 255 with no leading zeros.
  Any other spelling, including a host whose last label is all digits but
  which is not those four numbers, is refused. A host is made canonical
  before it is compared.
- Nothing but `http` and `https`. Any other scheme is refused, and a fetch
  refuses `https`.

## Tests

`formats-url` checks the addresses the reader accepts, with their parts and
canonical text; each refusal, with at least two texts; `parse_http_url`
refusing `https`; every normal example of RFC 3986 section 5.4.1 except
`g:h`, which is an unknown scheme; the abnormal examples of section 5.4.2
whose result stays an http address; `host_within`; and `host_header` with
and without a port.

## Limitations

No IPv6 literals, no international names, and no user info. An address that
uses any of those is refused. Links opened in the system's browser are
checked by the application, not here.
