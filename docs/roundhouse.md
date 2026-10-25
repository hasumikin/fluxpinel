# Roundhouse

Roundhouse lowers a Rails app to Ruby that Spinel compiles. Fluxpinel
adds a console to the blog of Roundhouse (`fixtures/real-blog`, a Rails 8
scaffold of articles and comments). A client connects to the running
server over a socket:

```
$ nc 127.0.0.1 4000
fx> Console.count("Article")
=> 3
fx> a = Console.find("Article", 1)
fx> a.instance_variable_get(:@title)
=> "Getting Started with Rails"
fx> c = Console.create("Comment", {article_id: 1, commenter: "fx", body: "from the console"})
fx> Console.count("Comment")
=> 4
fx> exit
```

The server serves its pages all the time. `exit` closes the session only.

## The files

| File | What it does |
|---|---|
| `ROUNDHOUSE_COMMIT` | holds the pin: the Roundhouse commit that Fluxpinel uses |
| `roundhouse/roundhouse.mk` | holds the make targets of this document |
| `roundhouse/apply.rb` | changes the Spinel output of the blog |
| `roundhouse/console.rb` | is the console and its entries |
| `test/roundhouse/test.rb` | runs the host build and checks it |

## Changes to the output of Roundhouse

The output of Roundhouse runs the server at its top level. `apply.rb`
changes three things:

- It moves the start of the server in `main.rb` into an entry,
  `Main.start`. Fluxpinel starts after the top level of the kernel, so the
  host build calls this entry. With `FLUXPINEL_CONSOLE=PORT`, `Main.start`
  also starts the console.
- It copies `console.rb`. This file accepts the connections of the console
  and gives each line to `Fluxpinel.feed`. It also has the entries
  `Console.count`, `Console.find` and `Console.create`.
- It copies `kernel/fluxpinel.rb` of Fluxpinel beside `console.rb`.

Each change of `main.rb` finds an anchor line. When an anchor is not in
`main.rb` exactly once, `apply.rb` stops and changes nothing.

## Build

You need these tools:

- Rust
- Rails 8.1.4, Bundler, Node.js with npm
- the SQLite CLI and headers (`libsqlite3-dev`)
- the jemalloc headers (`libjemalloc-dev`)

Run `make roundhouse` in the top directory of Fluxpinel. It does these
steps:

1. It fetches Roundhouse at the pin into `deps/roundhouse` and builds it.
2. It makes the Rails fixture with `bin/rh fixture` of Roundhouse.
3. It makes the Spinel output of the blog in `build/roundhouse/blog` and
   runs `apply.rb` on it.
4. It builds the assets of the blog: the JavaScript of Turbo and
   Stimulus, and the Tailwind CSS. Without them, the browser does not
   connect to Action Cable.
5. It builds the host in `build/roundhouse/host/blog`.

Each step runs again only when its input changes. A new pin starts again
from step 1. A change of `apply.rb`, `console.rb` or `kernel/fluxpinel.rb`
starts again from step 3.

rh and Bundler use the Ruby of `.ruby-version` of Fluxpinel. Rails must be
installed for that Ruby.

## Run

Seed the database and run the server with the console:

```sh
cd build/roundhouse/blog
sqlite3 storage/development.sqlite3 < db/seed.sql
FLUXPINEL_CONSOLE=4000 ../host/blog
```

The server serves the blog on http://localhost:3000. Connect to the
console with `nc 127.0.0.1 4000`.

Open http://localhost:3000/articles/1, and type this in the console. The
comment shows up on the open page with no reload:

```ruby
Console.create("Comment", {article_id: 1, commenter: "fx", body: "live"})
```

## Check

`make test-roundhouse` runs `test/roundhouse/test.rb` on the host build.
The test starts the server on free ports. It uses a copy of `db/seed.sql`
in a temporary database, so `storage/` of the blog does not change. It
checks these things:

- `GET /articles` answers 200 with an article of the seed.
- The console counts, finds and creates records, and keeps a local.
- `exit` closes the session, and the server runs after it.
- The page of the article shows the new comment.

## Follow the main of Roundhouse

`make upgrade_roundhouse` moves the pin to the main of Roundhouse.

1. It stops when `deps/roundhouse` has changes.
2. It fetches the branch main and writes its commit to
   `ROUNDHOUSE_COMMIT`.
3. It runs `make roundhouse` and `make test-roundhouse`.

The command does not commit. When it succeeds, commit `ROUNDHOUSE_COMMIT`.
When it fails, the message gives the old pin.

The main of Roundhouse follows the master of spinel. When spinel refuses
the `main.rb` of a new Roundhouse, move the pin of spinel first: run
`make upgrade_spinel`, then `make upgrade_roundhouse`.
