# Development

This document gives the work flow of Fluxpinel development. For the build
of Fluxpinel alone, see the README. For the design, see
[design.md](design.md).

## The spinel checkout

Fluxpinel builds against one spinel checkout. `SPINEL` names it.

- `make deps` fetches spinel into `deps/spinel` and builds it there.
  `SPINEL` names this checkout by default.
- To work on spinel too, use a checkout of your own. Set `SPINEL` in
  `config.mk`. Git does not track `config.mk`.

```make
SPINEL = $(HOME)/work/spinel
```

`SPINEL_COMMIT` holds the pin: the spinel commit that Fluxpinel uses. The
checkout must be at the pin, or `make` stops. Pass `SPINEL_PIN=0` to use
another commit for one run.

`--irb-port` needs a Reline that runs on a socket. Set `GEM_DIRS_FIRST` in
`config.mk` to the lib directory of that Reline. Without it, `make test`
skips the case of `--irb-port` and its last line counts the skip.

```make
GEM_DIRS_FIRST = $(HOME)/work/ruby/reline/lib
```

The runtime API of spinel is internal and changes often. The pin makes each
update of spinel an explicit step.

## The gems

The build and the tests read some gems of the Ruby that runs make. The
`Gemfile` names them.

- `--irb` runs the gems `irb`, `reline`, `prism` and `rdoc` under
  Fluxpinel. The build records their lib directories
  (`tools/gem_dirs.rb`), and every program has them at the end of
  `$LOAD_PATH`.
- `--irb` also loads `did_you_mean`, for the `Did you mean?` line of an
  error. CRuby has it as a default gem, with its files in the directory
  of the standard library. `make gems` installs it in a directory of its
  own. Without it, `--irb` starts and an error has no such line.
- `test/m3/ffi_gem.rb` compares with the `ffi` gem under CRuby.

Run `make gems` once, and again after a change of the Ruby. It runs
`bundle install`. Then it builds the shared library of the prism gem,
which IRB loads through the ffi backend of prism. The library is
`build/libprism.so` in the directory of the gem (`build/libprism.dylib`
on macOS). The prism gem of a platform (`arm64-darwin`,
`x86_64-linux-gnu`) has the sources too, so `make gems` builds the
library there with no other step.

Fluxpinel does not run under Bundler. Make and the tests call `ruby` with
no `bundle exec`, and the build takes the newest version of each gem. Git
does not track `Gemfile.lock`.

When a gem of `--irb` is missing or too old, each `make` prints a warning
that names the gem. The build goes on, and `--irb` of that build does not
start.

## The checks

| Command | What it does | Time |
|---|---|---|
| `make test` | runs `test/m2` and `test/m3` under CRuby and Fluxpinel, and `test/console` through the console of both, plain and under GC stress, then the cases of the command line in `test/cli/cases.rb` and the sessions of IRB in `test/irb`, and IRB on a pseudo terminal (`test/irb_tty.rb`) | about 2 minutes |
| `make test-host` | builds the host with `test/kernel/app.rb`, then runs the same tests and `test/host` on it; then builds a host with `test/kernel/bare.rb`, a kernel with no entry, and runs `test/host_bare` | about 2 minutes from a clean build |
| `make regress` | runs the spinel tests in `tools/oracle_agree.txt` under Fluxpinel | about 10 seconds |
| `make test-valgrind` | runs one session of IRB (`VALGRIND_SESSION`) under valgrind and fails on a read or a write outside a block | about 30 seconds |
| `make oracle` | runs all the spinel tests under CRuby, Fluxpinel and AOT | more than 10 minutes |
| `make optcarrot` | runs optcarrot under CRuby and Fluxpinel and compares the checksums | about 25 seconds for 180 frames |

Run `make test` and `make regress` after each change. Run `make oracle`
before a change of the pin and after a large change of the walker.

`make regress` and `make oracle` need Fluxpinel in the promote mode,
which is the default (`INT_OVERFLOW`). They stop on a build of another
mode.

### CI

`.github/workflows/ci.yml` has three jobs.

- `check` runs on a push and a pull request to master, on Linux and on
  macOS. It runs `make test`, `make test-host` and `make regress` on the
  pinned spinel. On Linux, it also runs `make test-valgrind`.
- `spinel-master` runs each week and on a manual start. It runs the same
  checks on the master of spinel with `SPINEL_PIN=0`. A failure tells that
  a change of spinel breaks Fluxpinel before the pin moves.
- `roundhouse` runs each week and on a manual start, on the pinned spinel.
  It runs `make roundhouse` and `make test-roundhouse` on the pinned
  Roundhouse, then `make upgrade_roundhouse` on the main of Roundhouse.
  A failure in the first two steps comes from Fluxpinel. A failure in the
  last step tells that a change of Roundhouse breaks the blog.

### make regress

`tools/oracle_agree.txt` names the spinel tests that Fluxpinel answers
right. `make regress` runs each of them under Fluxpinel and compares stdout
with the `.expected` file of the test. It does not compile the tests with
spinel, so it is fast.

- A test that no longer gives the CRuby answer makes the run fail. The run
  prints the name and the first line that differs.
- The outputs of a failed test go to `build/regress/cases/<test>/`.
- A listed test that is no longer in the spinel corpus gives a warning
  only.
