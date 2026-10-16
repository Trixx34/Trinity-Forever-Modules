# `install-forever.sh` -- the question set

Install planner for the whole Forever stack on a fresh machine. It walks
through every question below, collects the answers, and prints the plan of
what v2 would do. **V1 executes nothing**: no package installs, no
directories, no users, no git clones, no database writes, no service
changes, no firewall rules, no secrets. It writes no file at all.

```
curl -fsSL <github-raw-url>/serverScripts/install-forever.sh | sudo bash
sudo bash install-forever.sh              # same script from a file
sudo bash install-forever.sh --help       # the header, without a terminal
```

**Pipe safety.** Piped through `curl`, stdin is the script itself, so no
prompt may read stdin. Every prompt reads the controlling terminal on fd 3
(`exec 3</dev/tty`), so the answers come from your keyboard and the script
body is never consumed. With no controlling terminal (CI, a cron job, a
redirected run) the script exits 2 with an error instead of eating its own
source. Keeping the plan works because of the same rule:

```
sudo bash install-forever.sh | tee ~/forever-plan.txt
```

**Never started.** The script says this twice on purpose: once in a banner
before the first question, and once at the end after the plan. Nothing here
enables or starts a service, and v2 will not either -- the units are created
and left off, because the worldserver cannot run until the client/DBC data
is in `data/`, which no script can produce. The closing section lists the
exact directories to fill and the exact `systemctl enable --now` commands to
run afterwards. **v1 prints those commands; it does not run them.**

**Credentials.** The script never asks for, prints or writes a secret value.
It asks for paths and environment variable *names* only. The convention it
plans for is the existing one: `/etc/forever/credentials/<name>.env`,
`root:<service group>`, mode 0640, one `TC_<KEY>=...` line per database
setting.

## The questions

