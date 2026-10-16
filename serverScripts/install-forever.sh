#!/usr/bin/env bash
# Install planner for the whole Forever stack on a fresh Linux machine:
# prod (and optionally sim) worldservers, modules, dashboard, database,
# users, directories, systemd units and a firewall plan.
#
#   curl -fsSL <github-raw-url>/serverScripts/install-forever.sh | sudo bash
#   sudo bash install-forever.sh              # or download it first
#   sudo bash install-forever.sh --help
#
# V1 IS A PLANNER ONLY. It asks every question below, then prints the
# complete plan of what v2 would do. It executes none of it: no package
# installs, no directory or user creation, no git clones, no database
# writes, no service changes, no firewall rules, no secrets. It writes no
# file at all, not even a log; redirect stdout if you want to keep the
# plan:   sudo bash install-forever.sh | tee ~/forever-plan.txt
#
# Pipe safety: piped through curl, stdin is the script itself, so no prompt
# may read stdin. Every prompt reads the controlling terminal from fd 3
# (exec 3</dev/tty below). With no controlling terminal the script exits
# with an error instead of reading the script as answers.
#
# Credentials: it never asks for, prints, or writes secret values -- only
# paths and environment variable names. The existing convention holds:
# /etc/forever/credentials/<name>.env, root:<service group>, mode 0640,
# one TC_<KEY>=... line per database setting.
#
# The tree, users and units it plans for are the ones setup-directories.sh
# and install-services.sh already implement; v2 should call those scripts
# rather than reimplement them. The dashboard has its own installer,
# deploy/install-dashboard.sh in the forever-dashboard repo.
#
# The full question set, with the reason for each one, is documented in
# serverScripts/install-forever.md.
set -euo pipefail

usage() {
  # The header comment, when the script runs from a file. Piped through curl
  # there is no file to read, so fall back to a one-line summary.
  local self="${BASH_SOURCE[0]:-}"
  if [[ -n "$self" && -r "$self" ]]; then
    awk 'NR > 1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$self"
  else
    echo "install-forever.sh -- Forever stack install planner (v1 asks every"
    echo "question and prints the plan; it executes nothing). Download it first"
    echo "to read the header, or see serverScripts/install-forever.md."
  fi
}

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
  "")        ;;
  *) echo "unknown option: $1 (v1 takes no options)" >&2; exit 2 ;;
esac

# Every prompt below reads the controlling terminal on fd 3 and never stdin,
# because piped through curl stdin is the script itself. The test runs in a
# subshell, so a failure cannot leave the script's own stderr pointing at
# /dev/null for the rest of the run.
if ! ( exec 3</dev/tty ) 2>/dev/null; then
  echo "no controlling terminal -- run this interactively:  sudo bash install-forever.sh" >&2
  exit 2
fi
exec 3</dev/tty

say()     { printf '%s\n' "$*"; }
section() { printf '\n== %s\n' "$*"; }
trim()    { local s="$1"; s="${s#"${s%%[![:space:]]*}"}"; s="${s%"${s##*[![:space:]]}"}"; printf '%s' "$s"; }
have()    { command -v "$1" >/dev/null 2>&1; }
is_root() { [[ $EUID -eq 0 ]] && echo yes || echo no; }

# ask <var> <prompt> [default] [empty-ok] -- free text. Loops until non-empty
# unless the 4th arg is y, where an empty answer returns empty (for optional
# questions like "admin login, empty for none").
ask() {
  local var="$1" prompt="$2" def="${3:-}" empty_ok="${4:-n}" input
  while :; do
    if [[ -n "$def" ]]; then printf '%s [%s]: ' "$prompt" "$def"
    elif [[ "$empty_ok" == y ]]; then printf '%s (empty ok): ' "$prompt"
    else printf '%s: ' "$prompt"; fi
    IFS= read -r input <&3 || { echo; exit 0; }
    input="$(trim "$input")"
    [[ -z "$input" ]] && input="$def"
    if [[ -n "$input" ]]; then printf -v "$var" '%s' "$input"; return 0; fi
    if [[ "$empty_ok" == y ]]; then printf -v "$var" '%s' ""; return 0; fi
    say "  (an answer is needed)"
  done
}

# yesno <var> <prompt> <default y|n>
yesno() {
  local var="$1" prompt="$2" def="${3:-y}" input yn="Y/n"
  [[ "$def" == "n" ]] && yn="y/N"
  while :; do
    printf '%s [%s]: ' "$prompt" "$yn"
    IFS= read -r input <&3 || { echo; exit 0; }
    input="$(trim "$input")"; input="${input,,}"
    [[ -z "$input" ]] && input="$def"
    case "$input" in
      y|yes) printf -v "$var" '%s' y; return 0 ;;
      n|no)  printf -v "$var" '%s' n; return 0 ;;
      *) say "  (y or n)" ;;
    esac
  done
}

