open! Core

let bytes_per_gib = 1024 * 1024 * 1024

let run ~bundle ~ipsw ~disk_size_gib ~requested_cpu_count ~requested_memory_gib ~install =
  let%bind.Or_error () = Or_error.try_with (fun () -> Core_unix.mkdir_p bundle) in
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
  let aux_path = Filename.concat bundle "aux.bin" in
  let disk_path = Filename.concat bundle "disk.img" in
  let%bind.Or_error auxiliary_storage =
    Mac_virt.Auxiliary_storage.create ~path:aux_path ~hardware_model
  in
  let%bind.Or_error () =
    Mac_virt.Disk_image.create
      ~path:disk_path
      ~size_bytes:(Int64.of_int (disk_size_gib * bytes_per_gib))
  in
  let%bind.Or_error storage =
    Mac_virt.Storage_device.disk_image ~path:disk_path ~read_only:false
  in
  let platform =
    Mac_virt.Mac_platform.create
      ~hardware_model
      ~machine_identifier:(Mac_virt.Machine_identifier.create ())
      ~auxiliary_storage
  in
  let cpu_count =
    let minimum = Mac_virt.Restore_image.Requirements.minimum_cpu_count requirements in
    match requested_cpu_count with
    | Some n -> Int.max n minimum
    | None -> minimum
  in
  let memory_size =
    let minimum = Mac_virt.Restore_image.Requirements.minimum_memory_size requirements in
    match requested_memory_gib with
    | Some gib -> Int64.max (Int64.of_int (gib * bytes_per_gib)) minimum
    | None -> minimum
  in
  let config =
    Mac_virt.Configuration.create_macos
      ~cpu_count
      ~memory_size
      ~platform
      ~storage_devices:[ storage ]
  in
  let%bind.Or_error vm = Mac_virt.Virtual_machine.create config in
  match install with
  | false ->
    print_s
      [%message
        "assembled a macOS configuration and instantiated the machine"
          ~bundle
          (cpu_count : int)
          (memory_size : int64)
          ~state:(Mac_virt.Virtual_machine.state vm : Mac_virt.Virtual_machine.State.t)];
    Ok ()
  | true ->
    let%bind.Or_error restore_image_path =
      match ipsw with
      | Some path -> Ok path
      | None ->
        let path = Filename.concat bundle "restore.ipsw" in
        print_s [%message "downloading latest restore image" (path : string)];
        let%map.Or_error () = Mac_virt.Restore_image.download image ~path in
        path
    in
    print_s [%message "installing macOS" ~bundle (cpu_count : int) (memory_size : int64)];
    let%map.Or_error () =
      Mac_virt.Installer.install ~virtual_machine:vm ~restore_image_path
    in
    print_endline "macOS installed."
;;

let command =
  Command.basic
    ~summary:"Build a macOS virtual machine bundle, optionally installing macOS into it"
    (let%map_open.Command bundle =
       flag "bundle" (required string) ~doc:"DIR directory to hold the VM bundle"
     and ipsw =
       flag
         "ipsw"
         (optional string)
         ~doc:"PATH local restore image (default: fetch the latest supported)"
     and disk_size_gib =
       flag "disk-size-gib" (optional_with_default 64 int) ~doc:"N main disk size in GiB"
     and requested_cpu_count =
       flag "cpu-count" (optional int) ~doc:"N virtual CPUs (default: the image minimum)"
     and requested_memory_gib =
       flag "memory-gib" (optional int) ~doc:"N RAM in GiB (default: the image minimum)"
     and install =
       flag
         "install"
         no_arg
         ~doc:
           " install macOS into the bundle (downloads the latest image if --ipsw is \
            omitted)"
     in
     fun () ->
       print_s
         [%message "virtualization support" ~supported:(Mac_virt.is_supported () : bool)];
       match
         run
           ~bundle
           ~ipsw
           ~disk_size_gib
           ~requested_cpu_count
           ~requested_memory_gib
           ~install
       with
       | Ok () -> ()
       | Error e -> print_s [%message "failed" (e : Error.t)])
;;

let () = Command_unix.run command
