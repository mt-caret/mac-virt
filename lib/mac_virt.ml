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

  let create ~path ~hardware_model = or_error_of_result (create_stub path hardware_model)
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
  external url : t -> string = "vz_restore_image_url"
  external requirements : t -> Requirements.t option = "vz_restore_image_requirements"

  let fetch_latest () = or_error_of_result (fetch_latest_stub ())
  let load ~path = or_error_of_result (load_stub path)
end

module Configuration = struct
  type t

  external create_stub : int -> int64 -> t = "vz_configuration_create"

  external create_macos_stub
    :  int
    -> int64
    -> Mac_platform.t
    -> t
    = "vz_configuration_create_macos"

  external cpu_count : t -> int = "vz_configuration_cpu_count"
  external memory_size : t -> int64 = "vz_configuration_memory_size"
  external validate_stub : t -> string option = "vz_configuration_validate"

  let create ~cpu_count ~memory_size = create_stub cpu_count memory_size

  let create_macos ~cpu_count ~memory_size ~platform =
    create_macos_stub cpu_count memory_size platform
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

  external create_unchecked : Configuration.t -> t = "vz_virtual_machine_create"
  external state_stub : t -> int = "vz_virtual_machine_state"

  let create config =
    match Configuration.validate config with
    | Error _ as error -> error
    | Ok () -> Ok (create_unchecked config)
  ;;

  let state t = State.of_int (state_stub t)
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
