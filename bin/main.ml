open! Core

let () =
  print_s [%message "virtualization support" ~supported:(Mac_virt.is_supported () : bool)];
  match Mac_virt.validate_basic_config ~cpu_count:1 ~memory_size:1L with
  | Ok () -> print_endline "empty config validated (unexpected)"
  | Error e ->
    print_s [%message "validation rejected the config (expected)" (e : Error.t)]
;;