# choose <var> <prompt> <default-n> <option...> -- stores the option number.
choose() {
  local var="$1" prompt="$2" def="$3"; shift 3
  local opts=("$@") i input
  for i in "${!opts[@]}"; do printf '  %d) %s\n' "$((i + 1))" "${opts[$i]}"; done
  while :; do
    printf '%s [%s]: ' "$prompt" "$def"
    IFS= read -r input <&3 || { echo; exit 0; }
    input="$(trim "$input")"
    [[ -z "$input" ]] && input="$def"
    if [[ "$input" =~ ^[0-9]+$ ]] && (( input >= 1 && input <= ${#opts[@]} )); then
      printf -v "$var" '%s' "$input"; return 0
    fi
    say "  (1..${#opts[@]})"
  done
}

# pickmany <var> <prompt> <default> <n> [maybe] -- stores sorted unique numbers.
# "all" takes every entry; "none" (5th arg maybe) clears the set.
pickmany() {
  local var="$1" prompt="$2" def="$3" n="$4" maybe="${5:-no}" input tok ok
  while :; do
    printf '%s (space separated, [%s]): ' "$prompt" "$def"
    IFS= read -r input <&3 || { echo; exit 0; }
    input="$(trim "$input")"
    [[ -z "$input" ]] && input="$def"
    case "${input,,}" in
      all)  input="$(seq 1 "$n" | tr '\n' ' ')" ;;
      none) if [[ "$maybe" == maybe ]]; then printf -v "$var" '%s' ""; return 0; fi
            say "  (pick at least one)"; continue ;;
    esac
    local -a sel=(); ok=1
    for tok in ${input//,/ }; do
      if [[ "$tok" =~ ^[0-9]+$ ]] && (( tok >= 1 && tok <= n )); then
        sel+=("$tok")
      else ok=0; fi
    done
    (( ok )) || { say "  (numbers 1..$n, or: all$( [[ "$maybe" == maybe ]] && echo ", or: none" ))"; continue; }
    [[ -z "${sel[*]:-}" ]] && { say "  (pick at least one)"; continue; }
    local IFS=$'\n'
    sel=($(printf '%s\n' "${sel[@]}" | sort -n -u))
    printf -v "$var" '%s' "${sel[*]}"
    return 0
  done
}

# --- the one thing to read before anything else --------------------------------

section "READ THIS FIRST -- nothing here will start a service"

say "This installer never enables and never starts a service. Not now, not in"
say "v2. It creates nothing yet either: v1 asks every question and prints the"
say "plan, and that is all it does."
say
say "And the worldserver will not run until YOU place the client data, because"
say "both units check for it before they start (ExecStartPre test -r data/dbc):"
say
say "    <install root>/data/dbc      <install root>/data/vmaps"
say "    <install root>/data/maps     <install root>/data/mmaps"
say "                              <install root>/data/gt"
say
say "Those files come from a WoW client. No script can download or extract"
say "them, so the installer creates the folders and moves on -- the plan at"
say "the end tells you exactly what goes where, and the last section gives"
say "you the commands to enable the services once the data is in place."
say
say "Press Enter to start the questions."

# --- preflight (read-only detection, printed before the questions) -------------

OS_ID=unknown; OS_VER=""; OS_PRETTY="$OS_ID"
if [[ -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  . /etc/os-release
  OS_ID="${ID:-unknown}"; OS_VER="${VERSION_ID:-}"; OS_PRETTY="${PRETTY_NAME:-$OS_ID}"
fi
ARCH="$(uname -m)"
TZ="$(readlink -f /etc/localtime 2>/dev/null | sed 's#^/usr/share/zoneinfo/##')"
[[ -n "$TZ" ]] || TZ="$(date +%Z)"
RAM="$(free -h 2>/dev/null | awk '/^Mem:/{print $2}')"
[[ -n "$RAM" ]] || RAM="?"

section "Preflight (detected, nothing changed)"
say "OS:        $OS_PRETTY ($OS_ID $OS_VER)"
say "arch:      $ARCH"
say "root:      $(is_root)"
say "timezone:  $TZ"
say "memory:    $RAM"
TOOLS_LINE=""
for t in cmake g++ make ninja protoc git python3 mysql ufw; do
  if have "$t"; then TOOLS_LINE+="$t=ok "; else TOOLS_LINE+="$t=MISSING "; fi
done
if [[ -r /usr/include/boost/version.hpp ]]; then TOOLS_LINE+="boost=ok"; else TOOLS_LINE+="boost=MISSING"; fi
say "tools:     $TOOLS_LINE"
say
say "Every prompt reads the terminal on fd 3, never stdin, so the script"
say "works when piped. Defaults are in [brackets]; press Enter to accept."

# --- Q1: OS and distro --------------------------------------------------------

section "Q1 -- OS and distro"
say "Package names, service paths and useradd flags differ per distro."
say "Detected: $OS_PRETTY ($ARCH). v1 plans for the Debian/Ubuntu family only"
say "(install-tools.sh is apt-based and the units are systemd)."
ask OS_CONFIRM "Confirm the OS/distro" "$OS_PRETTY"

# --- Q2: root requirement -----------------------------------------------------

section "Q2 -- Root"
if [[ $EUID -ne 0 ]]; then
  say "Not root. Most planned actions need root, and the usual invocation is"
  say "pipe-to-bash-as-root:  curl -fsSL <github-raw-url>/serverScripts/install-forever.sh | sudo bash"
  yesno ROOT_OK "Continue planning without root? (v1 changes nothing either way)" y
else
  say "Running as root. v1 still changes nothing; v2 will need this."
fi

# --- Q3: install root ---------------------------------------------------------

section "Q3 -- Install root"
say "Everything lives under this path. /var/log/forever and /etc/forever stay"
say "outside it (system paths, same as setup-directories.sh)."
while :; do
  ask INSTALL_ROOT "Install root" /srv/forever
  [[ "$INSTALL_ROOT" == /* ]] && break
  say "  (an absolute path)"
done
INSTALL_ROOT="${INSTALL_ROOT%/}"
if [[ -d "$INSTALL_ROOT" ]]; then
  ENTRY_COUNT="$(find "$INSTALL_ROOT" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l)"
  if (( ENTRY_COUNT == 0 )); then TREE_STATE="exists, empty"
  else TREE_STATE="exists, NOT empty ($ENTRY_COUNT entries)"; fi
else
  TREE_STATE="does not exist"
fi
say "  $INSTALL_ROOT: $TREE_STATE"
if [[ "$TREE_STATE" == *"NOT empty"* ]]; then
  say "  Note: Q6 decides what a second run over that tree does."
fi

# --- Q4: timezone and clock ---------------------------------------------------

section "Q4 -- Timezone and clock"
say "Bot.Run.StopTime and BotsStopTime are LOCAL clock times and log"
say "timestamps depend on the same clock, so this is a server setting, not a"
say "display preference."
ask TZ_CONFIRM "Server timezone" "$TZ"

# --- Q5: sim server too? ------------------------------------------------------

section "Q5 -- Sim server too?"
say "Prod is a live world; sim is the bot test world on the same machine."
say "Both need their own tree, user, unit and credential file. The sim is what"
say "start-sim-soak.sh and the bot runs use, and it needs TDB world base data."
yesno SIM_OK "Install the optional sim server as well as prod?" y

# --- Q6: idempotency ----------------------------------------------------------

section "Q6 -- Idempotency"
say "What should a second run over an existing install do?"
choose IDEM "Re-run behaviour" 3 \
  "refuse   -- abort if the install root is not empty" \
  "upgrade -- refresh an existing install (re-clone, re-copy units, fix rights)" \
  "repair  -- only create what is missing and fix ownership/modes, like setup-directories.sh"
case "$IDEM" in
  1) IDEM=refuse ;; 2) IDEM=upgrade ;; 3) IDEM=repair ;;
esac

# --- Q7: create the folders ---------------------------------------------------

section "Q7 -- Create all folders"
say "Mirrors setup-directories.sh exactly (same owners, groups, modes):"
say "  data tree (forever-build:forever-data 2750), src+build (forever-build 2775),"
say "  prod tree (forever-prod 0750)$( [[ "$SIM_OK" == y ]] && echo ", sim tree (forever-sim 0750) + sim/run" ),"
say "  backups (forever-backup:forever-ops 0750), /var/log/forever{,/prod$( [[ "$SIM_OK" == y ]] && echo ",/sim" )},"
say "  /etc/forever{,/credentials} (root 0700/0750)."
yesno FOLDERS_OK "Plan folder creation with this scope?" y

# --- Q8: client / DBC data ----------------------------------------------------

section "Q8 -- Client / DBC data (you supply it; nothing blocks on it)"
say "The server needs data/dbc, data/maps, data/vmaps, data/mmaps and data/gt."
say "Those come from a WoW client, NOT from git: no script can download or"
say "extract them. v2 creates the folders and moves on -- filling them is your"
say "step, and a missing copy here is not an error and does not stop anything."
ask CLIENT_SOURCE "Which client / build do they come from (e.g. 11.2.7 retail, ptr)?" ""
yesno DATA_PRESENT "Are the data files already on this machine?" n
DATA_PATH=""
if [[ "$DATA_PRESENT" == y ]]; then
  ask DATA_PATH "Where are they now (a path, even one that does not exist yet)" ""
  if [[ -n "$DATA_PATH" && ! -d "$DATA_PATH" ]]; then
    say "  noted: $DATA_PATH is not there now; v2 creates the tree, you fill it"
  fi
else
  say "  noted: you will place them in <install root>/data/ yourself."
fi

# --- Q9: modules --------------------------------------------------------------

section "Q9 -- Which modules to install"
say "  1) Trinity-Forever-Modules (core)      -- REQUIRED, added automatically"
say "  2) forever-module-sdk                   -- REQUIRED with any module, added automatically"
say "  3) MOD-Forever-Bots                     -- optional, the bot test worlds"
say "  4) MOD-auto-vendor-trash                -- optional"
say "  5) MOD-quest-group-loot                 -- optional"
say "  6) forever-dashboard                    -- optional, LAN page to check and control"
say "       the servers (port 8088, account forever-dash, /opt/forever-dashboard)"
say "The Telegraf bots-metrics input (serverScripts/telegraf/) belongs to the"
say "core repo and is installed separately. Grafana itself is not part of this"
say "stack: build_dashboard.py only produces a JSON for an existing Grafana."
pickmany MODULES "Pick modules (1 and 2 are added for you)" "1 2" 6

MODS="$MODULES"
for req in 1 2; do
  [[ " $MODS " == *" $req "* ]] || MODS="$MODS $req"
done
MODS="$(printf '%s\n' $MODS | sort -n -u | tr '\n' ' ')"

mod_name() {
  case "$1" in
    1) echo "Trinity-Forever-Modules (core)" ;;
    2) echo "forever-module-sdk" ;;
    3) echo "MOD-Forever-Bots" ;;
    4) echo "MOD-auto-vendor-trash" ;;
    5) echo "MOD-quest-group-loot" ;;
    6) echo "forever-dashboard" ;;
  esac
}
mod_repo() {
  case "$1" in
    1) echo "Trinity-Forever-Modules" ;; 2) echo "forever-module-sdk" ;;
    3) echo "MOD-Forever-Bots" ;; 4) echo "MOD-auto-vendor-trash" ;;
    5) echo "MOD-quest-group-loot" ;; 6) echo "forever-dashboard" ;;
  esac
}

SOAK_DATA=""
if [[ " $MODS " == *" 3 "* ]]; then
  say
  say "Bots extra: a run also needs starter_quests.txt or world_quests.txt and"
  say "grind_spots.txt (rotations.txt optional). start-sim-soak.sh reads them"
  say "from --data-dir, by default the bots repo clone, and copies them to"
  say "sim/modules_runtime/forever-bots/data before the run."
  ask SOAK_DATA "Where are those data files (path, or empty to generate them later with tools/prepare_soak.sh)" "" y
fi

# --- Q10: git access ----------------------------------------------------------

section "Q10 -- Pull the repos from git"
ask GIT_ORG "Git host and owner prefix (e.g. github.com:Trixx34, or an https base URL)" github.com:Trixx34
choose GIT_AUTH "How should the machine authenticate to the git host?" 1 \
  "public  -- repos need no authentication" \
  "private -- SSH deploy key (you give the KEY PATH, never the key itself)" \
  "private -- token (you give the ENV VAR NAME holding it, never the value)"
case "$GIT_AUTH" in
  1) GIT_AUTH=public ;; 2) GIT_AUTH=ssh ;; 3) GIT_AUTH=token ;;
esac
SSH_KEY=""; TOKEN_VAR=""
if [[ "$GIT_AUTH" == ssh ]]; then
  while :; do
    ask SSH_KEY "Path to the SSH deploy key (e.g. /home/you/.ssh/forever_deploy)" ""
    [[ -r "$SSH_KEY" ]] && break
    say "  (not a readable file)"
  done
  say "  v2 adds the public half to the git host as a deploy key; the private"
  say "  half never leaves this machine and is never printed here."
elif [[ "$GIT_AUTH" == token ]]; then
  ask TOKEN_VAR "Name of the environment variable holding the token (value is never printed or written)" ""
  if [[ -n "${!TOKEN_VAR:-}" ]]; then
    say "  ok: \$$TOKEN_VAR is set in this environment (value not shown)"
  else
    say "  note: \$$TOKEN_VAR is not set here; v2 will require it at clone time"
  fi
fi
say
say "State as of 2026-10-16, recorded so v2 re-checks it before cloning:"
say "  MOD-Forever-Bots main f0e701e -- history rewritten, authors normalised."
say "  forever-module-sdk has a local unpushed merge (bd8d087) that is being"
say "  fixed against this machine's Boost; v2 must build it, not assume it."

# --- Q11: build toolchain -----------------------------------------------------

section "Q11 -- Build toolchain"
say "cmake, a C++20 compiler, make/ninja, protoc (the SDK generates protobuf"
say "sources), Boost and git. Package names per install-tools.sh."
MISSING_TOOLS=()
for t in cmake g++ make ninja protoc git python3; do have "$t" || MISSING_TOOLS+=("$t"); done
[[ -r /usr/include/boost/version.hpp ]] || MISSING_TOOLS+=("boost")
have mysql || MISSING_TOOLS+=("mysql-client")
if (( ${#MISSING_TOOLS[@]} > 0 )); then
  say "Missing here: ${MISSING_TOOLS[*]}"
  yesno TOOLS_OK "Should v2 install the missing ones with apt?" y
else
  say "All build tools present."
  TOOLS_OK=n
fi

# --- Q12: database ------------------------------------------------------------

section "Q12a -- Where the database lives"
choose DB_WHERE "Where does the database live?" 1 \
  "local    -- install MySQL on this machine (8.4 from the MySQL APT repo, port 3306)" \
  "remote   -- an existing MySQL on another host"
case "$DB_WHERE" in
  1) DB_WHERE=local ;; 2) DB_WHERE=remote ;;
esac
if [[ "$DB_WHERE" == local ]]; then
  yesno DB_INSTALL "Plan a local MySQL 8.4 install?" y
  DB_HOST=127.0.0.1; DB_PORT=3306
else
  ask DB_HOST "Database host" ""
  while :; do
    ask DB_PORT "Database port" 3306
    [[ "$DB_PORT" =~ ^[0-9]+$ ]] && (( DB_PORT >= 1 && DB_PORT <= 65535 )) && break
    say "  (1..65535)"
  done
fi
section "Q12b -- Schema creation"
say "base + create + the updates needed to reach a working schema. This is one"
say "of the two SQL writes the installer performs: v2 creates the schema for"
say "you. It cannot be skipped -- with no schema there is nothing to start"
say "against. It is still only printed here; v1 runs no SQL at all."
yesno SCHEMA_OK "v2 will create the schema itself. Confirm?" y
say "  commands it would run are printed in the plan, so you can do it by hand."

section "Q12c -- Base world dump (the core imports this one, not v2)"
say "World and hotfix are different from the other two databases. The core"
say "expects the file to be in its working directory and imports it itself:"
say "  DBUpdater.cpp:189,248  world and hotfix are LOCATION_DOWNLOAD"
say "  DBUpdater.cpp:369-381  resolved against boost::filesystem::current_path()"
say "It downloads nothing. The file has to be there before the server starts."
say
say "Working directories, read from the unit files:"
say "  prod  /var/log/forever/prod   (forever-prod.service:14 -- that IS the log folder)"
if [[ "$SIM_OK" == y ]]; then
  say "  sim   $INSTALL_ROOT/sim/run     (forever-sim.service:14; setup-directories.sh:157"
  say "                                  calls sim/run the folder it 'reads the TDB base"
  say "                                  files from')"
fi
say
say "This machine already has a dump, for reference:"
say "  /srv/forever/build/test-run/tdb/TDB_full_world_forever_2026_10_14.sql"
say "  /srv/forever/build/test-run/tdb/TDB_full_hotfixes_forever_2026_10_14.sql"
say "  /srv/forever/build/test-run/tdb/TDB_full_forever_2026_10_14.7z"
say "On a fresh machine you supply your own."
say
if [[ "$SIM_OK" == y ]]; then
  choose DUMP_WHERE "Which working directory gets the dump?" 1 \
    "sim only  -- $INSTALL_ROOT/sim/run (default: the test world, and the folder built for it)" \
    "prod only -- /var/log/forever/prod (a v2 change to the prod unit's WorkingDirectory" \
    "             would be cleaner than keeping a dump in the log folder)" \
    "both      -- one in each, so whichever server starts first can import it"
  case "$DUMP_WHERE" in
    1) DUMP_DIRS="$INSTALL_ROOT/sim/run" ;;
    2) DUMP_DIRS="/var/log/forever/prod" ;;
    3) DUMP_DIRS="$INSTALL_ROOT/sim/run /var/log/forever/prod" ;;
  esac
else
  DUMP_DIRS="/var/log/forever/prod"
  say "No sim in this install, so the dump goes to the prod working directory:"
  say "  $DUMP_DIRS   (the log folder -- see forever-prod.service:14)"
fi
ask DUMP_PATH "The dump (.sql, or the .7z to unpack first); empty if you have none yet" "" y
yesno DUMP_OK "v2 will place it in: $DUMP_DIRS -- and the core imports it on first start. Confirm?" y
TOOLS_7Z=""
if [[ "$DUMP_PATH" == *.7z ]] && ! have 7z; then
  # Only a .7z user is asked: the .sql path needs no unpacker.
  MISSING_TOOLS+=("p7zip-full (for the .7z dump)")
  TOOLS_7Z=y
  say "7z is not installed here and your dump is an archive. v2 unpacks it"
  say "before loading it, which needs p7zip-full."
  yesno TOOLS_7Z_OK "Add p7zip-full to the packages v2 installs?" y
fi

# --- Q13: credentials ---------------------------------------------------------

section "Q13 -- Credentials"
say "Convention (never secret values -- paths and names only):"
say "  /etc/forever/credentials/prod-db.env     root:forever-prod     0640"
if [[ "$SIM_OK" == y ]]; then
  say "  /etc/forever/credentials/sim-db.env      root:forever-sim      0640"
fi
say "  /etc/forever/credentials/backup-db.env   root:forever-backup   0640"
say "  each holds TC_LOGIN_DATABASE_INFO, TC_WORLD_DATABASE_INFO,"
say "  TC_CHARACTER_DATABASE_INFO, TC_HOTFIX_DATABASE_INFO lines"
say "  (the core reads TC_<KEY> from the environment; see install-services.sh)."
say "v2 creates empty placeholder files only where none exist; an existing"
say "file is never overwritten, and the values are written by you."
yesno CREDS_OK "Confirm the credential plan?" y

# --- Q14: service users and ownership ----------------------------------------

section "Q14 -- Service users and folder ownership"
say "Per setup-directories.sh: groups forever-data, forever-ops, forever-prod,"
say "forever-sim, forever-backup, forever-build; users forever-build,"
say "forever-prod, forever-sim, forever-backup (system, nologin, home = install root)."
yesno USERS_OK "Plan creating them?" y
ADMIN=""
ask ADMIN "Optional admin login to add to forever-ops (leave empty for none)" "" y
while [[ -n "$ADMIN" ]] && ! id -u "$ADMIN" >/dev/null 2>&1; do
  say "  (no such login on this machine)"
  ask ADMIN "Admin login (leave empty for none)" ""
done
if [[ " $MODS " == *" 6 "* ]]; then
  say "forever-dashboard brings its own account forever-dash and a root-owned"
  say "/opt/forever-dashboard tree, created by its own installer."
fi

# --- Q15: systemd units and memory limits -------------------------------------

section "Q15 -- systemd units"
if [[ "$SIM_OK" == y ]]; then
  UNITS="forever-prod.service, forever-prod-bnet.service, forever-sim.service"
else
  UNITS="forever-prod.service, forever-prod-bnet.service"
fi
say "v2 would install $UNITS to /etc/systemd/system."
yesno BNET_OK "Is the Battle.net login server (forever-prod-bnet) in scope?" n
say "  default off: the certs step needs a real account, and prod is not"
say "  a Battle.net host on day one."
if [[ "$BNET_OK" == y ]]; then
  say "It needs a certificate pair made by create-bnet-certs.sh, run as your"
  say "own login and not by this script: server.key.pem and fullchain.pem go to"
  say "prod/conf, and root.key must stay off this server."
  BNET_STATE="unit installed; the certs are still made by hand"
else
  UNITS="forever-prod.service"
  BNET_STATE="out of scope (the unit and the certs are skipped)"
fi
say "Units are NOT enabled and NOT started. Prod stays off until the owner"
say "approves the start window; its unit keeps Restart=no until then."
yesno UNITS_OK "Plan installing the units?" y
MEM_LIMIT="n/a"
if [[ "$SIM_OK" == y ]]; then
  say "The fixed 9G default is right on a big box and wrong on a small one."
  yesno MEM_DERIVE "Derive the sim MemoryMax from this machine's RAM instead?" n
  if [[ "$MEM_DERIVE" == y ]]; then
    RAM_MB="$(free -m 2>/dev/null | awk '/^Mem:/{print $2}')"
    if [[ "$RAM_MB" =~ ^[0-9]+$ ]]; then
      LIMIT_MB=$(( RAM_MB * 60 / 100 ))
      (( LIMIT_MB < 2048 )) && LIMIT_MB=2048
      MEM_LIMIT="${LIMIT_MB}M"
      say "  derived: 60% of ${RAM_MB}M = $MEM_LIMIT (floor 2G, below that a sim is pointless)"
    else
      say "  could not read the total RAM; asking instead"
      MEM_DERIVE=n
    fi
  fi
  if [[ "$MEM_DERIVE" != y ]]; then
    while :; do
      ask MEM_LIMIT "sim MemoryMax (so a bot run cannot starve prod; OOMScoreAdjust=500 stays; 2G is the floor)" 9G
      [[ "$MEM_LIMIT" =~ ^[0-9]+[GMgm]$ ]] && break
      say "  (e.g. 9G or 8192M)"
    done
  fi
fi

# --- Q16: ports and firewall --------------------------------------------------

section "Q16 -- Ports and firewall"
say "  1) 8085/tcp  prod worldserver (LAN only)"
say "  2) 3724/tcp  auth (legacy)"
if [[ "$BNET_OK" == y ]]; then
  say "  3) 1119/tcp + 8081/tcp  Battle.net, when the login server goes live"
else
  say "  3) 1119/tcp + 8081/tcp  Battle.net (out of scope)"
fi
say "  4) 3306/tcp  MySQL (LAN only)"
if [[ " $MODS " == *" 6 "* ]]; then
  say "  5) 8088/tcp  dashboard (LAN only; it binds localhost by default, so an"
  say "       SSH tunnel is enough and no rule is needed)"
else
  say "  5) 8088/tcp  dashboard (you did not pick it)"
fi
say "The sim ports (9085/4443/8878) stay closed -- the sim binds 127.0.0.1 too."
say "v1 opens nothing; this question records intent only."
if have ufw; then
  say "ufw is present on this machine; v2 would add rules with 'ufw allow from'."
else
  say "ufw is absent here; v2 would say so instead of installing a firewall."
fi
pickmany PORTS "Plan firewall rules for which ports (v1 opens nothing)" none 5 maybe

# --- the plan -----------------------------------------------------------------

section "PLAN -- what v2 would do (v1 executes NONE of this)"

say "[0] Assumptions"
say "    OS: $OS_CONFIRM ($ARCH)   root: $(is_root)   timezone: $TZ_CONFIRM"
say "    Install root: $INSTALL_ROOT ($TREE_STATE)"
say "    re-run behaviour: $IDEM"

say
say "[1] Tools (apt, per install-tools.sh)"
# TOOLS_OK comes from Q11; TOOLS_7Z_OK from Q12c, which can add p7zip-full
# after Q11 already found everything present. Both feed the same list.
if [[ "${TOOLS_OK:-n}" == y || "${TOOLS_7Z_OK:-n}" == y ]]; then
  say "    install: ${MISSING_TOOLS[*]}"
else
  say "    nothing to install (all present)"
fi

say
say "[2] Users and groups (setup-directories.sh)"
if [[ "${USERS_OK:-y}" == y ]]; then
  say "    groups: forever-data forever-ops forever-prod forever-backup forever-build$( [[ "$SIM_OK" == y ]] && echo " forever-sim (user too)" )"
  say "    users:  forever-build forever-prod forever-backup$( [[ "$SIM_OK" == y ]] && echo " forever-sim" )"
  say "    in forever-data: forever-build, forever-prod, forever-sim, forever-backup"
  say "    in forever-ops:  forever-backup$( [[ -n "$ADMIN" ]] && echo ", $ADMIN" )"
else
  say "    not created; they must already exist"
fi

if [[ "${FOLDERS_OK:-y}" == y ]]; then
  say
  say "[3] Directories (owner:group mode) -- mirror of setup-directories.sh"
  plan_dir() { printf '    %-52s %s %s\n' "$1" "$2" "$3"; }
  plan_dir "$INSTALL_ROOT" root:root 0755
  plan_dir "$INSTALL_ROOT/data" forever-build:forever-data 2750
  for d in dbc maps vmaps mmaps gt; do plan_dir "$INSTALL_ROOT/data/$d" forever-build:forever-data 2750; done
  plan_dir "$INSTALL_ROOT/src" forever-build:forever-build 2775
  plan_dir "$INSTALL_ROOT/build" forever-build:forever-build 2775
  for d in "$INSTALL_ROOT/prod" "$INSTALL_ROOT/prod/conf" "$INSTALL_ROOT/prod/bin" \
           "$INSTALL_ROOT/prod/bin/scripts" "$INSTALL_ROOT/prod/lib" "$INSTALL_ROOT/prod/modules_runtime" \
           "$INSTALL_ROOT/prod/conf/worldserver.conf.d" "$INSTALL_ROOT/prod/conf/worldserver.conf.d/modules.d"; do
    plan_dir "$d" forever-prod:forever-prod 0750
  done
  if [[ "$SIM_OK" == y ]]; then
    for d in "$INSTALL_ROOT/sim" "$INSTALL_ROOT/sim/conf" "$INSTALL_ROOT/sim/bin" \
             "$INSTALL_ROOT/sim/bin/scripts" "$INSTALL_ROOT/sim/lib" "$INSTALL_ROOT/sim/modules_runtime" \
             "$INSTALL_ROOT/sim/conf/worldserver.conf.d" "$INSTALL_ROOT/sim/conf/worldserver.conf.d/modules.d" \
             "$INSTALL_ROOT/sim/run"; do
      plan_dir "$d" forever-sim:forever-sim 0750
    done
  fi
  for d in "$INSTALL_ROOT/backups" "$INSTALL_ROOT/backups/db" "$INSTALL_ROOT/backups/configs" "$INSTALL_ROOT/backups/manifests"; do
    plan_dir "$d" forever-backup:forever-ops 0750
  done
  plan_dir /var/log/forever root:root 0755
  plan_dir /var/log/forever/prod forever-prod:forever-prod 0750
  [[ "$SIM_OK" == y ]] && plan_dir /var/log/forever/sim forever-sim:forever-sim 0750
  plan_dir /etc/forever root:root 0700
  plan_dir /etc/forever/credentials root:root 0750
  if [[ " $MODS " == *" 6 "* ]]; then
    plan_dir /opt/forever-dashboard root:root 0755
    plan_dir /etc/forever-dashboard root:root 0700
    say "    (the dashboard folders come from its own installer; listed for the record)"
  fi
fi

say
say "[4] Client / game data (BEFORE any first start)"
say "    source:   ${CLIENT_SOURCE:-?}"
if [[ -n "$DATA_PATH" ]]; then
  say "    present:  yes, at $DATA_PATH"
  say "    action:   extract/copy into data/{dbc,maps,vmaps,mmaps,gt} as forever-build,"
  say "              then files to group forever-data, mode 0640 (setup-directories.sh rule)"
else
  say "    present:  not on this machine -- you supply them, and nothing here"
  say "              blocks on it: v2 creates the folders and finishes"
  say "    action:   place the files into data/{dbc,maps,vmaps,mmaps,gt} as"
  say "              forever-build, files to group forever-data, mode 0640"
fi

say
say "[5] Git repos under $INSTALL_ROOT/src"
for m in $MODS; do
  m_repo="$(mod_repo "$m")"; m_label="$(mod_name "$m")"
  if [[ "$m_repo" == "$m_label" ]]; then say "    $m_repo"
  else say "    $m_repo   <- $m_label"; fi
done
say "    from $GIT_ORG"
case "$GIT_AUTH" in
  public) say "    auth:     public, nothing needed" ;;
  ssh)    say "    auth:     SSH deploy key at $SSH_KEY (v2 adds the public half to the host)" ;;
  token)  say "    auth:     token from \$$TOKEN_VAR via a credential helper; value never printed or written" ;;
esac
[[ -n "$SOAK_DATA" ]] && say "    bots data files: $SOAK_DATA"

say
if [[ "$DB_WHERE" == local ]]; then
  DB_PLAN="local MySQL on $DB_HOST:$DB_PORT"
  [[ "${DB_INSTALL:-y}" == y ]] && DB_PLAN+=" (install planned)"
else
  DB_PLAN="remote $DB_HOST:$DB_PORT"
fi
say "[6] Database: $DB_PLAN"
say "    databases: auth, world, character, hotfix"
say "    if local: MySQL 8.4 from the MySQL APT repo (install-tools.sh convention)"
say
say "    The installer writes the schema and places the dump. Nothing else in the"
say "    database is its business. Five steps, in order -- and note the asymmetry:"
say "    (1)-(3) are imported by the installer with mysql, (4) is only PLACED and"
say "    the core imports it, (5) is not ours at all."
say
if [[ "${SCHEMA_OK:-y}" == y ]]; then
  say "    (1) create the databases   mysql -h $DB_HOST -P $DB_PORT -u <user> -p < sql/create/create_mysql.sql"
  say "    (2) auth schema            mysql -h $DB_HOST -P $DB_PORT -u <user> -p auth      < sql/base/auth_database.sql"
  say "    (3) characters schema      mysql -h $DB_HOST -P $DB_PORT -u <user> -p character < sql/base/characters_database.sql"
  say "        (printed here, NEVER run by v1; database names are yours to fill in)"
  say "        Not optional: with no schema there is nothing to start against."
else
  say "    (1-3) NOT done by v2 (you declined). The server will not start without a"
  say "          schema; you bring your own working one."
fi
say
say "    (4) world and hotfixes -- v2 PLACES the file, the CORE imports it."
if [[ -n "$DUMP_PATH" ]] && [[ "${DUMP_OK:-y}" == y ]]; then
  say "        No mysql in this step: DBUpdater.cpp:189,248 makes these two"
  say "        LOCATION_DOWNLOAD, resolved against boost::filesystem::current_path()"
  say "        at :369-381. It downloads nothing; the file must already be there."
  say "        dump: $DUMP_PATH"
  DUMP_SQL="$DUMP_PATH"
  if [[ "$DUMP_PATH" == *.7z ]]; then
    DUMP_SQL="${DUMP_PATH%.7z}.sql"
    say "        unpack first (needs p7zip-full):"
    for d in $DUMP_DIRS; do say "          7z x \"$DUMP_PATH\" -o\"$d\""; done
  fi
  say "        then place it as the service user (printed here, NEVER run by v1):"
  for d in $DUMP_DIRS; do
    DUMP_OWNER=forever-prod
    [[ "$d" == "$INSTALL_ROOT/sim/run" ]] && DUMP_OWNER=forever-sim
    say "          install -o $DUMP_OWNER -g $DUMP_OWNER -m 0640 \"$DUMP_SQL\" \"$d/\""
  done
  say "        The core imports it on that server's first start."
  say
  say "        WARNING -- the leftover file: the import only happens into an EMPTY"
  say "        database (DBUpdater.cpp:352-355 returns as soon as SHOW TABLES has"
  say "        rows), so the file is inert while the database exists. But drop that"
  say "        database one day and the stale dump is imported again with nobody"
  say "        asking. Move it out once the first start has succeeded -- step (5) in"
  say "        the closing section says exactly what to run."
else
  say "        no dump given, or declined. Those two databases stay empty and that"
  say "        server will not come up."
fi
say
say "    (5) sql/updates/<database>/master/ -- NOT the installer's job. The core"
say "        walks those per-database directories itself at startup. See below."
say
say "    Deliberately NOT in any command above:"
say "      sql/base/dev/        development samples (a world and a hotfixes database),"
say "                           kept next to a DO_NOT_IMPORT_THESE_FILES.txt warning."
say "                           Importing them would lay a sample world over the TDB dump"
say "                           in step (4). If you find them, leave them alone."
say "      sql/create/drop_mysql.sql, sql/create/drop_mysql_8.sql"
say "                           they DROP databases. Never run them on an install."
say "      sql/updates/<database>/master/"
say "                           the core's to apply, not ours (step 5)."
say
say "    -- Updates are applied by the core --"
say "    Not the installer, and deliberately so. When a database is opened the"
say "    core applies sql/base and sql/updates itself:"
say "      DatabaseLoader.cpp:27-28   reads Updates.AutoSetup (default 1, on) and"
say "                                 Updates.EnableDatabases (default 15 = all four)"
say "      DatabaseLoader.cpp:60      auto-populates an empty database"
say "      DatabaseLoader.cpp:84-85   populates/updates whenever they are enabled"
say "      DBUpdater.cpp:149,208      reads sql/base/*.sql from SourceDirectory"
say "    Because of that, v2 must not also apply those files: the same one would"
say "    run twice. The installer writes the schema and places the dump; the core"
say "    imports the dump and keeps itself updated."
say "    SourceDirectory: the shipped template leaves it empty"
say "    (worldserver.conf.dist:209-215), so it falls back to the path compiled in at"
say "    build time (revision_data.h.in.cmake:38). The service process reads it at"
say "    runtime (DBUpdater::GetBaseFile -> BuiltInConfig::GetSourceDirectory,"
say "    BuiltInConfig.cpp:42-46), so the service user needs read access to it."
say "    Whether prod/conf or sim/conf override it is UNVERIFIED: both directories"
say "    are 0750 and owned by the service users, so the planner cannot read them."
say "    v2 must check that before trusting the compiled-in path."

say
say "[7] Credentials (paths and names only)"
if [[ "${CREDS_OK:-y}" == y ]]; then
  say "    create if missing, root:<group> 0640, never overwrite:"
  [[ "$SIM_OK" == y ]] && say "      /etc/forever/credentials/sim-db.env     root:forever-sim"
  say "      /etc/forever/credentials/prod-db.env    root:forever-prod"
  say "      /etc/forever/credentials/backup-db.env  root:forever-backup"
  say "    values: written by you; the script never sees them"
else
  say "    not created; you place the files yourself"
fi

say
say "[8] Components installed"
for m in $MODS; do say "    - $(mod_name "$m")"; done

say
say "[9] systemd"
if [[ "${UNITS_OK:-y}" == y ]]; then
  say "    install (NOT enable, NOT start): $UNITS"
else
  say "    units not installed"
fi
say "    Battle.net: $BNET_STATE"
if [[ "$SIM_OK" == y ]]; then
  say "    sim limits: MemoryMax=$MEM_LIMIT, OOMScoreAdjust=500 (prod is OOMScoreAdjust=-500)"
else
  say "    sim limits: n/a (no sim in this install)"
fi
say "    prod stays off until the owner approves the start window"

say
say "[10] Firewall"
PORTS_LABELS=(
  [1]="8085/tcp prod worldserver (LAN only)"
  [2]="3724/tcp auth (legacy)"
  [3]="1119+8081/tcp Battle.net (login server go-live)"
  [4]="3306/tcp MySQL (LAN only)"
  [5]="8088/tcp dashboard (LAN only, or none if you tunnel)"
)
if [[ -z "${PORTS:-}" ]]; then
  say "    nothing opened (v1 opens nothing; this is the plan's record only)"
else
  for p in $PORTS; do say "    planned: ${PORTS_LABELS[$p]}"; done
fi

say
say "[11] Explicitly NOT done by v2"
say "    no service started or enabled; no client data downloaded; no secret"
say "    value handled; nothing deleted; nothing removed from an existing install;"
say "    Battle.net certs stay with create-bnet-certs.sh (run as your login); bot"
say "    soak runs, copy-account-to-sim.py and remove-stray-bots.py stay separate."

section "BEFORE YOU START ANYTHING -- v1 only prints this, it never runs it"

say "(1) Fill the data folders. No unit starts until these hold real files:"
for d in dbc maps vmaps mmaps gt; do say "      $INSTALL_ROOT/data/$d"; done
say "    files belong to group forever-data, mode 0640 (the setup-directories.sh rule)"
say "    they come from a WoW client: no step of this install produces them"

say
say "(2) Fill the credential files (the values are yours to write, never the script's):"
[[ "$SIM_OK" == y ]] && say "      sudoedit /etc/forever/credentials/sim-db.env"
say "      sudoedit /etc/forever/credentials/prod-db.env"
say "      sudoedit /etc/forever/credentials/backup-db.env"
say "    each needs TC_LOGIN/WORLD/CHARACTER/HOTFIX_DATABASE_INFO lines"

say
say "(3) Install the units. They are only written to disk here; nothing starts:"
say "      sudo bash $INSTALL_ROOT/src/Trinity-Forever-Modules/serverScripts/install-services.sh"
if [[ " $MODS " == *" 6 "* ]]; then
  say "      sudo bash $INSTALL_ROOT/src/forever-dashboard/deploy/install-dashboard.sh"
  say "        ORDER MATTERS: the dashboard installer is the only one of these"
  say "        that touches an existing unit -- install-dashboard.sh --remove runs"
  say "        'systemctl disable --now forever-dashboard' (install-dashboard.sh:45)."
  say "        So run it BEFORE step (4), and never run --remove afterwards."
  say "        install-services.sh has no such line: it only daemon-reloads"
  say "        (install-services.sh:130) and starts nothing."
fi

say
say "(4) Then enable the services by hand. These are the real commands:"
say "      sudo systemctl daemon-reload"
say "      sudo systemctl enable --now forever-prod"
if [[ "$SIM_OK" == y ]]; then
  say "      sudo systemctl enable --now forever-sim"
fi
if [[ " $MODS " == *" 6 "* ]]; then
  say "      sudo systemctl enable --now forever-dashboard"
  say "        # undone by a later install-dashboard.sh --remove (see step 3)"
  say "      sudo systemctl enable --now telegraf        # optional, and only if Telegraf"
  say "                                                 # is installed: the bots input comes"
  say "                                                 # from telegraf/install-telegraf-bots.sh"
fi
if [[ "$BNET_OK" == y ]]; then
  say "      (no Battle.net line on purpose: run create-bnet-certs.sh first, then"
  say "       sudo systemctl enable --now forever-prod-bnet)"
fi
say "    prod keeps Restart=no until the owner approves a restart policy."
say "    Read --now: it STARTS the unit immediately. To enable without starting,"
say "    drop it:  sudo systemctl enable forever-prod   (start it later, by hand)."
say
say "(5) After the FIRST successful start, move the dump out of the working"
say "    directory. The core imports it only into an EMPTY database"
say "    (DBUpdater.cpp:352-355 returns as soon as SHOW TABLES has rows), so the"
say "    file is inert while the database exists -- but it would be imported again"
say "    silently if that database is ever dropped and recreated."
if [[ -n "$DUMP_PATH" ]] && [[ "${DUMP_OK:-y}" == y ]]; then
  for d in ${DUMP_DIRS:-}; do
    say "      sudo mv \"$d\"/TDB_full_*.sql /var/tmp/    # anything outside $d is fine"
  done
  say "    (match the glob to whatever the file is actually called)"
fi
say
say "    v1 has printed these commands. It has not run any of them, and it has"
say "    created nothing. Start with the sim, not prod."

section "Done"
say "Nothing was installed, created or changed. This was a planning run."
say "v2 turns the answers above into actions; re-run behaviour: $IDEM."
say "Keep the plan:  sudo bash install-forever.sh | tee ~/forever-plan.txt"
