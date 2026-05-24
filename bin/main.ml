open! Core

let bytes_per_gib = 1024 * 1024 * 1024

(* Files within a VM bundle. *)
let aux_storage_file bundle = Filename.concat bundle "aux.bin"
let disk_image_file bundle = Filename.concat bundle "disk.img"
let hardware_model_file bundle = Filename.concat bundle "hardware_model.bin"
let machine_identifier_file bundle = Filename.concat bundle "machine_identifier.bin"
let restore_image_file bundle = Filename.concat bundle "restore.ipsw"

let resolve_cpu_count ~requested ~minimum =
  match requested with
  | Some n -> Int.max n minimum
  | None -> minimum
;;

let resolve_memory_size ~requested_gib ~minimum =
  match requested_gib with
  | Some gib -> Int64.max (Int64.of_int (gib * bytes_per_gib)) minimum
  | None -> minimum
;;

let install ~bundle ~ipsw ~disk_size_gib ~requested_cpu_count ~requested_memory_gib =
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
  let machine_identifier = Mac_virt.Machine_identifier.create () in
  (* Persist the platform identity so the bundle can be booted later. *)
  Out_channel.write_all
    (hardware_model_file bundle)
    ~data:(Mac_virt.Hardware_model.to_data hardware_model);
  Out_channel.write_all
    (machine_identifier_file bundle)
    ~data:(Mac_virt.Machine_identifier.to_data machine_identifier);
  let%bind.Or_error auxiliary_storage =
    Mac_virt.Auxiliary_storage.create ~path:(aux_storage_file bundle) ~hardware_model
  in
  let disk_path = disk_image_file bundle in
  let%bind.Or_error () =
    Mac_virt.Disk_image.create
      ~path:disk_path
      ~size_bytes:(Int64.of_int (disk_size_gib * bytes_per_gib))
  in
  let%bind.Or_error storage =
    Mac_virt.Storage_device.disk_image ~path:disk_path ~read_only:false
  in
  let platform =
    Mac_virt.Mac_platform.create ~hardware_model ~machine_identifier ~auxiliary_storage
  in
  let cpu_count =
    resolve_cpu_count
      ~requested:requested_cpu_count
      ~minimum:(Mac_virt.Restore_image.Requirements.minimum_cpu_count requirements)
  in
  let memory_size =
    resolve_memory_size
      ~requested_gib:requested_memory_gib
      ~minimum:(Mac_virt.Restore_image.Requirements.minimum_memory_size requirements)
  in
  let config =
    Mac_virt.Configuration.create_macos
      ~cpu_count
      ~memory_size
      ~platform
      ~storage_devices:[ storage ]
  in
  let%bind.Or_error vm = Mac_virt.Virtual_machine.create config in
  let%bind.Or_error restore_image_path =
    match ipsw with
    | Some path -> Ok path
    | None ->
      let path = restore_image_file bundle in
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

let read_file_if_present path =
  match Stdlib.Sys.file_exists path with
  | true -> Some (In_channel.read_all path)
  | false -> None
;;

(* The hardware model is normally saved at install time; for bundles created before that
   was the case, re-derive it from the latest restore image. *)
let load_hardware_model ~bundle =
  match read_file_if_present (hardware_model_file bundle) with
  | Some data ->
    (match Mac_virt.Hardware_model.of_data data with
     | Some hardware_model -> Ok hardware_model
     | None -> Or_error.error_string "saved hardware model is invalid")
  | None ->
    let%bind.Or_error image = Mac_virt.Restore_image.fetch_latest () in
    (match Mac_virt.Restore_image.requirements image with
     | None -> Or_error.error_string "restore image is not supported on this host"
     | Some requirements ->
       let hardware_model =
         Mac_virt.Restore_image.Requirements.hardware_model requirements
       in
       Out_channel.write_all
         (hardware_model_file bundle)
         ~data:(Mac_virt.Hardware_model.to_data hardware_model);
       Ok hardware_model)
;;

let load_machine_identifier ~bundle =
  let create_and_save () =
    let machine_identifier = Mac_virt.Machine_identifier.create () in
    Out_channel.write_all
      (machine_identifier_file bundle)
      ~data:(Mac_virt.Machine_identifier.to_data machine_identifier);
    machine_identifier
  in
  match read_file_if_present (machine_identifier_file bundle) with
  | None -> create_and_save ()
  | Some data ->
    (match Mac_virt.Machine_identifier.of_data data with
     | Some machine_identifier -> machine_identifier
     | None -> create_and_save ())
;;

let boot ~bundle ~requested_cpu_count ~requested_memory_gib =
  let%bind.Or_error hardware_model = load_hardware_model ~bundle in
  let machine_identifier = load_machine_identifier ~bundle in
  let auxiliary_storage =
    Mac_virt.Auxiliary_storage.load ~path:(aux_storage_file bundle)
  in
  let%bind.Or_error storage =
    Mac_virt.Storage_device.disk_image ~path:(disk_image_file bundle) ~read_only:false
  in
  let platform =
    Mac_virt.Mac_platform.create ~hardware_model ~machine_identifier ~auxiliary_storage
  in
  let cpu_count = Option.value requested_cpu_count ~default:4 in
  let memory_size =
    Int64.of_int (Option.value requested_memory_gib ~default:8 * bytes_per_gib)
  in
  let config =
    Mac_virt.Configuration.create_macos
      ~cpu_count
      ~memory_size
      ~platform
      ~storage_devices:[ storage ]
  in
  print_s [%message "booting" ~bundle (cpu_count : int) (memory_size : int64)];
  Mac_virt.Gui.boot config ~title:"macOS"
;;

let install_command =
  Command.basic
    ~summary:"Create a VM bundle and install macOS into it"
    (let%map_open.Command bundle =
       flag "bundle" (required string) ~doc:"DIR directory to hold the VM bundle"
     and ipsw =
       flag
         "ipsw"
         (optional string)
         ~doc:"PATH local restore image (default: download the latest supported)"
     and disk_size_gib =
       flag "disk-size-gib" (optional_with_default 64 int) ~doc:"N main disk size in GiB"
     and requested_cpu_count =
       flag "cpu-count" (optional int) ~doc:"N virtual CPUs (default: the image minimum)"
     and requested_memory_gib =
       flag "memory-gib" (optional int) ~doc:"N RAM in GiB (default: the image minimum)"
     in
     fun () ->
       match
         install ~bundle ~ipsw ~disk_size_gib ~requested_cpu_count ~requested_memory_gib
       with
       | Ok () -> ()
       | Error e -> print_s [%message "install failed" (e : Error.t)])
;;

let boot_command =
  Command.basic
    ~summary:"Boot an installed VM bundle in a window"
    (let%map_open.Command bundle =
       flag "bundle" (required string) ~doc:"DIR the VM bundle to boot"
     and requested_cpu_count =
       flag "cpu-count" (optional int) ~doc:"N virtual CPUs (default: 4)"
     and requested_memory_gib =
       flag "memory-gib" (optional int) ~doc:"N RAM in GiB (default: 8)"
     in
     fun () ->
       match boot ~bundle ~requested_cpu_count ~requested_memory_gib with
       | Ok () -> ()
       | Error e -> print_s [%message "boot failed" (e : Error.t)])
;;

let () =
  Command_unix.run
    (Command.group
       ~summary:"Create, install, and boot macOS virtual machines"
       [ "install", install_command; "boot", boot_command ])
;;