- The run collects the shadows: a call that a hand-written method answered
  while a generated wrapper fits it (`FX_TRACE_SHADOW=1`, see
  `docs/design.md`, section 3.2.2). `tools/builtin_census.rb` then prints
  the counts of the builtin layer and fails on a shadow that
  `tools/shadowed_known.txt` does not list. A listed shadow with a reason
  stays until spinel gives what the reason names; one with no reason is a
  method to remove. `make census` prints the counts of the last run.

### make oracle

`make oracle` writes `build/oracle/report.md`. The report gives the count
of each class and the reasons of the gaps. See design.md, section 4.

The oracle also writes `tools/oracle_agree.txt` again. Commit this file
together with the change that moves its contents.

`make oracle` runs `make regress` last, on the new list. The census then
reads the tests that the list gained.

`tools/oracle_known.tsv` lists the known defects of spinel that the oracle
finds. The file itself tells when a line can go.

The spinel tests can leave scratch files in the spinel tree when Fluxpinel
stops them early. The oracle removes the files that a run adds.

### make optcarrot

`make optcarrot` fetches [optcarrot](https://github.com/mame/optcarrot) at
the commit in `OPTCARROT_COMMIT` into `deps/optcarrot`. The commit is on
the branch that spinel tests. `tools/optcarrot.rb` then runs
`bin/optcarrot -b` under CRuby and under Fluxpinel.

- The checksum of Fluxpinel must be the checksum of CRuby, or the run
  fails.
- The run prints the fps of each side.
- Pass `OPTCARROT_FRAMES=10` for a short run. A run of 10 frames prints
  no fps.

## Follow the master of spinel

`make upgrade_spinel` moves the pin to the master of spinel.

1. It stops when the spinel checkout has changes, untracked files
   included.
2. It fetches the branch master from `SPINEL_URL`.
3. In `deps/spinel`, it checks out that commit. In a checkout of your own,
   it moves the branch master forward. It stops on any other branch and
   when master cannot move forward.
4. It builds spinel.
5. It writes the new commit to `SPINEL_COMMIT`.
6. It runs these checks:
   - `make`
   - `make test`
   - `make test-host`
   - `make regress`

The command does not commit. When it succeeds, do these steps:

1. Run `make oracle`.
2. Compare the report with the last one. A test that agreed before must
   still agree.
3. Commit `SPINEL_COMMIT` and `tools/oracle_agree.txt`. Give the oracle
   counts in the commit message.

A change of the pin is a commit of its own. When the new spinel needs a
change of Fluxpinel, put that change in the same commit.

When a check fails, the checkout and `SPINEL_COMMIT` stay at the new
commit. The message gives the old pin. Fix Fluxpinel there, or go back:

```sh
git checkout SPINEL_COMMIT
git -C $SPINEL checkout --detach <the old pin>
make -C $SPINEL
```

Before the next `make upgrade_spinel`, put your checkout on the branch
master again: `git -C $SPINEL switch master`.

### Each night

The master of spinel moves by about a hundred commits a day. Run
`make check-spinel-master` each night (a cron line or a systemd timer) to
know what the next pin move meets, and which commit of spinel brought it.

1. It clones spinel into `deps/spinel-master` the first time, with its
   history, and fetches master into it. The pinned checkout stays as it
   is.
2. It builds spinel there, then Fluxpinel on it with `SPINEL_PIN=0`
   under `build/master/fx`, from nothing.
3. It runs `make all`, `make test`, `make test-host` and `make regress`.
4. It appends one line to `SPINEL_MASTER_REPORT` (`build/master/report.md`
   by default): the date, the commit of spinel, and green, or the target
   that failed. The commit of the last green run stays in
   `build/master/good`.
5. When a target fails, it bisects spinel with `git bisect run`, and the
   line names the first bad commit. The bisect starts at the last green
   commit or at the pin (`SPINEL_COMMIT`), the nearer ancestor of master.
   Each step builds spinel and Fluxpinel again, so the bisect of a day of
   commits takes about half an hour.

Read the line in the morning. A removed or renamed function of the
runtime is a change of Fluxpinel; a table or a thunk of `--ext-class` that
does not answer as `docs/spin.md` of spinel says is an issue of spinel.
Move the pin on a green day.

## Follow the main of Roundhouse

`ROUNDHOUSE_COMMIT` pins Roundhouse for the blog with a console (see
[design.md](design.md), section 5.4). Run `make roundhouse` and
`make test-roundhouse` after a change of the pin of spinel.

Campfire is a second application on the same pins (design.md, section
5.4.7). After a change of the pin of spinel or of Roundhouse, run
`make campfire` and `make test-campfire` also. The build takes about 35
minutes, and no workflow runs it.

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

## The host build

`make host` builds a kernel and Fluxpinel into one binary. Give the kernel,
its entries and a name for the build directory.

```sh
make host HOST=build/logq HOST_NAME=logq KERNEL=examples/logq/app.rb \
  KERNEL_MAIN=Logq.main \
  KERNEL_ENTRIES=Logq.main,Logq.all,Logq.where,Logq.slower,Logq.where_block,Logq.top,Logq.count_by
```

`make host` makes the kernel C again after a change of these variables or
of a file that the kernel requires. A change of `KERNEL_MAIN` or
`HOST_LIBS` makes only the link again.

A change of spinel can break the host build and leave `make test` green.
`make test-host` builds a host, so `make upgrade_spinel` finds such a
break. The examples use other kernels: after a change of the pin, build
them too.
