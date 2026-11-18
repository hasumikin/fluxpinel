# Contributing

Fluxpinel takes pull requests. This document gives the rules for one. For
the build, see the [README](README.md). For the work flow and the checks,
see [docs/development.md](docs/development.md). For the design, see
[docs/design.md](docs/design.md).

## Before a pull request

Run these checks on your change:

```sh
make test
make test-host
make regress
```

The CI runs the same checks on Linux and on macOS, on each pull request.

## Tests

A change of what a Ruby program gets comes with a test.

- A test is a Ruby program in `test/m3`. `make test` runs it under CRuby
  4.0 and under Fluxpinel, and compares stdout and the exit status. A test
  has no file of expected output.
- A test of the host build goes in `test/host`. It can call the kernel of
  `test/kernel/app.rb`.
- Add the cases to the file of the same topic when one exists.
- Write a test that gives the same output on Linux and on macOS.
- When Fluxpinel differs from CRuby on purpose, say so in the pull
  request.

## Scope

- Keep one topic in one pull request. A small pull request is fast to
  review and seldom conflicts with another one.
- Do not change `SPINEL_COMMIT`, `ROUNDHOUSE_COMMIT` or
  `tools/oracle_agree.txt`. The maintainer moves the pins and writes the
  list again (`docs/development.md`).
- Fluxpinel does not modify spinel. Report a defect of spinel to
  [matz/spinel](https://github.com/matz/spinel).
- A builtin method comes from the runtime of spinel when spinel has it. A
  method that Fluxpinel writes by hand needs a reason (`docs/design.md`,
  section 3.2.2). `make regress` fails when such a method answers a call
  that a generated wrapper fits.

## Style

- Follow the code around your change.
- Write comments, commit messages and documents in English, in ASCII.
- A comment tells why the code is as it is. It does not tell the history
  of the code.
- Leave no whitespace at the end of a line.
- A document describes the present state. The history of a change goes in
  the commit message.
- Write documents in short sentences, in the active voice and in the
  present tense.

## Commit messages

- The subject is a sentence that states what Fluxpinel does after the
  change: "File.identical? takes an open File by fstat of its descriptor".
- The body states what was wrong and how the change corrects it.
