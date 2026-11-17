# web-Cerver

A tiny static web server written in C. It serves files from a folder over HTTP on port **8000**, using only the C standard library and POSIX sockets, with no third-party dependencies. :3

## Project structure

```
web-Cerver/
├── README.md
├── server.c
└── test.html
```

| File | Description |
|------|-------------|
| `server.c` | The web server source code |
| `test.html` | A sample page to check that the server works |
| `README.md` | This file |

## Features

- Serves static files (HTML, CSS, JS, images, PDFs, ...) with the correct `Content-Type`
- Serves `index.html` when a folder is requested
- Supports `GET` and `HEAD` requests
- Returns `404` for missing files, `403` for blocked paths, `405` for unsupported methods
- Blocks path traversal (`..`) so requests can't escape the served folder
- One process per connection (`fork`), so idle browser sockets don't block other requests

## Requirements

- A C compiler (`gcc` or `clang`)
- Linux or macOS (uses POSIX sockets;)
- No windows (u can use wsl or smt)

## Build

```bash
git clone https://github.com/theR00Tuz3r/web-Cerver.git
cd web-Cerver
gcc -Wall -Wextra -o server server.c
```

## Run

```bash
./server              # serves the current folder
./server ./folder     # serves a different folder
```

Then open the test page in your browser:

http://localhost:8000/test.html

Or use `curl`:

```bash
curl -i http://localhost:8000/test.html
```

Stop the server with `Ctrl+C`.

## Notes

This is cetenly a learning project. It has no HTTPS, no keep-alive, and only minimal request parsing, so **don't expose it to the internet**.

## License

This project is licensed under the **MIT License**.