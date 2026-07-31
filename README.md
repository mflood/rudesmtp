# rudesmtp

A small C++ client library for sending mail over SMTP.

First released in 2003 as part of the [RudeServer](https://github.com/mflood)
C++ library family; modernized in 2026 (CMake, C++17, RFC 5321 conformance
fixes, CI).

> **Read this before you use it.** This release speaks `HELO` and sends in the
> clear. There is **no authentication and no encryption**, so it can only talk
> to a server that accepts unauthenticated relay — in practice a local one.
> It cannot send through Gmail, Microsoft 365, SES, Mailgun or any other
> hosted provider. `EHLO`, `AUTH` and TLS are the next piece of work; see
> [Roadmap](#roadmap).

## Quick start

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
find_package(rudesmtp 2.0 REQUIRED)
target_link_libraries(myapp PRIVATE rudesmtp::rudesmtp)
```

Or without installing anything:

```cmake
include(FetchContent)
FetchContent_Declare(rudesmtp
    GIT_REPOSITORY https://github.com/mflood/rudesmtp.git
    GIT_TAG v2.0.0)
FetchContent_MakeAvailable(rudesmtp)
target_link_libraries(myapp PRIVATE rudesmtp::rudesmtp)
```

## The API

The calls are the SMTP conversation itself, in order:

| | |
|---|---|
| `connect(host, port)` | Opens the connection, reads the greeting |
| `sayHelo(hostname)` | `HELO` |
| `sayFrom(address)` | `MAIL FROM` — brackets added if you leave them off |
| `addRecipient(address)` | `RCPT TO` — call once per recipient |
| `sendData(message)` | The whole message: headers, blank line, body |
| `disconnect()` | `QUIT`, and closes the connection either way |
| `getResponseCode()` | The last reply's numeric code |
| `getResponse()` | The last reply's full text |
| `getError()` | What went wrong, including the server's own words |
| `setTimeout(seconds)` | How long to wait on a silent server (default 30) |

`sendData()` takes the complete message and generates none of it — no headers
are added for you. Use CRLF line endings. Lines beginning with `.` are escaped
as the protocol requires, and the end-of-data marker is appended, so do not add
it yourself.

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

## Roadmap

This library is only useful against a local relay until the following land:

1. **`EHLO` with capability parsing** — needed before anything else, and the
   reason multiline reply handling had to be fixed first.
2. **`AUTH PLAIN` / `AUTH LOGIN`** — required by every hosted provider.
3. **Implicit TLS on port 465** — rudesocket already has `connectSSL()`.
4. **`STARTTLS` on port 587** — rudesocket gained `startSSL()` in 1.7.0 for
   exactly this.

## Bug reports

https://github.com/mflood/rudesmtp/issues

## Copying

GPL-2.0-or-later. See `COPYING`.
