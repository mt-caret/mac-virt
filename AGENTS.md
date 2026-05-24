# mac-virt — agent & contributor notes

OCaml bindings to Apple's **Virtualization.framework**, used to create, install, and
boot macOS guests on Apple silicon. Written in the Jane Street style (`Core`,
`ppx_jane`, inline `%expect` tests).

This file captures *how* the binding is built and the non-obvious things about the
framework that will bite you. Read it before changing the FFI layer.

## Layout

```
lib/
  mac_virt.ml / .mli   public OCaml API (one module per VZ concept)
  vz_stubs.c           ALL the Objective-C glue (compiled as ObjC, ARC)
  dune                 foreign_stubs + framework link flags
bin/
  main.ml              CLI: `Command.group` with `install` and `boot`
  mac-virt.entitlements  the com.apple.security.virtualization entitlement
  help.txt             generated (promoted) from `mac-virt help -recursive -flags`
dune-workspace         (pkg enabled) — dune package management
dune.lock/             committed lock dir (vanilla OCaml + Core + ppx_jane + core_unix)
```

## Build / run

```sh
dune build
dune runtest          # inline %expect tests (entitlement-free paths only)
dune fmt              # ocamlformat, janestreet profile
```

### Running anything that touches a VM requires code signing

The process must carry the `com.apple.security.virtualization` entitlement or **even
`VZVirtualMachineConfiguration.validateWithError:` fails** with a missing-entitlement
error (not just VM creation/boot). `dune` marks `_build` artifacts read-only, so sign a
**copy**:

```sh
dune build
cp _build/default/bin/main.exe /tmp/mac-virt
codesign --entitlements bin/mac-virt.entitlements -s - /tmp/mac-virt
/tmp/mac-virt install -bundle ~/vm     # downloads ~20 GB IPSW + installs macOS
/tmp/mac-virt boot    -bundle ~/vm     # opens a window, boots the guest
```

There is intentionally no dune rule that signs in place — signing a build artifact
fights dune's sandboxing/read-only model. Keep signing a manual/scripted post-build step.

## Toolchain

- **Vanilla OCaml**, not OxCaml — even though the machine's system compiler may be
  `+ox`. `dune-workspace` is just `(lang dune 3.x)` + `(pkg enabled)` (no oxcaml
  repository); `dune pkg lock` provisions a vanilla compiler into `dune.lock/`.
- The lock dir is **committed** (portable lock dirs). Re-lock with `dune pkg lock` after
  changing `(depends ...)` in `dune-project`.

## FFI architecture

Three layers: `Mac_virt` (typed OCaml) → C glue (`value` ⇄ C, runtime lock) →
Objective-C calls into `VZ*`. The framework is **Objective-C native**, so the glue is
plain ObjC compiled by clang — no Swift runtime. See `lib/dune`:

```
(foreign_stubs (language c) (names vz_stubs) (flags (-x objective-c -fobjc-arc)))
(c_library_flags (-framework Virtualization -framework Foundation -framework AppKit))
```

`.c` (not `.m`) + `-x objective-c` is how dune foreign stubs compile Objective-C. `-fobjc-arc`
turns on ARC. AppKit is needed for the GUI (`VZVirtualMachineView`, `NSApplication`).

### Holding ObjC objects in OCaml

`VZ*` objects live in OCaml as **custom blocks** wrapping the `id`:

- `vz_alloc(id)` stores `CFBridgingRetain(obj)` (a +1 reference owned outside ARC) in a
  custom block; the finalizer does `CFBridgingRelease`.
- `vz_unwrap(value)` returns `(__bridge id)` — borrows, no retain count change.
- Each VZ class is an abstract `type t` in its OCaml module; at the C level they are all
  the same custom block, so the OCaml types are the only thing keeping them distinct.

A **`VZVirtualMachine` is special**: it's stored in a `{ void *vm; void *queue; }` custom
block (`vz_vm`) because the machine is confined to a serial dispatch queue and every call
must run on it.

### Value constructors in C

- `vz_block1(tag, v)` builds a 1-field block: `Some`/`Ok` are tag 0, `Error` is tag 1,
  `None` is `Val_int(0)`. OCaml `option` and `result` have exactly this representation.
- Functions that can fail return `(t, string) result` (C builds it) and OCaml maps it to
  `Or_error.t` via `or_error_of_result`. Functions that may be absent return `t option`.
