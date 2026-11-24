# The blog of Roundhouse with a console of Fluxpinel (docs/design.md,
# section 5.4).
# The Makefile includes this file.
#
#   make roundhouse
#               fetch Roundhouse at the commit in ROUNDHOUSE_COMMIT into
#               deps/roundhouse, make the blog with it and build its host
#   make test-roundhouse
#               run the host build of the blog and check it
#   make upgrade_roundhouse
#               move the pin to the main of Roundhouse, then make the blog
#               and run test-roundhouse
#
# The steps need Rust, Rails, Bundler, Node.js with npm, the SQLite CLI and
# headers, and the jemalloc headers. rh and Bundler use the Ruby of this
# tree (.ruby-version).

DEPS_ROUNDHOUSE = $(CURDIR)/deps/roundhouse
ROUNDHOUSE_URL ?= https://github.com/rubys/roundhouse.git
ROUNDHOUSE_COMMIT := $(shell cat ROUNDHOUSE_COMMIT)
# RH_BLOG is the Spinel output of the blog, RH_RAILS the Rails fixture.
RH_BLOG ?= $(BUILD)/roundhouse/blog
RH_BLOG_DIR = $(abspath $(RH_BLOG))
RH_RAILS = $(RH_BLOG_DIR)-rails
RH_HOST_DIR = $(BUILD)/roundhouse/host
RH_HOST ?= $(RH_HOST_DIR)/blog
RH_ENTRIES = Main.start,Console.count,Console.find,Console.create
# The host build of an application, from its Spinel output after apply.rb:
#
#   $(call rh_host,NAME,OUT,HOST,FLAGS,LIBS)
#
# NAME is the name of the binary, OUT the output directory (an absolute
# path) and HOST the directory of the host build. FLAGS are more flags of
# spinel (the -I of a package) and LIBS more objects and libraries of the
# link. The classes of --ext-class come from OUT/.fx-ext-class, which
# apply.rb writes: the models, and the classes of Active Record whose
# objects their methods answer. `all` roots every public method of the
# application, and spinel then refuses runtime/logger_spinel.rb (a Proc
# read from a slot that holds other values appends to a String).
define rh_host
$(MAKE) host HOST=$(3) HOST_NAME=$(1) KERNEL=$(2)/main.rb KERNEL_MAIN=Main.start \
  KERNEL_ENTRIES=$(RH_ENTRIES) \
  KERNEL_FLAGS="$(strip -I $(2) --rbs $(2) $(4))" \
  KERNEL_EXT_CLASS="$$(cat $(2)/.fx-ext-class)" HOST_LIBS="$(strip $(5) -lsqlite3 -ljemalloc)"
endef
# The interpreter itself, not a shim of a version manager. A shim of rbenv
# picks the Ruby from the directory of the script, and deps/roundhouse
# names a Ruby of its own.
RH_RUBY = $(shell ruby -e 'print RbConfig.ruby')

# Roundhouse at the pin. A new pin fetches and builds it again.
$(BUILD)/roundhouse/roundhouse.stamp: ROUNDHOUSE_COMMIT
	if [ ! -d $(DEPS_ROUNDHOUSE)/.git ]; then \
	  git init -q $(DEPS_ROUNDHOUSE) && git -C $(DEPS_ROUNDHOUSE) remote add origin $(ROUNDHOUSE_URL); fi
	if [ "$$(git -C $(DEPS_ROUNDHOUSE) rev-parse -q --verify HEAD)" != "$(ROUNDHOUSE_COMMIT)" ]; then \
	  git -C $(DEPS_ROUNDHOUSE) fetch -q --depth 1 origin $(ROUNDHOUSE_COMMIT) && \
	  git -C $(DEPS_ROUNDHOUSE) checkout -q --detach FETCH_HEAD; fi
	cd $(DEPS_ROUNDHOUSE) && cargo build --release --bin roundhouse
	@mkdir -p $(dir $@)
	touch $@

# The Rails fixture comes from a script of Roundhouse.
$(RH_RAILS)/.fx-stamp: $(BUILD)/roundhouse/roundhouse.stamp
	rm -rf $(RH_RAILS)
	$(RH_RUBY) $(DEPS_ROUNDHOUSE)/bin/rh fixture $(RH_RAILS)
	touch $@

# The Spinel output, with the changes of apply.rb and the assets. The make
# of the blog gets no variables of this make.
$(RH_BLOG_DIR)/.fx-stamp: $(RH_RAILS)/.fx-stamp roundhouse/apply.rb roundhouse/console.rb \
                          kernel/fluxpinel.rb
	rm -rf $(RH_BLOG_DIR)
	$(DEPS_ROUNDHOUSE)/target/release/roundhouse --target spinel -o $(RH_BLOG_DIR) $(RH_RAILS)
	ruby roundhouse/apply.rb $(RH_BLOG_DIR)
	cd $(RH_BLOG_DIR) && bundle config set --local path vendor/bundle && bundle install
	cd $(RH_BLOG_DIR) && MAKEFLAGS= MAKELEVEL= make assets
	touch $@

roundhouse: $(RH_BLOG_DIR)/.fx-stamp
	$(call rh_host,blog,$(RH_BLOG_DIR),$(RH_HOST_DIR),,)

test-roundhouse:
	ruby test/roundhouse/test.rb $(RH_BLOG) $(RH_HOST)

# Moves the pin to the main of Roundhouse and checks the blog on it. It
# stops when deps/roundhouse has changes. It does not commit: commit
# ROUNDHOUSE_COMMIT when the check passes.
#
# The checkout follows the fetch here, so that the stamp rule does not
# fetch again: a fetch leaves a gc in the background, which rewrites
# .git/shallow under a second fetch ("shallow file has changed").
upgrade_roundhouse:
	@if [ -d $(DEPS_ROUNDHOUSE)/.git ] && [ -n "$$(git -C $(DEPS_ROUNDHOUSE) status --porcelain)" ]; then \
	  echo "upgrade_roundhouse: $(DEPS_ROUNDHOUSE) has changes" >&2; \
	  git -C $(DEPS_ROUNDHOUSE) status --short >&2; exit 1; fi
	if [ ! -d $(DEPS_ROUNDHOUSE)/.git ]; then \
	  git init -q $(DEPS_ROUNDHOUSE) && git -C $(DEPS_ROUNDHOUSE) remote add origin $(ROUNDHOUSE_URL); fi
	git -C $(DEPS_ROUNDHOUSE) fetch -q --depth 1 $(ROUNDHOUSE_URL) main
	git -C $(DEPS_ROUNDHOUSE) checkout -q --detach FETCH_HEAD
	git -C $(DEPS_ROUNDHOUSE) rev-parse HEAD > ROUNDHOUSE_COMMIT
	@$(MAKE) roundhouse && $(MAKE) test-roundhouse || { \
	  echo "upgrade_roundhouse: the check failed on $$(cat ROUNDHOUSE_COMMIT)." \
	    "The pin was $(ROUNDHOUSE_COMMIT)." >&2; \
	  echo "upgrade_roundhouse: when spinel refuses main.rb, the new Roundhouse can need" \
	    "a newer spinel. Run make upgrade_spinel first." >&2; exit 1; }
	@echo "upgrade_roundhouse: ROUNDHOUSE_COMMIT is $$(cat ROUNDHOUSE_COMMIT), from $(ROUNDHOUSE_COMMIT)."

.PHONY: roundhouse test-roundhouse upgrade_roundhouse
