# blog

The blog of Roundhouse (`fixtures/real-blog`, a Rails 8 scaffold of
articles and comments) with a console of Fluxpinel. Roundhouse lowers the
Rails app to Ruby that Spinel compiles. This example adds a console to that
output, so a client can connect to the running server over a socket:

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

## Changes to the output of Roundhouse

The output of Roundhouse runs the server at its top level. This example
changes three things by hand:

- `main.rb.patch` moves the start of the server into an entry,
  `Main.start`. Fluxpinel starts after the top level of the kernel, so the
  host build calls this entry. With `FLUXPINEL_CONSOLE=PORT`, `Main.start`
  also starts the console.
- `console.rb` accepts the connections of the console and gives each line
  to `Fluxpinel.feed`. It also has the entries `Console.count`,
  `Console.find` and `Console.create`.
- `kernel/fluxpinel.rb` of Fluxpinel goes beside `console.rb`.

## Versions

This example works with these versions:

- Roundhouse `2953060d9` (main of 2026-10-23). An older Roundhouse emits a
  `Preloader#initialize` that the Spinel of the pin refuses. The patch has
  no context lines, so it applies to the `main.rb` of this version only.
- Spinel at the commit in `SPINEL_COMMIT` of Fluxpinel.
- Rails 8.1.4 for the fixture.

## Build and run

You need Rust, Rails, Bundler, Node.js with npm, the SQLite CLI and
headers (`libsqlite3-dev`), and the jemalloc headers (`libjemalloc-dev`). In the steps, `FX` is the top
directory of Fluxpinel and `BLOG` is a new directory for the blog.

1. Make the fixture and the Spinel output with Roundhouse:

   ```sh
   git clone https://github.com/rubys/roundhouse && cd roundhouse
   git checkout 2953060d9
   cargo build --release --bin roundhouse
   bin/rh fixture "$BLOG-rails"
   target/release/roundhouse --target spinel -o "$BLOG" "$BLOG-rails"
   ```

2. Apply the changes:

   ```sh
   cd "$BLOG"
   patch -p1 < "$FX/examples/blog/main.rb.patch"
   cp "$FX/examples/blog/console.rb" "$FX/kernel/fluxpinel.rb" .
   ```

3. Build the assets: the JavaScript of Turbo and Stimulus from the gems,
   and the Tailwind CSS. Without them, the pages load, but the browser
   does not connect to Action Cable, and a new record does not show up
   on an open page:

   ```sh
   cd "$BLOG"
   bundle config set --local path vendor/bundle
   bundle install
   make assets
   ```

4. Build the host in the top directory of Fluxpinel. `KERNEL_FLAGS` gives
   spinel the flags that `spin flags` prints for the project, and
   `HOST_LIBS` the libraries of the link:

   ```sh
   cd "$FX"
   make host HOST=build/blog HOST_NAME=blog KERNEL="$BLOG/main.rb" KERNEL_MAIN=Main.start \
     KERNEL_ENTRIES=Main.start,Console.count,Console.find,Console.create \
     KERNEL_FLAGS="-I $BLOG --rbs $BLOG" HOST_LIBS="-lsqlite3 -ljemalloc"
   ```

5. Seed the database and run the server with the console:

   ```sh
   cd "$BLOG"
   sqlite3 storage/development.sqlite3 < db/seed.sql
   FLUXPINEL_CONSOLE=4000 "$FX/build/blog/blog"
   ```

   The server serves the blog on http://localhost:3000. Connect to the
   console with `nc 127.0.0.1 4000`.

   Open http://localhost:3000/articles/1, and type this in the console.
   The comment shows up on the open page with no reload:

   ```ruby
   Console.create("Comment", {article_id: 1, commenter: "fx", body: "live"})
   ```
