open! Core

external is_supported : unit -> bool = "vz_is_supported"

module Configuration = struct
  type t

  external create_stub : int -> int64 -> t = "vz_configuration_create"
  external cpu_count : t -> int = "vz_configuration_cpu_count"
  external memory_size : t -> int64 = "vz_configuration_memory_size"
  external validate_stub : t -> string option = "vz_configuration_validate"

  let create ~cpu_count ~memory_size = create_stub cpu_count memory_size

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

let%expect_test "validating a configuration without a boot loader fails" =
  let config = Configuration.create ~cpu_count:1 ~memory_size:1L in
  print_s [%sexp (Result.is_error (Configuration.validate config) : bool)];
  [%expect {| true |}]
;;
