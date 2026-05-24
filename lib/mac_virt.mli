(** Bindings to Apple's Virtualization framework.

    This is the M0 smoke-test surface: it proves that the OCaml -> Objective-C ->
    framework toolchain links and runs. Real VM lifecycle bindings replace and extend
    this. *)

open! Core

(** Whether this host (hardware + macOS version) can run virtual machines. Does not
    require the virtualization entitlement. *)
val is_supported : unit -> bool

(** Build a trivial machine configuration with [cpu_count] virtual CPUs and [memory_size]
    bytes of RAM, then ask the framework to validate it. Returns the validation error
    verbatim, if any.

    Validation checks the [com.apple.security.virtualization] entitlement, so an unsigned
    binary reports a missing-entitlement error here rather than a configuration error. *)
val validate_basic_config : cpu_count:int -> memory_size:int64 -> unit Or_error.t
