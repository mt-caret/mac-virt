open! Core

let bytes_per_gib = 1024 * 1024 * 1024

let build_config ~ipsw ~aux_storage ~requested_cpu_count ~requested_memory_gib =
  let%bind.Or_error image =
    match ipsw with
    | Some path -> Mac_virt.Restore_image.load ~path
    | None -> Mac_virt.Restore_image.fetch_latest ()
  in
  let%bind.Or_error requirements =
    match Mac_virt.Restore_image.requirements image with
    | Some requirements -> Ok requirements
    | None -> Or_error.error_string "restore image is not supported on this host"
  in
  let hardware_model = Mac_virt.Restore_image.Requirements.hardware_model requirements in
  let%map.Or_error auxiliary_storage =
    Mac_virt.Auxiliary_storage.create ~path:aux_storage ~hardware_model
  in
  let platform =
    Mac_virt.Mac_platform.create
      ~hardware_model
      ~machine_identifier:(Mac_virt.Machine_identifier.create ())
      ~auxiliary_storage
  in
  let minimum_cpu_count =
    Mac_virt.Restore_image.Requirements.minimum_cpu_count requirements
  in
  let minimum_memory_size =
    Mac_virt.Restore_image.Requirements.minimum_memory_size requirements
  in
  let cpu_count =
    match requested_cpu_count with
    | Some n -> Int.max n minimum_cpu_count
    | None -> minimum_cpu_count
  in
  let memory_size =
    match requested_memory_gib with
    | Some gib -> Int64.max (Int64.of_int (gib * bytes_per_gib)) minimum_memory_size
    | None -> minimum_memory_size
  in
  Mac_virt.Configuration.create_macos ~cpu_count ~memory_size ~platform
;;

let command =
  Command.basic
    ~summary:"Assemble and validate a macOS virtual machine configuration"
    (let%map_open.Command ipsw =
       flag
         "ipsw"
         (optional string)
         ~doc:"PATH local restore image (default: fetch the latest supported)"
     and aux_storage =
       flag
         "aux-storage"
         (required string)
         ~doc:"PATH where to create the NVRAM auxiliary storage file"
     and requested_cpu_count =
       flag "cpu-count" (optional int) ~doc:"N virtual CPUs (default: the image minimum)"
     and requested_memory_gib =
       flag "memory-gib" (optional int) ~doc:"N RAM in GiB (default: the image minimum)"
     in
     fun () ->
       print_s
         [%message "virtualization support" ~supported:(Mac_virt.is_supported () : bool)];
       let result =
         let%bind.Or_error config =
           build_config ~ipsw ~aux_storage ~requested_cpu_count ~requested_memory_gib
         in
         let%map.Or_error () = Mac_virt.Configuration.validate config in
         config
       in
       match result with
       | Ok config ->
         print_s
           [%message
             "assembled a valid macOS configuration"
               ~cpu_count:(Mac_virt.Configuration.cpu_count config : int)
               ~memory_size:(Mac_virt.Configuration.memory_size config : int64)]
       | Error e -> print_s [%message "could not assemble configuration" (e : Error.t)])
;;

let () = Command_unix.run command
