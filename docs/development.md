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

The runtime API of spinel is internal and changes often. The pin makes each
update of spinel an explicit step.

## The checks

| Command | What it does | Time |
|---|---|---|
| `make test` | runs `test/m2` and `test/m3` under CRuby and Fluxpinel, and `test/console` through the console of both, plain and under GC stress | seconds |
| `make test-host` | builds the host with `test/kernel/app.rb`, then runs the same tests and `test/host` on it | about 90 seconds from a clean build |
| `make regress` | runs the spinel tests in `tools/oracle_agree.txt` under Fluxpinel | about 10 seconds |
| `make oracle` | runs all the spinel tests under CRuby, Fluxpinel and AOT | more than 10 minutes |

Run `make test` and `make regress` after each change. Run `make oracle`
before a change of the pin and after a large change of the walker.

`make regress` and `make oracle` use the promote build in
`build/promote`. They build it when it is out of date.

### CI

`.github/workflows/ci.yml` has three jobs.

- `check` runs on a push and a pull request to master. It runs
  `make test`, `make test-host` and `make regress` on the pinned spinel.
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

### make oracle

`make oracle` writes `build/oracle/report.md`. The report gives the count
of each class and the reasons of the gaps. See design.md, section 4.

The oracle also writes `tools/oracle_agree.txt` again. Commit this file
together with the change that moves its contents.

`tools/oracle_known.tsv` lists the known defects of spinel that the oracle
finds. The file itself tells when a line can go.

The spinel tests can leave scratch files in the spinel tree when Fluxpinel
stops them early. The oracle removes the files that a run adds.

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

## Follow the main of Roundhouse

`ROUNDHOUSE_COMMIT` pins Roundhouse for the blog with a console. Run
`make roundhouse` and `make test-roundhouse` after a change of the pin of
spinel. `make upgrade_roundhouse` moves the pin of Roundhouse. Move the pin
of spinel first. See [roundhouse.md](roundhouse.md).

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
