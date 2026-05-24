open! Core

let bytes_per_gib = 1024 * 1024 * 1024

let command =
  Command.basic
    ~summary:"Create a virtual machine with the given resources and report its state"
    (let%map_open.Command cpu_count =
       flag
         "cpu-count"
         (optional_with_default 1 int)
         ~doc:"N number of virtual CPUs (default: 1)"
     and memory_gib =
       flag
         "memory-gib"
         (optional_with_default 4 int)
         ~doc:"N amount of RAM in GiB (default: 4)"
     in
     fun () ->
       print_s
         [%message "virtualization support" ~supported:(Mac_virt.is_supported () : bool)];
       let config =
         Mac_virt.Configuration.create
           ~cpu_count
           ~memory_size:(Int64.of_int (memory_gib * bytes_per_gib))
       in
       match Mac_virt.Virtual_machine.create config with
       | Ok vm ->
         print_s
           [%message
             "created virtual machine"
               ~state:
                 (Mac_virt.Virtual_machine.state vm : Mac_virt.Virtual_machine.State.t)]
       | Error e ->
         print_s
           [%message
             "could not create machine (expected until a boot loader is added)"
               (e : Error.t)])
;;

let () = Command_unix.run command
