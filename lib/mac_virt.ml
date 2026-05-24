open! Core

external is_supported : unit -> bool = "vz_is_supported"

let or_error_of_result : ('a, string) Result.t -> 'a Or_error.t = function
  | Ok x -> Ok x
  | Error message -> Or_error.error_string message
;;

module Hardware_model = struct
  type t

  external of_data : string -> t option = "vz_hardware_model_of_data"
  external to_data : t -> string = "vz_hardware_model_to_data"
  external is_supported : t -> bool = "vz_hardware_model_is_supported"
end

module Machine_identifier = struct
  type t

  external create : unit -> t = "vz_machine_identifier_create"
  external of_data : string -> t option = "vz_machine_identifier_of_data"
  external to_data : t -> string = "vz_machine_identifier_to_data"
end

module Auxiliary_storage = struct
  type t

  external create_stub
    :  string
    -> Hardware_model.t
    -> (t, string) Result.t
    = "vz_auxiliary_storage_create"

  external load_stub : string -> t = "vz_auxiliary_storage_load"

  let create ~path ~hardware_model = or_error_of_result (create_stub path hardware_model)
  let load ~path = load_stub path
end

module Disk_image = struct
  let create ~path ~size_bytes =
    Or_error.try_with (fun () ->
      let fd =
        Core_unix.openfile path ~mode:Core_unix.[ O_WRONLY; O_CREAT; O_EXCL ] ~perm:0o644
      in
      Exn.protect
        ~f:(fun () -> Core_unix.ftruncate fd ~len:size_bytes)
        ~finally:(fun () -> Core_unix.close fd))
  ;;
end

module Mac_platform = struct
  type t

  external create_stub
    :  Hardware_model.t
    -> Machine_identifier.t
    -> Auxiliary_storage.t
    -> t
    = "vz_mac_platform_create"

  let create ~hardware_model ~machine_identifier ~auxiliary_storage =
    create_stub hardware_model machine_identifier auxiliary_storage
  ;;
end

module Restore_image = struct
  type t

  module Requirements = struct
    type t

    external hardware_model : t -> Hardware_model.t = "vz_requirements_hardware_model"
    external minimum_cpu_count : t -> int = "vz_requirements_minimum_cpu_count"
    external minimum_memory_size : t -> int64 = "vz_requirements_minimum_memory_size"
  end

  external fetch_latest_stub
    :  unit
    -> (t, string) Result.t
    = "vz_restore_image_fetch_latest"

  external load_stub : string -> (t, string) Result.t = "vz_restore_image_load"
  external download_stub : t -> string -> string option = "vz_restore_image_download"
  external url : t -> string = "vz_restore_image_url"
  external requirements : t -> Requirements.t option = "vz_restore_image_requirements"

  let fetch_latest () = or_error_of_result (fetch_latest_stub ())
  let load ~path = or_error_of_result (load_stub path)

  let download t ~path =
    match download_stub t path with
    | None -> Ok ()
    | Some message -> Or_error.error_string message
  ;;
end

module Storage_device = struct
  type t

  external disk_image_stub
    :  string
    -> bool
    -> (t, string) Result.t
    = "vz_storage_device_disk_image"

  let disk_image ~path ~read_only = or_error_of_result (disk_image_stub path read_only)
end

module Configuration = struct
  type t

  external create_stub : int -> int64 -> t = "vz_configuration_create"

  external create_macos_stub
    :  int
    -> int64
    -> Mac_platform.t
    -> Storage_device.t list
    -> t
    = "vz_configuration_create_macos"

  external cpu_count : t -> int = "vz_configuration_cpu_count"
  external memory_size : t -> int64 = "vz_configuration_memory_size"
  external validate_stub : t -> string option = "vz_configuration_validate"

  let create ~cpu_count ~memory_size = create_stub cpu_count memory_size

  let create_macos ~cpu_count ~memory_size ~platform ~storage_devices =
    create_macos_stub cpu_count memory_size platform storage_devices
  ;;

  let validate t =
    match validate_stub t with
    | None -> Ok ()
    | Some message -> Or_error.error_string message
  ;;
end

module Virtual_machine = struct
  module State = struct
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

    let of_int = function
      | 0 -> Stopped
      | 1 -> Running
      | 2 -> Paused
      | 3 -> Error
      | 4 -> Starting
      | 5 -> Pausing
      | 6 -> Resuming
      | 7 -> Stopping
      | 8 -> Saving
      | 9 -> Restoring
      | other -> raise_s [%message "unknown VZVirtualMachineState" (other : int)]
    ;;
  end

  type t

  (* Representation must match [vz_virtual_machine_wait_for_stop]: [Stopped_clean] is
     [Val_int 0], [Timed_out] is [Val_int 1], [Stopped_with_error] is a block with tag 0. *)
  type raw_stop =
    | Stopped_clean
    | Timed_out
    | Stopped_with_error of string

  external create_unchecked : Configuration.t -> t = "vz_virtual_machine_create"
  external state_stub : t -> int = "vz_virtual_machine_state"
  external start_stub : t -> string option = "vz_virtual_machine_start"
  external stop_stub : t -> string option = "vz_virtual_machine_stop"
  external request_stop_stub : t -> string option = "vz_virtual_machine_request_stop"
  external wait_for_stop_stub : t -> int -> raw_stop = "vz_virtual_machine_wait_for_stop"

  let create config =
    match Configuration.validate config with
    | Error _ as error -> error
    | Ok () -> Ok (create_unchecked config)
  ;;

  let state t = State.of_int (state_stub t)

  let unit_or_error = function
    | None -> Ok ()
    | Some message -> Or_error.error_string message
  ;;

  let start t = unit_or_error (start_stub t)
  let stop t = unit_or_error (stop_stub t)
  let request_stop t = unit_or_error (request_stop_stub t)

  let wait_for_stop t ~timeout_seconds =
    match wait_for_stop_stub t timeout_seconds with
    | Stopped_clean -> Ok `Stopped
    | Timed_out -> Ok `Timed_out
    | Stopped_with_error message -> Or_error.error_string message
  ;;
end

module Installer = struct
  external install_stub
    :  Virtual_machine.t
    -> string
    -> string option
    = "vz_installer_install"

  let install ~virtual_machine ~restore_image_path =
    match install_stub virtual_machine restore_image_path with
    | None -> Ok ()
    | Some message -> Or_error.error_string message
  ;;
end

module Gui = struct
  external boot_stub : Configuration.t -> string -> unit = "vz_boot_gui"

  let boot config ~title =
    match Configuration.validate config with
    | Error _ as error -> error
    | Ok () ->
      boot_stub config title;
      Ok ()
  ;;
end

let%expect_test "virtualization is supported on this host" =
  print_s [%sexp (is_supported () : bool)];
  [%expect {| true |}]
;;

let%expect_test "configuration round-trips cpu count and memory size" =
  let config =
    Configuration.create ~cpu_count:2 ~memory_size:(Int64.of_int (4 * 1024 * 1024 * 1024))
  in
  print_s
    [%message
      ""
        ~cpu_count:(Configuration.cpu_count config : int)
        ~memory_size:(Configuration.memory_size config : int64)];
  [%expect {| ((cpu_count 2) (memory_size 4294967296)) |}]
;;

let%expect_test "an incomplete configuration fails validation" =
  let config = Configuration.create ~cpu_count:1 ~memory_size:1L in
  print_s [%sexp (Result.is_error (Configuration.validate config) : bool)];
  [%expect {| true |}]
;;

let%expect_test "machine identifier round-trips through its data representation" =
  let id = Machine_identifier.create () in
  let data = Machine_identifier.to_data id in
  let round_trips =
    match Machine_identifier.of_data data with
    | None -> false
    | Some id' -> String.equal (Machine_identifier.to_data id') data
  in
  print_s [%sexp (round_trips : bool)];
  [%expect {| true |}]