| # | Question | Why it is asked | Planned action it feeds |
|---|---|---|---|
| Q1 | Confirm the OS/distro (detected, default filled in) | Package names, service paths and `useradd` flags differ per distro. `install-tools.sh` is apt-based and the units are systemd, so v1 plans for Debian/Ubuntu only. | `[0]` |
| Q2 | Root? (only asked when not root) | Almost every planned action needs root. The script says so and shows the pipe-to-bash-as-root form. | `[0]` |
| Q3 | Install root (default `/srv/forever`) | The whole tree hangs off it, and the ownership rules in `setup-directories.sh` are written for it. The script reports whether the path exists and is empty. | `[0]`, `[3]` |
| Q4 | Server timezone (detected, default filled in) | `Bot.Run.StopTime` and `BotsStopTime` are LOCAL clock times and every log timestamp depends on it. This is a server setting, not a display preference. | `[0]` |
| Q5 | Install the sim server as well? | Prod and sim each need a tree, a user, a unit and a credential file. The sim is what `start-sim-soak.sh` and the bot runs use, and it is what the sim-only questions below hang off. | `[2]`, `[3]`, `[9]`, `[16]` |
| Q6 | Re-run behaviour: refuse / upgrade / repair | Decide what a second run over an existing install does. `repair` is what `setup-directories.sh` does today; `upgrade` also re-clones and re-copies units. | `[0]` |
| Q7 | Create all folders? (mirror of `setup-directories.sh`) | The plan prints every directory with its owner, group and mode, copied from the script that owns that truth. | `[3]` |
| Q8 | Which client/build is the DBC data from? Are the files already here, and where? | `data/dbc`, `data/maps`, `data/vmaps`, `data/mmaps` and `data/gt` come from a WoW **client**, not from git. The installer creates the folders and moves on: a missing copy is not an error, nothing is blocked on it, and nothing here downloads or extracts them. The units check for `data/dbc` before starting, so the server still will not run until you fill them. | `[4]`, and the closing section |
| Q9 | Which modules? (1 core, 2 SDK, both forced on; 3 Bots, 4 vendor-trash, 5 group-loot, 6 dashboard) | Decides what gets cloned, what gets built and what gets installed into `modules_runtime`. Core and the SDK are required as soon as any module is chosen, so they are added automatically. | `[5]`, `[8]` |
| Q9b | *(only with Bots)* Where are `starter_quests.txt` / `world_quests.txt`, `grind_spots.txt`, `rotations.txt`? | A bot run needs them before the soak starts; `start-sim-soak.sh` takes a `--data-dir` and copies the files into `sim/modules_runtime/forever-bots/data`. | `[5]` |
| Q10 | Git host and owner prefix, and how to authenticate: public, SSH deploy key (path only) or token (env var name only) | The repos may be private. A private clone needs a deploy key or a token on the machine; the script asks for the *path* or the *variable name*, never the secret. | `[5]` |
| Q11 | Should v2 install the missing build tools? | cmake, a C++20 compiler, make/ninja, `protoc` (the SDK generates protobuf sources), Boost, git. Asked at all only when something is actually missing here. | `[1]` |
| Q11b | *(only with a `.7z` dump)* Add `p7zip-full`? | The plan prints `7z x …` to unpack the archive. A user with a plain `.sql` dump is never asked about a tool they do not need, so this is asked at Q12c, where the dump path is known, and the package joins the same install list in `[1]`. | `[1]` |
| Q12a | Local or remote database? If local, install MySQL? If remote, host and port. | The sim unit wants `mysql.service`; a remote DB changes the firewall plan and the credential files. MySQL 8.4 from the MySQL APT repo is the `install-tools.sh` convention. | `[6]` |
| Q12b | **v2 creates the schema.** Confirm. | One of the two SQL writes the installer performs, and the only unavoidable one: with no schema there is nothing to start against. It is a confirmation, not a choice of who does it. The plan prints the exact `mysql` commands so you can do it by hand. | `[6]` (1)-(3) |
| Q12c | Which **working directory** gets the dump (derived from the unit files, correctable), the dump file itself (`.sql` or `.7z`), and **v2 places it for the core to import**. Confirm. | World and hotfix are the two databases v2 does *not* import. The core looks for the file in its working directory and imports it on first start (`DBUpdater.cpp:189,248`, resolved at `:369-381`). Both services have a working directory of their own (`<root>/sim/run`, `<root>/prod/run`), so the dump never lands in a log folder. The known dump on this machine is `/srv/forever/build/test-run/tdb/`; on a fresh one you supply your own. v1 prints the placement commands, never runs them. | `[6]` (4) |
| Q13 | Confirm the credential plan | Records the three file paths, their groups and modes, and the `TC_<KEY>` names. v2 creates empty placeholders only where no file exists and never overwrites one. | `[7]` |
| Q14 | Create the service users? Which admin login goes into `forever-ops`? | The six groups and four users, plus the one admin membership, are exactly `setup-directories.sh`. An admin is a real local login, so the script verifies it exists. | `[2]` |
| Q15 | Is the Battle.net login server in scope? (default **off**) Install the units? *(with sim)* derive `MemoryMax` from RAM, or set it? | The unit exists in the repo and `install-services.sh` installs it, but it needs a certificate pair made by `create-bnet-certs.sh` **run as your own login**, so it defaults to off. `MemoryMax` (default `9G`) plus `OOMScoreAdjust=500` is what stops a bot run starving prod on the same box; you can have the default derived from detected RAM instead (60%, floor 2G), because a fixed 9G is wrong on a small machine. | `[9]` |
| Q16 | Which ports should the firewall plan cover? (none is the default) | 8085 worldserver, 3724 auth, 1119+8081 Battle.net, 3306 MySQL, 8088 dashboard. The sim ports 9085/4443/8878 stay closed -- the sim binds 127.0.0.1 too. **v1 opens nothing**; this records intent only. | `[10]` |

## How this maps onto the original ten-step sketch

| Original step | Now |
|---|---|
| 1 Where to install | Q3 |
| 2 Sim as well? | Q5 |
| 3 Create all folders | Q7 (+ Q14 for ownership) |
| 4 Which modules | Q9 (+ Q9b) |
| 5 Pull the repos | Q10 |
| 6 Database | Q12a, Q12b, Q12c |
| 7 Credentials | Q13 |
| 8 Users and rights | Q14 |
| 9 Init/systemd scripts | Q15 |
| 10 No auto-start, maps must be placed first | Q8 (its own question) + `[11]`, and the "NOT done" block |

