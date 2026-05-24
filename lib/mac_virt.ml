open! Core

external is_supported : unit -> bool = "vz_is_supported"

external validate_basic_config_stub
  :  int
  -> int64
  -> string option
  = "vz_validate_basic_config"

let validate_basic_config ~cpu_count ~memory_size =
  match validate_basic_config_stub cpu_count memory_size with
  | None -> Ok ()
  | Some error -> Or_error.error_string error
;;

let%expect_test "virtualization is supported on this host" =
  print_s [%sexp (is_supported () : bool)];
  [%expect {| true |}]
;;

let%expect_test "an invalid config fails validation" =
  let result = validate_basic_config ~cpu_count:1 ~memory_size:1L in
  print_s [%sexp (Result.is_error result : bool)];
  [%expect {| true |}]
;;
