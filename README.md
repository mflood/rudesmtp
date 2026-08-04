# RudeSMTP

RudeSMTP is a focused C++ client for sending messages through trusted SMTP
relays. Its API follows the SMTP conversation directly and gives callers the
server response information needed for delivery handling.

## Why RudeSMTP?

- Provides a compact API that mirrors the SMTP envelope and data exchange.
- Handles multiline server replies without losing protocol synchronization.
- Exposes numeric response codes for retry, rejection, and bounce decisions.
- Applies a configurable timeout to silent servers.
- Implements SMTP dot-stuffing and envelope address formatting.
- Reports transport and server errors through a single interface.

RudeSMTP 2.0 is designed for trusted relays that accept unauthenticated SMTP
over plain TCP, including local Postfix relays, development mail catchers, and
controlled private infrastructure.

## Quick start

```cpp
#include <rude/smtp.h>

#include <iostream>

int main()
{
    rude::SMTP smtp;
    smtp.setTimeout(30);

    if (!smtp.connect("localhost", 25) ||
        !smtp.sayHelo("myhost.example.com") ||
        !smtp.sayFrom("me@example.com") ||
        !smtp.addRecipient("you@example.com") ||
        !smtp.sendData("From: me@example.com\r\n"
                       "To: you@example.com\r\n"
                       "Subject: hello\r\n"
                       "\r\n"
                       "The body.\r\n")) {
        std::cerr << "SMTP error (" << smtp.getResponseCode() << "): "
                  << smtp.getError() << "\n";
        smtp.disconnect();
        return 1;
    }

    smtp.disconnect();
}
```

Compile a static installed copy with pkg-config:

```sh
c++ -std=c++17 app.cpp $(pkg-config --cflags --libs --static rudesmtp)
```

For a shared build, use `pkg-config --cflags --libs rudesmtp` instead.

## Build and install

RudeSMTP requires CMake 3.16 or newer, a C++17 compiler, and RudeSocket 1.7.1
or newer. If RudeSocket is not installed, CMake fetches and builds it.

```sh
git clone https://github.com/mflood/rudesmtp.git
cd rudesmtp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix ./install
```

The default build is static. Pass `-DBUILD_SHARED_LIBS=ON` to build shared
libraries.

For an install outside the system prefix, point CMake consumers at it with
`-DCMAKE_PREFIX_PATH=/path/to/install`. For pkg-config, add
`/path/to/install/lib/pkgconfig` to `PKG_CONFIG_PATH`.

The runnable example is in
[`examples/sendmail.cpp`](examples/sendmail.cpp).

## Use from CMake

With an installed copy:

```cmake
find_package(rudesmtp 2.0 REQUIRED)
target_link_libraries(myapp PRIVATE rudesmtp::rudesmtp)
```

Or include it directly with `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(rudesmtp
    GIT_REPOSITORY https://github.com/mflood/rudesmtp.git
    GIT_TAG v2.0.0)
FetchContent_MakeAvailable(rudesmtp)
target_link_libraries(myapp PRIVATE rudesmtp::rudesmtp)
```

## SMTP conversation

The calls map directly to the protocol:

| Call | Purpose |
|---|---|
| `connect(host, port)` | Open the connection and read the greeting |
| `sayHelo(hostname)` | Identify the client with `HELO` |
| `sayFrom(address)` | Set the `MAIL FROM` envelope address |
| `addRecipient(address)` | Add one `RCPT TO` recipient |
| `sendData(message)` | Send the complete message |
| `disconnect()` | Send `QUIT` and close the connection |
| `setTimeout(seconds)` | Set the server-response timeout |

`sendData()` accepts the complete RFC-style message: headers, a blank line,
and the body. Use CRLF line endings. RudeSMTP escapes leading dots and appends
the end-of-data marker, so callers should not add that marker themselves.

## Handling server responses

`getResponseCode()` returns the last three-digit SMTP reply code:

```cpp
if (!smtp.addRecipient(address)) {
    const int responseClass = smtp.getResponseCode() / 100;
    if (responseClass == 4) {
        requeue(message); // temporary failure
    } else if (responseClass == 5) {
        bounce(message);  // permanent failure
    }
}
```

Use `getResponse()` for the server's complete reply and `getError()` for a
diagnostic that includes transport failures and server-provided detail.

## Current transport scope

Version 2.0 supports SMTP using `HELO` over plain TCP. Deploy it with a trusted
relay that accepts unauthenticated clients from the application host or
private network. Do not send credentials or sensitive message content over an
untrusted network with this release.

## Documentation and support

- Public API: [`src/smtp.h`](src/smtp.h)
- Runnable example: [`examples/sendmail.cpp`](examples/sendmail.cpp)
- Release notes: [`NEWS`](NEWS)
- Bug reports: [GitHub Issues](https://github.com/mflood/rudesmtp/issues)

## License

GPL-2.0-or-later. See [`COPYING`](COPYING).
