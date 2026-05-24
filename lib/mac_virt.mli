(** Bindings to Apple's Virtualization framework.

    Operations that touch a live virtual machine — validating a configuration, creating or
    starting a machine, installing macOS — require the host process to carry the
    [com.apple.security.virtualization] entitlement. An unsigned binary gets a
    missing-entitlement error back from {!Configuration.validate}. *)

open! Core

(** Whether this host (hardware + macOS version) can run virtual machines. Does not
    require the entitlement. *)
val is_supported : unit -> bool

(** A model of the virtual Mac hardware. It is opaque data obtained from a
    {!Restore_image}; it cannot be conjured from nothing. *)
module Hardware_model : sig
  type t

  val of_data : string -> t option
  val to_data : t -> string
  val is_supported : t -> bool
end

(** A unique identity for a virtual Mac, persisted as part of a VM bundle so the guest
    sees stable hardware across boots. *)
module Machine_identifier : sig
  type t

  val create : unit -> t
  val of_data : string -> t option
  val to_data : t -> string
end

(** The auxiliary (NVRAM) storage backing a macOS VM, created on disk for a given
    {!Hardware_model}. *)
module Auxiliary_storage : sig
  type t

  (** Create a new auxiliary storage file at [path]. Fails if [path] already exists. *)
  val create : path:string -> hardware_model:Hardware_model.t -> t Or_error.t

  (** Open the existing auxiliary storage at [path], for booting a bundle that was already
      created. *)
  val load : path:string -> t
end

(** The main disk image backing a VM. *)
module Disk_image : sig
  (** Create a new, empty sparse disk image of [size_bytes] at [path]. Fails if [path]
      already exists. *)
  val create : path:string -> size_bytes:int64 -> unit Or_error.t
end

(** The Mac-specific platform of a configuration: which hardware model, which machine
    identity, and where its NVRAM lives. *)
module Mac_platform : sig
  type t

  val create
    :  hardware_model:Hardware_model.t
    -> machine_identifier:Machine_identifier.t
    -> auxiliary_storage:Auxiliary_storage.t
    -> t
end

(** A macOS restore image (an [.ipsw]). The hardware model and resource minimums needed to
    build a configuration come from here. *)
module Restore_image : sig
  type t

  module Requirements : sig
    type t

    val hardware_model : t -> Hardware_model.t
    val minimum_cpu_count : t -> int
    val minimum_memory_size : t -> int64
  end

  (** Fetch metadata for the latest restore image Apple supports on this host. Makes a
      network request but does not download the image itself. *)
  val fetch_latest : unit -> t Or_error.t

  (** Load a restore image from a local [.ipsw] file. *)
  val load : path:string -> t Or_error.t

  (** Download this image to a local file at [path], overwriting any existing file. Blocks
      until the download finishes — the image is several gigabytes — printing progress to
      stderr. *)
  val download : t -> path:string -> unit Or_error.t

  (** Where the image can be downloaded from. *)
  val url : t -> string

  (** The most capable configuration this image supports on this host, if any. *)
  val requirements : t -> Requirements.t option
end

(** A storage device exposed to the guest. *)
module Storage_device : sig
  type t

  (** A Virtio block device backed by the disk image at [path], which must already exist. *)
  val disk_image : path:string -> read_only:bool -> t Or_error.t
end

module Configuration : sig
  (** A [VZVirtualMachineConfiguration]: a description of the hardware a virtual machine
      should have. *)
  type t

  (** A bare configuration with no platform or boot loader; on its own it will not pass
      {!validate}. *)
  val create : cpu_count:int -> memory_size:int64 -> t

  (** A configuration for booting macOS: the given platform and storage plus a macOS boot
      loader. *)
  val create_macos
    :  cpu_count:int
    -> memory_size:int64
    -> platform:Mac_platform.t
    -> storage_devices:Storage_device.t list
    -> t

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

(** Installs macOS into a virtual machine's storage from a restore image. *)
module Installer : sig
  (** Install macOS onto [virtual_machine]'s storage from the local [.ipsw] at
      [restore_image_path]. Blocks until installation completes — which can take many
      minutes — printing progress to stderr. *)
  val install
    :  virtual_machine:Virtual_machine.t
    -> restore_image_path:string
    -> unit Or_error.t
end

(** Boots a configuration in an on-screen window. *)
module Gui : sig
  (** Create and start a virtual machine from [config], show it in a window titled
      [title], and run the AppKit event loop until the window is closed (which exits the
      process). Must be called on the program's main thread, and returns only if the
      configuration is invalid. *)
  val boot : Configuration.t -> title:string -> unit Or_error.t
end
