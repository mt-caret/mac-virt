(** Bindings to Apple's Virtualization framework.

    Operations that touch a live virtual machine — validating a configuration, creating or
    starting a machine — require the host process to carry the
    [com.apple.security.virtualization] entitlement. An unsigned binary gets a
    missing-entitlement error back from {!Configuration.validate}. *)

open! Core

(** Whether this host (hardware + macOS version) can run virtual machines. Does not
    require the entitlement. *)
val is_supported : unit -> bool

module Configuration : sig
  (** A [VZVirtualMachineConfiguration]: a description of the hardware a virtual machine
      should have. *)
  type t

  val create : cpu_count:int -> memory_size:int64 -> t
  val cpu_count : t -> int
  val memory_size : t -> int64

  (** Ask the framework to validate the configuration as a coherent whole. *)
  val validate : t -> unit Or_error.t
end

module Virtual_machine : sig
  module State : sig
    (** Mirrors [VZVirtualMachineState]. *)
    type t =
      | Stopped
      | Running
      | Paused
      | Error
      | Starting
      | Pausing
      | Resuming
      | Stopping
      | Saving
      | Restoring
    [@@deriving compare, equal, sexp_of]
  end

  type t

  (** Create a virtual machine from a configuration. The configuration is validated first,
      so a [t] always originates from a valid configuration. *)
  val create : Configuration.t -> t Or_error.t

  (** The machine's current run state. *)
  val state : t -> State.t
end