The extra questions were folded into the step they belong to rather than
appended: OS and root (Q1, Q2) gate every step and run first; client data
(Q8) became a full question instead of a footnote under the maps note; the
observability bits sit inside the module list (Q9 item 6 and the Telegraf
note) instead of a separate question.

## What the SQL steps actually are

The real layout on disk (verified, not assumed):

```
sql/base/auth_database.sql          sql/create/create_mysql.sql
sql/base/characters_database.sql    sql/create/drop_mysql.sql
sql/base/dev/                       sql/create/drop_mysql_8.sql
  DO_NOT_IMPORT_THESE_FILES.txt     sql/updates/<database>/master/
  (development samples)
```

The five steps, in the order the plan prints them:

| # | Step | Command / source |
|---|---|---|
| 1 | create the databases | `mysql … < sql/create/create_mysql.sql` -- this is the real schema-creation step; there are no per-database `CREATE DATABASE` files to hunt for |
| 2 | auth schema | `mysql … -p auth < sql/base/auth_database.sql` |
| 3 | characters schema | `mysql … -p character < sql/base/characters_database.sql` |
| 4 | world + hotfixes | **not imported by the installer.** v2 places the dump in the service's working directory and the core imports it on first start |
| 5 | `sql/updates/<database>/master/` | **the core's job**, applied at startup -- see the next section |

**Why `sql/base/dev/` is skipped.** It holds development samples: a world and
a hotfixes database, kept next to a `DO_NOT_IMPORT_THESE_FILES.txt` warning.
Importing them would lay a sample world straight over the real TDB dump from
step (4) -- the exact opposite of what you want. This is a deliberate skip,
not an oversight; if you find those files later, leave them alone. Their
names are not printed as commands anywhere in the plan, on purpose.

**`sql/create/drop_mysql.sql` and `drop_mysql_8.sql` are never printed.** They
DROP databases. The plan names them only in the "deliberately not used"
list, so nobody reaches for them by accident.

**`sql/updates/<database>/master/`** is one directory per database (auth,
characters, hotfixes, world), each currently empty in this checkout. The
plan describes it as a directory rather than enumerating files, because the
core walks it recursively and the contents depend on the commit.

## The dump is placed, not imported

`DBUpdater.cpp` gives login and characters `LOCATION_REPOSITORY` (they read
`sql/base/*.sql` straight from `SourceDirectory`, `:147-151` and `:206-210`).
World and hotfix are `LOCATION_DOWNLOAD` instead (`:187-190`, `:246-249`):
the base file name is resolved against `boost::filesystem::current_path()`
(`:369-381`). The core downloads nothing -- it expects the file to already
be in the service's **working directory** and imports it itself. That is
why steps (1)-(3) use `mysql` and step (4) does not.

Working directories, read from the units themselves:

| Service | Working directory | Source |
|---|---|---|
| sim | `<install root>/sim/run` | `forever-sim.service:14`, and `setup-directories.sh:163` creates it with the comment that it "reads the TDB base files from here" |
| prod | `<install root>/prod/run` | `forever-prod.service:18`, created by `setup-directories.sh:164` |

Both are a folder of their own, `0750` owned by the service user, and both
are in their unit's `ReadWritePaths`. Prod used to point at
`/var/log/forever/prod`, which meant a ~600 MB dump sat in the folder the
server writes its logs to; the dedicated folder removes that. Q12c prints
the derived values and still lets you correct them rather than assuming.

**Is re-application guarded?** Yes -- by emptiness, not by a hash.
`DBUpdater<T>::Populate()` runs `SHOW TABLES` and returns immediately if the
database has any table at all (`DBUpdater.cpp:352-355`); the base file is
imported only into an **empty** database. So a dump left in the working
directory is inert while the database exists, and would be imported again
without anyone asking if that database were ever dropped and recreated.
The closing section therefore tells you to move the file out after the
first successful start.

The `sql/updates/<database>/master/` phase is separate: `DatabaseLoader`
queues a `Populate` and an `Update` callback and runs them in order
(`OpenDatabases -> PopulateDatabases -> UpdateDatabases -> PrepareStatements`,
`DatabaseLoader.cpp:126-137`), and `Update()` is hash-tracked through the
updates archive, unlike the base file.

## Updates are applied by the core