- NSData ⇄ string: `caml_alloc_initialized_string(len, bytes)` and
  `[NSData dataWithBytes:String_val(s) length:caml_string_length(s)]`.
- Reading an OCaml list in C (e.g. storage devices → `NSArray`):
  `for (value l = v; Is_block(l); l = Field(l, 1)) ... Field(l, 0) ...` (`[]` is `Val_int(0)`).

### Async bridging (no OCaml callbacks on foreign threads yet)

Several framework calls are asynchronous (completion handlers): restore-image
fetch/load, install, IPSW download. We bridge them **synchronously** with a
`dispatch_semaphore_t`: kick off the async call, `dispatch_semaphore_wait`, and have the
completion handler signal it. The handlers touch only C/ObjC state, so we never call back
into the OCaml runtime from a foreign thread (no `caml_c_thread_register` needed — yet).

Long waits (install, download, fetch) **release the OCaml runtime lock** around the wait
with `caml_enter_blocking_section()` / `caml_leave_blocking_section()` (`<caml/threads.h>`).
Never touch an OCaml `value` between enter/leave; do all OCaml allocation after leaving.

## Threading & the run-loop problem (read this before touching boot/GUI)

`VZVirtualMachine` is **dispatch-queue confined**: it must be created on a queue and every
method call + delegate callback happens on that queue. Two modes:

- **Headless / install** (`vz_virtual_machine_create`): create on a *private* serial queue
  (`initWithConfiguration:queue:`). Issue work with `dispatch_async`; read `state` with
  `dispatch_sync`. The VM runs as long as libdispatch services the queue and the process is
  alive — **no run loop required** (the install proved this: we blocked `main` on a
  semaphore while the VM ran the installer on its private queue). Lifecycle is exposed as
  `Virtual_machine.start` / `stop` (force power-off) / `request_stop` (graceful) — each
  dispatched onto the VM's queue and bridged to a synchronous `unit Or_error.t` via a
  semaphore, guarded by `canStart`/`canStop`/`canRequestStop` so misuse returns an error
  instead of raising. The `run` CLI subcommand drives this headlessly; verified from the
  shell: start → `Running`, hold, force-stop → `Stopped`. Waiting for the guest to stop is
  **delegate-driven, not polled**: `vz_virtual_machine_create` attaches a
  `VZVirtualMachineDelegate` (`MacVirtHeadlessDelegate`, kept alive in the `vz_vm` block)
  that signals a semaphore on `guestDidStopVirtualMachine:` / `didStopWithError:`.
  `Virtual_machine.wait_for_stop ~timeout_seconds` blocks on that semaphore (runtime lock
  released), returning ``` `Stopped ``` / ``` `Timed_out ``` / an `Error`. The C stub returns
  a 3-way OCaml value whose representation is hand-matched to a `raw_stop` variant (`Val_int 0`
  = clean, `Val_int 1` = timed out, a tag-0 block = error). This powers the graceful stop
  (`request_stop` then `wait_for_stop`, force-off fallback on timeout) and a `run` mode with
  no `-seconds` that blocks until the guest powers itself off.

- **GUI** (`vz_boot_gui`): AppKit requires the **main thread** running `NSApplication`, and
  `VZVirtualMachineView` wants the VM on the **main queue**. So the VM is created with
  `initWithConfiguration:` (no `queue:` → main queue), and the **ObjC stub owns the run
  loop**: it sets up `NSApp`, builds the window + view, starts the VM, and calls
  `[NSApp run]` (blocking). This is why GUI boot is one self-contained blocking call that
  takes a `Configuration.t` rather than reusing the headless `Virtual_machine.t`.

If you ever need OCaml callbacks delivered from the framework's queues, you'll have to
`caml_c_thread_register` the calling thread and acquire the runtime lock before
`caml_callback`. We avoid that entirely today via the semaphore bridge.

## Virtualization.framework gotchas (hard-won)

- **A macOS config with no graphics device boots to a BLACK window.** `validate` passes
  and the VM *runs* without a `VZMacGraphicsDeviceConfiguration`; the installer doesn't
  need one either. But `VZVirtualMachineView` has no framebuffer to show. `create_macos`
  now always adds a display + `VZUSBKeyboardConfiguration` +
  `VZUSBScreenCoordinatePointingDeviceConfiguration`. Corollary: "VM started, no delegate
  error" does **not** prove a visible desktop — only eyes on the window do.
