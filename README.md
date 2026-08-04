# rudesmtp

A small C++ client library for sending mail over SMTP.

First released in 2003 as part of the [RudeServer](https://github.com/mflood)
C++ library family; modernized in 2026 (CMake, C++17, RFC 5321 conformance
fixes, CI).

As of 2.1.0 this speaks `EHLO`, authenticates with `AUTH PLAIN`/`AUTH LOGIN`,
and encrypts with implicit TLS (port 465) or `STARTTLS` (port 587) — it can
send through Gmail, Microsoft 365, SES, Mailgun or any other hosted provider
that takes a username and password. Plain `HELO` over an unauthenticated
connection still works unchanged, for a local relay that does not need any of
that.

## Quick start

Against a local relay, unauthenticated:

```cpp
#include <rude/smtp.h>
#include <iostream>

int main()
{
    rude::SMTP smtp;

    if (!smtp.connect("localhost", 25)) {
        std::cerr << smtp.getError() << "\n";
        return 1;
    }

    smtp.sayHelo("myhost.example.com");
    smtp.sayFrom("me@example.com");
    smtp.addRecipient("you@example.com");

    if (!smtp.sendData("From: me@example.com\r\n"
                       "To: you@example.com\r\n"
                       "Subject: hello\r\n"
                       "\r\n"
                       "The body.\r\n")) {
        // 4xx is worth retrying later; 5xx is not.
        std::cerr << "failed (" << smtp.getResponseCode() << "): "
                  << smtp.getError() << "\n";
        smtp.disconnect();
        return 1;
    }

    smtp.disconnect();
}
```

Against a hosted provider, over `STARTTLS` on port 587:

```cpp
rude::SMTP smtp;
smtp.connect("smtp.example.com", 587);
smtp.sayEhlo("myhost.example.com");

if (!smtp.startTLS()) {
    std::cerr << smtp.getError() << "\n";
    return 1;
}
smtp.sayEhlo("myhost.example.com");   // required again -- see startTLS() below

if (!smtp.authenticate("me@example.com", "app-password")) {
    std::cerr << smtp.getError() << "\n";
    return 1;
}

smtp.sayFrom("me@example.com");
smtp.addRecipient("you@example.com");
smtp.sendData("From: me@example.com\r\nTo: you@example.com\r\n"
              "Subject: hello\r\n\r\nThe body.\r\n");
smtp.disconnect();
```

For implicit TLS on port 465, replace `connect()` + `startTLS()` with a single
`connectSSL()` — the connection is already encrypted, so one `sayEhlo()` is
enough.

Compile with:

```sh
c++ -std=c++17 app.cpp $(pkg-config --cflags --libs --static rudesmtp)
```

`--static` matters: rudesmtp builds a static library by default and rudesocket
is a private dependency, so a plain `pkg-config --libs` can leave the link
short. Drop it only if you built with `-DBUILD_SHARED_LIBS=ON`.

## Retry or bounce

`getResponseCode()` returns the server's three-digit reply code, which is what
distinguishes a failure worth retrying from one that never will be:

```cpp
if (!smtp.addRecipient(address)) {
    if (smtp.getResponseCode() / 100 == 4) {
        requeue(message);        // 4xx — temporary; the mailbox is busy,
                                 // the server is shutting down, greylisting
    } else {
        bounce(message);         // 5xx — permanent; no such user, refused
    }
}
```

Before 2.0.0 this was not possible: replies were judged by comparing the first
character to a digit, so `421 Service not available` and `550 No such user`
both produced a bare `false`.

## Building

Requires CMake 3.16+, a C++17 compiler, and
[rudesocket](https://github.com/mflood/rudesocket) 1.7.1 or newer. If
rudesocket is not installed, the build fetches and builds it automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

### Using it from CMake

```cmake
find_package(rudesmtp 2.1 REQUIRED)
target_link_libraries(myapp PRIVATE rudesmtp::rudesmtp)
```

Or without installing anything:

```cmake
include(FetchContent)
FetchContent_Declare(rudesmtp
    GIT_REPOSITORY https://github.com/mflood/rudesmtp.git
    GIT_TAG v2.1.0)
FetchContent_MakeAvailable(rudesmtp)
target_link_libraries(myapp PRIVATE rudesmtp::rudesmtp)
```

## The API

The calls are the SMTP conversation itself, in order:

| | |
|---|---|
| `connect(host, port)` | Opens the connection, reads the greeting |
| `connectSSL(host, port)` | Same, already wrapped in TLS — port 465 |
| `sayHelo(hostname)` | `HELO` — no extensions, no `startTLS()`/`authenticate()` after |
| `sayEhlo(hostname)` | `EHLO` — records what the server advertises |
| `startTLS()` | Upgrades an open connection to TLS (RFC 3207) — port 587 |
| `authenticate(user, password)` | `AUTH PLAIN`/`AUTH LOGIN` (RFC 4954) |
| `sayFrom(address)` | `MAIL FROM` — brackets added if you leave them off |
| `addRecipient(address)` | `RCPT TO` — call once per recipient |
| `sendData(message)` | The whole message: headers, blank line, body |
| `disconnect()` | `QUIT`, and closes the connection either way |
| `getResponseCode()` | The last reply's numeric code |
| `getResponse()` | The last reply's full text |
| `getError()` | What went wrong, including the server's own words |
| `setTimeout(seconds)` | How long to wait on a silent server (default 30) |
| `setSSLVerify(bool)` | Certificate verification for `connectSSL()`/`startTLS()` (on by default) |
| `allowPlaintextAuth(bool)` | Permit `authenticate()` without encryption (off by default) |
| `isSecure()` | True once the connection is carrying TLS |
| `supportsExtension(name)` / `supportsAuth(mechanism)` | What the last `sayEhlo()` advertised |

`sendData()` takes the complete message and generates none of it — no headers
are added for you. Use CRLF line endings. Lines beginning with `.` are escaped
as the protocol requires, and the end-of-data marker is appended, so do not add
it yourself.

## Encryption and authentication

`authenticate()` refuses to run on a connection that is not encrypted, unless
`allowPlaintextAuth(true)` has been called first. `AUTH PLAIN` and `AUTH LOGIN`
both put the password on the wire as base64 — an encoding, not encryption — so
sending it in the clear means anyone who can see the connection has it. This
is a refusal to send, not "ask and let the server reject it": nothing
resembling `AUTH` reaches the wire when the connection is unencrypted.

`startTLS()` clears everything the prior `sayEhlo()` learned. RFC 3207 section
4.2 requires this: that exchange happened before encryption started, so a
network attacker could have altered it — hiding `AUTH` to force a weaker
mechanism, for instance. Call `sayEhlo()` again after `startTLS()` succeeds;
`authenticate()` and `supportsExtension()`/`supportsAuth()` will not see
anything from before the upgrade.

Not in this release: SASL mechanisms beyond `PLAIN` and `LOGIN` — no
`CRAM-MD5`, no `XOAUTH2` (needed by Google Workspace accounts with Basic Auth
disabled, i.e. most of them; an app password sidesteps this and works with
`PLAIN`/`LOGIN`). Also no command pipelining and no `SIZE`-based rejection
before sending — `sendData()` finds out the same way a `HELO`-only client
always has, from the reply.

## What changed in 2.0.0

The wire behaviour was wrong in several ways that only show against a server
enforcing the grammar, which is why they survived: the library was only ever
pointed at lenient ones.

- **Multiline replies desynchronized the session.** Only the first line of a
  reply was read, so continuation lines were consumed as the reply to the
  *next* command, putting every later exchange one behind.
- **A message beginning with `.` lost that character.** Dot-stuffing looked at
  the preceding character, and the first line of a message has none.
- **Every message gained a trailing blank line**, because the end-of-data
  marker was sent as `\r\n.\r\n` unconditionally.
- **`MAIL FROM: <addr>`** carried a space the grammar does not have, and angle
  brackets were the caller's problem.
- **No timeout was ever set**, so a server that accepted the connection and
  then went quiet hung the calling thread with no way for the caller to
  intervene.
- **`SMTP` was copyable while owning a socket**, so a copy double-freed it.
- **Failed writes were ignored** by `addRecipient`, `sendData` and
  `disconnect`, which then waited for a reply that could not be coming.

See `NEWS` for the full list.

## What's new in 2.1.0

Everything above the `HELO`-only path: `sayEhlo()` with capability parsing,
`authenticate()` (`AUTH PLAIN`/`AUTH LOGIN`), `connectSSL()` (implicit TLS,
port 465) and `startTLS()` (RFC 3207, port 587). Nothing from 2.0.0's API
changed — this release is additive. See [Encryption and
authentication](#encryption-and-authentication) above and `NEWS` for the
details, including why `startTLS()` requires a second `sayEhlo()`.

## Bug reports

https://github.com/mflood/rudesmtp/issues

## Copying

GPL-2.0-or-later. See `COPYING`.