Not by the installer, and deliberately so. When a database is opened the
core applies `sql/base` and `sql/updates` itself:

| Where | What |
|---|---|
| `DatabaseLoader.cpp:27-28` | reads `Updates.AutoSetup` (default `1`, on) and `Updates.EnableDatabases` (default `15` = all four databases) |
| `DatabaseLoader.cpp:60` | auto-populates an empty database |
| `DatabaseLoader.cpp:84-85` | populates/updates whenever they are enabled for the pool |
| `DBUpdater.cpp:149,208` | reads `sql/base/*.sql` from `SourceDirectory` |

So the installer does **not** ask about post-start updates and does not
apply them: v2 must not run those files, or each one would run twice. The
whole SQL story is one line -- **the installer writes the schema and loads
the dump; the core keeps itself updated.** Those are the only two SQL
writes it performs. The exact files are listed in the section above.

`SourceDirectory` is a real config key that the shipped template leaves
empty (`worldserver.conf.dist:209-215`: *"If the path is left empty, the
built-in CMAKE_SOURCE_DIR is used"*), falling back to the path compiled in
at build time (`revision_data.h.in.cmake:38`). It is read at **runtime** by
the service process -- `DBUpdater::GetBaseFile()` calls
`BuiltInConfig::GetSourceDirectory()` (`BuiltInConfig.cpp:42-46`) each time
it resolves a base file -- so the service user needs read access to it.

**Unverified:** whether `prod/conf` or `sim/conf` override it. Both
directories are `0750` and owned by `forever-prod`/`forever-sim`, and the
planner runs as a normal user, so it cannot read them. An earlier version of
this document asserted they were unset; that was a permission-denied grep
being read as "no match", and the claim is withdrawn. v2 must check both
before trusting the compiled-in path.

## Two more findings worth reading before v2 is written

**The dashboard is a Flask app on 8088.** Confirmed from
`deploy/install-dashboard.sh` in the forever-dashboard repo: account
`forever-dash`, code in `/opt/forever-dashboard`, config in
`/etc/forever-dashboard`, unit `forever-dashboard.service`. It binds
localhost by default, so an SSH tunnel needs no firewall rule. Grafana is not
part of this stack -- `grafana/build_dashboard.py` only emits JSON for a
Grafana you already run.

**One installer can undo a unit.** `install-dashboard.sh --remove` runs
`systemctl disable --now forever-dashboard` (`install-dashboard.sh:45`), so
the closing steps install the dashboard *before* enabling it and warn
against a later `--remove`. `install-services.sh` has no such line -- it only
runs `daemon-reload` (`install-services.sh:130`) and starts nothing. (This
was first reported as "disables on every run"; that was wrong. The
`systemctl disable` sits inside the `--remove` branch, not the install
path.)

## Scope: one machine, deliberately

Everything here assumes prod **and** sim on the same host. That is what the
layout, the six `forever-*` groups and the `MemoryMax=9G` /
`OOMScoreAdjust=500` pair on the sim unit imply. Sim-on-a-second-host would
change the directory tree, the users and the memory limits, and would need
its own questions rather than a different answer to Q5.

## Deliberately outside the question set

- **`/srv/forever/src/apply-pending.sh`** -- an operator runbook *for this
  machine*. It references paths that already exist here (the dashboard, the
  maps, the sim quests), it is not a fresh-install step, and it is not in
  `AGENTS.md`'s module list.
- **`/srv/forever/src/trash`** -- a setgid directory holding its own `.git`;
  a discarded checkout, not a component. Not in the module list either.
- **Anything secret** -- no password, key or token value, only paths and
  variable names.

## What v1 deliberately does not ask

- **Anything it cannot answer.** Who owns the client licence, which MySQL
  version a remote host runs, what the WAN IP is. Those are notes in the
  plan, not questions.

## Still open for v2 (not blocking v1)

- Whether v2 may ever write to the database at all (`[6]` offers it as a
  choice, the default is no).
- Whether `MemoryMax` should be derived from the machine's RAM instead of
  asked; on a small box the default 9G is wrong.
- Whether the sim is wanted on a second machine rather than the same one.
  Everything here assumes one box, which is what the layout and the
  `OOMScoreAdjust` pair imply.