- **`VZMacHardwareModel` can only come from a restore image**, never constructed from
  nothing. `+[VZMacOSRestoreImage fetchLatestSupportedWithCompletionHandler:]` returns one
  over the network *without* downloading the IPSW.
- **To boot a bundle later you must persist the platform identity.** `install` writes
  `hardware_model.bin` + `machine_identifier.bin` into the bundle; `boot` reconstructs from
  them. The machine identifier is random per install and unrecoverable if lost (a fresh one
  still boots an installed guest, but the guest sees "new hardware").
- **`VZMacOSInstaller` needs a *local* `.ipsw`** (`initWithVirtualMachine:restoreImageURL:`
  — the `restoringFromImageAt:` spelling is only the Swift `NS_SWIFT_NAME`). The
  `fetchLatest` URL is a remote download link, not directly installable, hence the
  downloader.
- **`VZMacOSInstaller.progress.fractionCompleted` plateaus ~90%** then the completion
  handler fires; progress is not linear. Treat the completion handler as authoritative.
- **`NSURLSession` download progress:** use `task.countOfBytesReceived` /
  `countOfBytesExpectedToReceive`; the `NSProgress.completedUnitCount` / `totalUnitCount`
  read 0 for URLSession tasks even while `fractionCompleted` advances.
- **`VZVirtualMachine.delegate` is `weak`** — keep a strong reference alive yourself for the
  duration of the run.
- **Graceful shutdown:** window-close calls `requestStopWithError:` (a power-button request)
  and waits for the delegate's `guestDidStopVirtualMachine:` before quitting, with a **90s**
  safety timeout that force-quits otherwise. `stopWithCompletionHandler:` / `[NSApp
  terminate:]` is the "pull the plug" path. **Why 90s (learned against a real Tahoe 26.0
  guest):** on a logged-in macOS guest the power request surfaces the standard *"Are you sure
  you want to shut down? … automatically in 60 seconds"* dialog; with the window already
  closing there's nobody to confirm, so the guest doesn't power off until that ~60s countdown
  elapses (plus a few seconds to actually shut down). The timeout must sit comfortably past
  that, so an earlier 30s value force-quit every time. A guest in **Setup Assistant** ignores
  the request entirely and still hits the timeout. The cleanest path remains a
  **guest-initiated** shutdown (Apple menu → Shut Down), which powers off, fires
  `guestDidStopVirtualMachine:`, and exits immediately — verified.
- **Input-device fidelity:** with `VZUSBKeyboardConfiguration`, *fast* synthetic keystrokes
  (e.g. an automation tool typing a whole string at once) drop characters — observed typing
  "Calculator" into the guest and having only "a" register. Mouse/pointer input is reliable.
  If you need dependable automated keyboard input, type slowly (per-keystroke with delays)
  or try `VZMacKeyboardConfiguration` instead. Confirmed by driving the guest's Spotlight via
  computer-use against a real running guest (macOS Tahoe 26.0, "Apple Virtual Machine").

## VM bundle layout

A bundle is a directory produced by `install`:

```
aux.bin                 VZMacAuxiliaryStorage (NVRAM), tied to the hardware model
disk.img                main disk, a sparse file (ftruncate to size; grows as used)
hardware_model.bin      VZMacHardwareModel.dataRepresentation
machine_identifier.bin  VZMacMachineIdentifier.dataRepresentation
restore.ipsw            the downloaded restore image (only needed to (re)install)
```

## Conventions

- `open! Core`; `.mli` for every non-test `.ml`; doc comments live in the `.mli`.
- Predictable failures thread `Or_error.t`; `let%bind.Or_error` / `let%map.Or_error`.
- Smart constructors return `Or_error.t` / `option`; types are abstract so invariants hold
  (e.g. a `Virtual_machine.t` only comes from a validated config).
- Tests are inline `%expect_test`s in `lib/`. They can only cover entitlement-free paths
  (`is_supported`, data round-trips, disk/storage creation); anything that validates a
  config, creates/boots a VM, or installs needs a signed binary and is exercised manually.

## Status

M1 (install a macOS guest and boot it into an interactive window) is **done and visually
verified**, as is headless `start`/`stop`/`request_stop` plus a delegate-driven
`wait_for_stop` (the `run` subcommand, including a run-until-the-guest-shuts-down mode).
Natural next steps: a richer **state stream** (KVO on `VZVirtualMachine.state` for every
transition, not just stop), configurable display/resolution, network modes, shared folders,
and replacing the blocking semaphores with `Async` integration.