;;

let%expect_test "hardware model rejects invalid data" =
  print_s [%sexp (Option.is_none (Hardware_model.of_data "not a hardware model") : bool)];
  [%expect {| true |}]
;;

let%expect_test "loading a missing restore image fails" =
  print_s
    [%sexp (Result.is_error (Restore_image.load ~path:"/does/not/exist.ipsw") : bool)];
  [%expect {| true |}]
;;

let%expect_test "disk image is created at the requested size" =
  let path = Filename_unix.temp_file "mac_virt" ".img" in
  Core_unix.unlink path;
  let size_bytes = Int64.of_int (64 * 1024 * 1024) in
  let created = Disk_image.create ~path ~size_bytes in
  let actual_size = (Core_unix.stat path).Core_unix.st_size in
  Core_unix.unlink path;
  print_s
    [%message
      "" ~created:(Result.is_ok created : bool) (actual_size : int64) (size_bytes : int64)];
  [%expect {| ((created true) (actual_size 67108864) (size_bytes 67108864)) |}]
;;

let%expect_test "a storage device can be attached to a disk image" =
  let path = Filename_unix.temp_file "mac_virt" ".img" in
  Core_unix.unlink path;
  let result =
    let%bind.Or_error () =
      Disk_image.create ~path ~size_bytes:(Int64.of_int (64 * 1024 * 1024))
    in
    let%map.Or_error (_ : Storage_device.t) =
      Storage_device.disk_image ~path ~read_only:false
    in
    ()
  in
  Core_unix.unlink path;
  print_s [%sexp (Result.is_ok result : bool)];
  [%expect {| true |}]
;;
